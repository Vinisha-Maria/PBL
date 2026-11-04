/*
 * ipc_fifo.c - named-pipe log channel (see ipc_fifo.h)
 */
#define _GNU_SOURCE
#include "ipc_fifo.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define WRITE_TIMEOUT_MS    100        /* max time log_write() waits on a full pipe */
#define REOPEN_INTERVAL_MS  1000       /* min gap between failed open attempts      */
#define READ_POLL_MS        200        /* log_read_line() wakes up this often       */
#define PIPE_TARGET_SIZE    (1 << 20)  /* ask for a 1 MB pipe (best effort)         */
#define ACC_SIZE            8192       /* reader-side reassembly buffer             */

static long long now_ms(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
        return 0;
    return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static void close_fd(int *fd)
{
    if (*fd >= 0) {
        /* On Linux the descriptor is released even when close() fails with
         * EINTR, so we never retry. Nothing useful to do on other errors. */
        if (close(*fd) != 0 && errno != EINTR) {
            /* ignored on purpose */
        }
        *fd = -1;
    }
}

/* ------------------------------------------------------------------ */
/* Creation / removal                                                  */
/* ------------------------------------------------------------------ */

int fifo_create(void)
{
    if (mkfifo(SIM_LOG_FIFO_PATH, 0600) == 0)
        return 0;
    if (errno != EEXIST)
        return -1;

    /* Something is already there: accept it only if it is a FIFO that we
     * own (protects against someone pre-creating the name in /tmp). */
    struct stat st;
    if (lstat(SIM_LOG_FIFO_PATH, &st) != 0)
        return -1;
    if (!S_ISFIFO(st.st_mode) || st.st_uid != geteuid()) {
        errno = EEXIST;
        return -1;
    }
    return 0;
}

int fifo_unlink(void)
{
    if (unlink(SIM_LOG_FIFO_PATH) != 0 && errno != ENOENT)
        return -1;
    return 0;
}

/* ------------------------------------------------------------------ */
/* Writer side (Core)                                                  */
/* ------------------------------------------------------------------ */

static pthread_mutex_t g_wlock = PTHREAD_MUTEX_INITIALIZER;
static int       g_wfd = -1;
static long long g_next_open_ms = 0;   /* do not retry open() before this time */
static int       g_sigpipe_done = 0;

/* caller holds g_wlock */
static int open_writer_locked(void)
{
    if (g_wfd >= 0)
        return 0;

    if (!g_sigpipe_done) {
        struct sigaction cur;
        if (sigaction(SIGPIPE, NULL, &cur) == 0 && cur.sa_handler == SIG_DFL) {
            struct sigaction ign;
            memset(&ign, 0, sizeof ign);
            ign.sa_handler = SIG_IGN;
            sigemptyset(&ign.sa_mask);
            if (sigaction(SIGPIPE, &ign, NULL) != 0)
                return -1;
        }
        g_sigpipe_done = 1;
    }

    /* O_NONBLOCK: fails at once with ENXIO if nobody is reading, instead of
     * blocking the Core until a Logger appears. */
    int fd = open(SIM_LOG_FIFO_PATH, O_WRONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0)
        return -1;
    g_wfd = fd;
    return 0;
}

int log_open_writer(void)
{
    int e = pthread_mutex_lock(&g_wlock);
    if (e != 0) { errno = e; return -1; }
    int rc = open_writer_locked();
    int saved = errno;
    (void)pthread_mutex_unlock(&g_wlock);
    errno = saved;
    return rc;
}

/* caller holds g_wlock */
static int send_locked(const char *line, size_t len)
{
    long long now = now_ms();

    if (g_wfd < 0) {
        if (now < g_next_open_ms) {          /* recently failed: drop cheaply */
            errno = ENXIO;
            return -1;
        }
        if (open_writer_locked() != 0) {
            int e = errno;
            g_next_open_ms = now + REOPEN_INTERVAL_MS;
            errno = e;
            return -1;
        }
    }

    long long deadline = now + WRITE_TIMEOUT_MS;
    for (;;) {
        ssize_t n = write(g_wfd, line, len);
        if (n == (ssize_t)len)
            return 0;
        if (n >= 0) {                        /* cannot happen for len <= PIPE_BUF */
            errno = EIO;
            return -1;
        }
        if (errno == EINTR)
            continue;
        if (errno == EAGAIN) {               /* pipe full: wait a little, then give up */
            long long left = deadline - now_ms();
            if (left <= 0) {
                errno = EAGAIN;
                return -1;
            }
            struct pollfd p = { .fd = g_wfd, .events = POLLOUT, .revents = 0 };
            if (poll(&p, 1, (int)left) < 0 && errno != EINTR)
                return -1;
            continue;
        }
        if (errno == EPIPE) {                /* Logger is gone: forget the fd, retry open next call */
            close_fd(&g_wfd);
            g_next_open_ms = 0;
            errno = EPIPE;
            return -1;
        }
        return -1;
    }
}

int log_write(const char *level, const char *source, const char *fmt, ...)
{
    if (level == NULL || source == NULL || fmt == NULL) {
        errno = EINVAL;
        return -1;
    }
    if (strcmp(level, "INFO") != 0 && strcmp(level, "WARN") != 0 &&
        strcmp(level, "ERROR") != 0) {
        errno = EINVAL;
        return -1;
    }

    /* 1. timestamp (UTC, millisecond resolution; no '|' inside) */
    struct timespec ts;
    struct tm tm;
    if (clock_gettime(CLOCK_REALTIME, &ts) != 0)
        return -1;
    if (gmtime_r(&ts.tv_sec, &tm) == NULL)
        return -1;

    /* 2. SOURCE: copy and neutralise characters that would break the framing */
    char src[48];
    size_t i;
    for (i = 0; i < sizeof src - 1 && source[i] != '\0'; i++)
        src[i] = (source[i] == '|' || source[i] == '\n' || source[i] == '\r') ? '_' : source[i];
    src[i] = '\0';

    /* 3. build the line: header, then message, then '\n'. Total < 512 bytes. */
    char line[LOG_LINE_MAX];
    int h = snprintf(line, sizeof line - 1, "%04d-%02d-%02dT%02d:%02d:%02d.%03ldZ|%s|%s|",
                     tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                     tm.tm_hour, tm.tm_min, tm.tm_sec, ts.tv_nsec / 1000000,
                     level, src);
    if (h < 0 || (size_t)h >= sizeof line - 2) {
        errno = EINVAL;
        return -1;
    }
    size_t hdr = (size_t)h;
    size_t room = sizeof line - 1 - hdr;     /* leaves 1 byte for '\n' (vsnprintf adds a NUL inside room) */

    va_list ap;
    va_start(ap, fmt);
    int m = vsnprintf(line + hdr, room, fmt, ap);
    va_end(ap);
    if (m < 0) {
        errno = EINVAL;
        return -1;
    }
    size_t body = ((size_t)m >= room) ? room - 1 : (size_t)m;   /* truncate long messages */
    size_t len = hdr + body;
    for (size_t k = hdr; k < len; k++)       /* a newline inside the message would split the record */
        if (line[k] == '\n' || line[k] == '\r')
            line[k] = ' ';
    line[len++] = '\n';                      /* len is now <= 511 */

    /* 4. one write() of <= PIPE_BUF bytes => atomic, never interleaved */
    int e = pthread_mutex_lock(&g_wlock);
    if (e != 0) {
        errno = e;
        return -1;
    }
    int rc = send_locked(line, len);
    int saved = errno;
    (void)pthread_mutex_unlock(&g_wlock);
    errno = saved;
    return rc;
}

/* ------------------------------------------------------------------ */
/* Reader side (Logger)                                                */
/* ------------------------------------------------------------------ */

static int    g_rfd = -1;
static char   g_acc[ACC_SIZE];          /* bytes read but not yet returned */
static size_t g_off = 0, g_end = 0;     /* valid data is g_acc[g_off .. g_end) */

int log_open_reader(void)
{
    if (g_rfd >= 0)
        return 0;
    int fd = open(SIM_LOG_FIFO_PATH, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0)
        return -1;
#ifdef F_SETPIPE_SZ
    /* A bigger pipe absorbs bursts. Best effort: failure is harmless. */
    if (fcntl(fd, F_SETPIPE_SZ, PIPE_TARGET_SIZE) < 0) {
        /* ignored on purpose */
    }
#endif
    g_rfd = fd;
    g_off = g_end = 0;
    return 0;
}

/* All writers closed: read() returned 0. A FIFO that has seen a writer come
 * and go keeps reporting POLLHUP, so the read end must be reopened to wait
 * for the next writer. Open the NEW descriptor first and close the old one
 * afterwards, so the pipe never has zero readers (a Core that connects in
 * that instant would otherwise get ENXIO). */
static int reopen_reader(void)
{
    int fd = open(SIM_LOG_FIFO_PATH, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0)
        return -1;
    int old = g_rfd;
    g_rfd = fd;
    close_fd(&old);
    return 0;
}

ssize_t log_read_line(char *buf, size_t size)
{
    if (buf == NULL || size < 2) {
        errno = EINVAL;
        return -1;
    }
    if (g_rfd < 0) {
        errno = EBADF;
        return -1;
    }

    for (;;) {
        size_t avail = g_end - g_off;
        char *nl = (avail > 0) ? memchr(g_acc + g_off, '\n', avail) : NULL;

        if (nl != NULL) {                    /* a complete line is buffered */
            size_t linelen = (size_t)(nl - (g_acc + g_off));
            size_t copy = (linelen < size - 1) ? linelen : size - 1;
            memcpy(buf, g_acc + g_off, copy);
            buf[copy] = '\0';
            g_off += linelen + 1;
            if (g_off == g_end)
                g_off = g_end = 0;
            if (copy == 0)
                continue;                    /* skip empty lines; 0 is reserved for EOF */
            return (ssize_t)copy;
        }

        if (avail >= LOG_LINE_MAX) {         /* a conforming writer never does this */
            size_t copy = (avail < size - 1) ? avail : size - 1;
            memcpy(buf, g_acc + g_off, copy);
            buf[copy] = '\0';
            g_off = g_end = 0;
            fprintf(stderr, "ipc_fifo: %zu bytes without newline, emitted truncated\n", avail);
            return (ssize_t)copy;
        }

        /* need more bytes: first move the partial line to the front */
        if (g_off > 0) {
            memmove(g_acc, g_acc + g_off, avail);
            g_off = 0;
            g_end = avail;
        }

        struct pollfd p = { .fd = g_rfd, .events = POLLIN, .revents = 0 };
        int pr = poll(&p, 1, READ_POLL_MS);
        if (pr == 0)
            return LOG_READ_TIMEOUT;
        if (pr < 0)
            return (errno == EINTR) ? LOG_READ_TIMEOUT : -1;

        ssize_t n = read(g_rfd, g_acc + g_end, sizeof g_acc - g_end);
        if (n > 0) {
            g_end += (size_t)n;
            continue;
        }
        if (n == 0) {                        /* every writer closed its end */
            if (g_end > g_off)
                fprintf(stderr, "ipc_fifo: discarded %zu bytes of an incomplete line "
                                "(writer closed mid-line)\n", g_end - g_off);
            g_off = g_end = 0;
            if (reopen_reader() != 0)
                return -1;
            return 0;
        }
        if (errno == EINTR || errno == EAGAIN)
            continue;
        return -1;
    }
}

/* ------------------------------------------------------------------ */

int log_close(void)
{
    int rc = 0;
    int e = pthread_mutex_lock(&g_wlock);
    if (e != 0) { errno = e; return -1; }
    close_fd(&g_wfd);
    (void)pthread_mutex_unlock(&g_wlock);
    close_fd(&g_rfd);
    g_off = g_end = 0;
    return rc;
}
