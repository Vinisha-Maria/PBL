/*
 * logger_process.c - the Logging Process
 *
 *   /tmp/sim_log_fifo --> [reader thread] --> ring buffer --> [writer thread] --> sim_execution.log (all lines)
 *                                                                              --> sim_error.log     (WARN + ERROR)
 *
 * Usage: ./logger_process [--ctrl]
 *   --ctrl  also connect to /tmp/sim_ctrl.sock (HELLO/READY/PING/PONG/SHUTDOWN).
 *           This is a minimal stand-in for the ipc_sock module (see ASSUMPTIONS).
 *
 * Stops cleanly on SIGTERM, SIGINT or a SHUTDOWN control message: every line
 * already in the pipe or the ring buffer is written to disk before exit.
 */
#define _GNU_SOURCE
#include "ipc_fifo.h"

#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

#define EXEC_LOG       "sim_execution.log"
#define ERROR_LOG      "sim_error.log"
#define CTRL_SOCK_PATH "/tmp/sim_ctrl.sock"
#define RB_SLOTS       4096          /* ring buffer capacity in lines (~2 MB) */
#define DRAIN_MAX_MS   2000          /* max time spent emptying the pipe at shutdown */

/* ------------------------------------------------------------------ */
/* small helpers                                                       */
/* ------------------------------------------------------------------ */

static long long now_ms(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
        return 0;
    return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static void sleep_ms(long ms)
{
    struct timespec ts = { .tv_sec = ms / 1000, .tv_nsec = (ms % 1000) * 1000000L };
    while (nanosleep(&ts, &ts) != 0 && errno == EINTR) { /* resume the remainder */ }
}

/* pthread calls only fail on programmer error (bad pointer, double lock...):
 * report and abort instead of continuing with a corrupt buffer. */
static void must(int err, const char *what)
{
    if (err != 0) {
        fprintf(stderr, "logger: fatal: %s: %s\n", what, strerror(err));
        abort();
    }
}

static volatile sig_atomic_t g_stop = 0;

static void on_stop_signal(int sig)
{
    (void)sig;
    g_stop = 1;                      /* async-signal-safe: only sets a flag */
}

/* ------------------------------------------------------------------ */
/* thread-safe ring buffer: 1 producer (reader thread), 1 consumer      */
/* ------------------------------------------------------------------ */

typedef struct {
    size_t len;
    char   text[LOG_LINE_MAX];       /* line without '\n', NUL-terminated */
} slot_t;

typedef struct {
    slot_t          slots[RB_SLOTS];
    size_t          head;            /* next slot to fill   */
    size_t          tail;            /* next slot to drain  */
    size_t          count;           /* slots in use        */
    int             closed;          /* producer is finished */
    unsigned long   full_waits;      /* how often push() had to wait: buffer was full */
    pthread_mutex_t mu;
    pthread_cond_t  not_full;
    pthread_cond_t  not_empty;
} ring_t;

static ring_t g_rb = {
    .mu        = PTHREAD_MUTEX_INITIALIZER,
    .not_full  = PTHREAD_COND_INITIALIZER,
    .not_empty = PTHREAD_COND_INITIALIZER,
};

/* Blocks while the buffer is full (back-pressure, nothing is dropped here). */
static void rb_push(const char *s, size_t len)
{
    must(pthread_mutex_lock(&g_rb.mu), "lock");
    if (g_rb.count == RB_SLOTS)
        g_rb.full_waits++;
    while (g_rb.count == RB_SLOTS)
        must(pthread_cond_wait(&g_rb.not_full, &g_rb.mu), "cond_wait not_full");

    slot_t *sl = &g_rb.slots[g_rb.head];
    memcpy(sl->text, s, len);
    sl->text[len] = '\0';
    sl->len = len;
    g_rb.head = (g_rb.head + 1) % RB_SLOTS;
    g_rb.count++;

    must(pthread_cond_signal(&g_rb.not_empty), "cond_signal not_empty");
    must(pthread_mutex_unlock(&g_rb.mu), "unlock");
}

/* returns 1 = got a line, 0 = empty (only when !block), -1 = closed AND empty */
static int rb_pop(slot_t *out, int block)
{
    must(pthread_mutex_lock(&g_rb.mu), "lock");
    while (g_rb.count == 0) {
        if (g_rb.closed) {
            must(pthread_mutex_unlock(&g_rb.mu), "unlock");
            return -1;
        }
        if (!block) {
            must(pthread_mutex_unlock(&g_rb.mu), "unlock");
            return 0;
        }
        must(pthread_cond_wait(&g_rb.not_empty, &g_rb.mu), "cond_wait not_empty");
    }
    slot_t *sl = &g_rb.slots[g_rb.tail];
    out->len = sl->len;
    memcpy(out->text, sl->text, sl->len + 1);
    g_rb.tail = (g_rb.tail + 1) % RB_SLOTS;
    g_rb.count--;

    must(pthread_cond_signal(&g_rb.not_full), "cond_signal not_full");
    must(pthread_mutex_unlock(&g_rb.mu), "unlock");
    return 1;
}

static void rb_close(void)
{
    must(pthread_mutex_lock(&g_rb.mu), "lock");
    g_rb.closed = 1;
    must(pthread_cond_broadcast(&g_rb.not_empty), "cond_broadcast");
    must(pthread_mutex_unlock(&g_rb.mu), "unlock");
}

/* ------------------------------------------------------------------ */
/* writer thread: ring buffer -> files                                  */
/* ------------------------------------------------------------------ */

static FILE *g_exec, *g_err;
static unsigned long g_lines_in, g_lines_out, g_err_out, g_disk_errors;   /* in: reader thread; out: writer thread */

static int is_warn_or_error(const char *line)
{
    const char *p = strchr(line, '|');           /* skip the timestamp */
    if (p == NULL)
        return 0;
    p++;
    return strncmp(p, "WARN|", 5) == 0 || strncmp(p, "ERROR|", 6) == 0;
}

static void disk_error(const char *file)
{
    if (g_disk_errors++ == 0 || g_disk_errors % 1000 == 0)
        fprintf(stderr, "logger: write to %s failed: %s (line dropped)\n", file, strerror(errno));
}

static void put_line(FILE *f, const char *name, const slot_t *s)
{
    if (fwrite(s->text, 1, s->len, f) != s->len || fputc('\n', f) == EOF)
        disk_error(name);
}

static void flush_files(void)
{
    if (fflush(g_exec) != 0) disk_error(EXEC_LOG);
    if (fflush(g_err)  != 0) disk_error(ERROR_LOG);
}

/* Signals are blocked in this thread (inherited from main), so stdio calls
 * never see EINTR. Errors such as ENOSPC are counted, but the loop keeps
 * draining the buffer so the reader thread can never deadlock on a full ring. */
static void *writer_thread(void *arg)
{
    (void)arg;
    slot_t s;
    for (;;) {
        int r = rb_pop(&s, 0);
        if (r == 0) {                /* ring is empty: make the data visible on disk, then sleep */
            flush_files();
            r = rb_pop(&s, 1);
        }
        if (r < 0)
            break;                   /* closed and empty: everything has been written */

        put_line(g_exec, EXEC_LOG, &s);
        g_lines_out++;
        if (is_warn_or_error(s.text)) {
            put_line(g_err, ERROR_LOG, &s);
            g_err_out++;
        }
    }
    flush_files();
    return NULL;
}

/* ------------------------------------------------------------------ */
/* optional control-socket client (stand-in for ipc_sock)               */
/* ------------------------------------------------------------------ */

static int    g_ctrl_enabled = 0;
static int    g_ctrl_fd = -1;
static char   g_ctrl_buf[256];
static size_t g_ctrl_len = 0;

static void ctrl_drop(void)
{
    if (g_ctrl_fd >= 0 && close(g_ctrl_fd) != 0 && errno != EINTR) { /* ignore */ }
    g_ctrl_fd = -1;
    g_ctrl_len = 0;
}

static void ctrl_send(const char *msg)
{
    size_t len = strlen(msg);
    ssize_t n = send(g_ctrl_fd, msg, len, MSG_NOSIGNAL);    /* MSG_NOSIGNAL: no SIGPIPE */
    if (n != (ssize_t)len)
        ctrl_drop();
}

static void ctrl_connect(void)
{
    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (fd < 0)
        return;
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof addr);
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, CTRL_SOCK_PATH, sizeof addr.sun_path - 1);
    if (connect(fd, (struct sockaddr *)&addr, sizeof addr) != 0) {
        if (close(fd) != 0) { /* ignore */ }
        return;
    }
    g_ctrl_fd = fd;
    g_ctrl_len = 0;
    ctrl_send("HELLO logger\n");
    if (g_ctrl_fd >= 0)
        ctrl_send("READY\n");
}

/* Called from the reader thread's loop; polls the socket without blocking. */
static void ctrl_service(void)
{
    static long long next_ms = 0;
    long long now = now_ms();
    if (now < next_ms)
        return;
    next_ms = now + ((g_ctrl_fd < 0) ? 1000 : 100);

    if (g_ctrl_fd < 0) {
        ctrl_connect();
        return;
    }
    if (g_ctrl_len >= sizeof g_ctrl_buf - 1)      /* garbage without newline */
        g_ctrl_len = 0;

    ssize_t n = recv(g_ctrl_fd, g_ctrl_buf + g_ctrl_len, sizeof g_ctrl_buf - 1 - g_ctrl_len, 0);
    if (n == 0) { ctrl_drop(); return; }          /* peer closed */
    if (n < 0) {
        if (errno != EAGAIN && errno != EINTR)
            ctrl_drop();
        return;
    }
    g_ctrl_len += (size_t)n;
    g_ctrl_buf[g_ctrl_len] = '\0';

    char *nl;
    while (g_ctrl_fd >= 0 && (nl = strchr(g_ctrl_buf, '\n')) != NULL) {
        *nl = '\0';
        if (strcmp(g_ctrl_buf, "PING") == 0) {
            ctrl_send("PONG\n");
        } else if (strcmp(g_ctrl_buf, "SHUTDOWN") == 0) {
            fprintf(stderr, "logger: SHUTDOWN received\n");
            g_stop = 1;
        }
        size_t used = (size_t)(nl - g_ctrl_buf) + 1;
        if (g_ctrl_fd >= 0) {
            memmove(g_ctrl_buf, g_ctrl_buf + used, g_ctrl_len - used + 1);
            g_ctrl_len -= used;
        }
    }
}

/* ------------------------------------------------------------------ */
/* reader thread: FIFO -> ring buffer                                   */
/* ------------------------------------------------------------------ */

static void *reader_thread(void *arg)
{
    (void)arg;

    /* This is the only thread that receives SIGTERM / SIGINT. */
    sigset_t unb;
    sigemptyset(&unb);
    sigaddset(&unb, SIGTERM);
    sigaddset(&unb, SIGINT);
    must(pthread_sigmask(SIG_UNBLOCK, &unb, NULL), "pthread_sigmask");

    char line[LOG_LINE_MAX];

    while (!g_stop) {
        ssize_t n = log_read_line(line, sizeof line);
        if (n > 0) {
            rb_push(line, (size_t)n);
            g_lines_in++;
        } else if (n == 0) {
            /* Core closed its end (exit or crash). log_read_line() already
             * reopened the FIFO, so just keep waiting for the next Core. */
            fprintf(stderr, "logger: Core disconnected, waiting for it to reconnect\n");
            sleep_ms(10);            /* guards against a hot loop on odd kernels */
        } else if (n == LOG_READ_TIMEOUT) {
            /* idle tick: falls through to re-check g_stop and the control socket */
        } else {
            fprintf(stderr, "logger: read error: %s\n", strerror(errno));
            sleep_ms(100);
        }
        if (g_ctrl_enabled)
            ctrl_service();
    }

    /* Shutdown: take whatever the Core already put into the pipe. */
    long long deadline = now_ms() + DRAIN_MAX_MS;
    while (now_ms() < deadline) {
        ssize_t n = log_read_line(line, sizeof line);
        if (n <= 0)
            break;                   /* pipe empty (or error): nothing more to rescue */
        rb_push(line, (size_t)n);
        g_lines_in++;
    }

    rb_close();                      /* tells the writer thread: finish and exit */
    return NULL;
}

/* ------------------------------------------------------------------ */

int main(int argc, char **argv)
{
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--ctrl") == 0) {
            g_ctrl_enabled = 1;
        } else {
            fprintf(stderr, "usage: %s [--ctrl]\n", argv[0]);
            return 2;
        }
    }

    /* signals: handler for SIGTERM/SIGINT (no SA_RESTART, so blocking calls
     * return EINTR), SIGPIPE ignored. Then block them in main so every thread
     * created below inherits "blocked"; the reader thread unblocks them. */
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_stop_signal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    if (sigaction(SIGTERM, &sa, NULL) != 0 || sigaction(SIGINT, &sa, NULL) != 0) {
        perror("logger: sigaction");
        return 1;
    }
    struct sigaction ign;
    memset(&ign, 0, sizeof ign);
    ign.sa_handler = SIG_IGN;
    sigemptyset(&ign.sa_mask);
    if (sigaction(SIGPIPE, &ign, NULL) != 0) {
        perror("logger: sigaction SIGPIPE");
        return 1;
    }
    sigset_t blk;
    sigemptyset(&blk);
    sigaddset(&blk, SIGTERM);
    sigaddset(&blk, SIGINT);
    must(pthread_sigmask(SIG_BLOCK, &blk, NULL), "pthread_sigmask");

    /* resources */
    if (fifo_create() != 0) {
        perror("logger: fifo_create");
        return 1;
    }
    if (log_open_reader() != 0) {
        perror("logger: log_open_reader");
        return 1;
    }
    g_exec = fopen(EXEC_LOG, "a");
    g_err  = fopen(ERROR_LOG, "a");
    if (g_exec == NULL || g_err == NULL) {
        perror("logger: fopen log files");
        return 1;
    }
    if (setvbuf(g_exec, NULL, _IOFBF, 1 << 16) != 0 || setvbuf(g_err, NULL, _IOFBF, 1 << 14) != 0) {
        perror("logger: setvbuf");
        return 1;
    }

    pthread_t rt, wt;
    int e = pthread_create(&wt, NULL, writer_thread, NULL);
    if (e != 0) {
        fprintf(stderr, "logger: pthread_create writer: %s\n", strerror(e));
        return 1;
    }
    e = pthread_create(&rt, NULL, reader_thread, NULL);
    if (e != 0) {
        fprintf(stderr, "logger: pthread_create reader: %s\n", strerror(e));
        rb_close();
        must(pthread_join(wt, NULL), "join writer");
        return 1;
    }
    fprintf(stderr, "logger: ready (fifo %s)\n", SIM_LOG_FIFO_PATH);

    must(pthread_join(rt, NULL), "join reader");
    must(pthread_join(wt, NULL), "join writer");

    /* final flush to stable storage, then clean up */
    int rc = 0;
    if (fflush(g_exec) != 0 || fsync(fileno(g_exec)) != 0) { perror("logger: sync " EXEC_LOG); rc = 1; }
    if (fflush(g_err)  != 0 || fsync(fileno(g_err))  != 0) { perror("logger: sync " ERROR_LOG); rc = 1; }
    if (fclose(g_exec) != 0) { perror("logger: close " EXEC_LOG);  rc = 1; }
    if (fclose(g_err)  != 0) { perror("logger: close " ERROR_LOG); rc = 1; }
    if (log_close() != 0)    { perror("logger: log_close");        rc = 1; }
    if (fifo_unlink() != 0)  { perror("logger: fifo_unlink");      rc = 1; }
    if (g_ctrl_fd >= 0)
        ctrl_drop();

    fprintf(stderr, "logger: stopped. lines_in=%lu lines_out=%lu error_lines=%lu "
                    "ring_full_waits=%lu disk_errors=%lu\n",
            g_lines_in, g_lines_out, g_err_out, g_rb.full_waits, g_disk_errors);
    return (rc != 0 || g_disk_errors != 0) ? 1 : 0;
}
