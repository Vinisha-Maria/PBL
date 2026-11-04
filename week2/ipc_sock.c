#define _GNU_SOURCE
#include "ipc_sock.h"

#include <errno.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

static long now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

static void make_addr(struct sockaddr_un *a)
{
    memset(a, 0, sizeof *a);
    a->sun_family = AF_UNIX;
    strncpy(a->sun_path, SIM_CTRL_PATH, sizeof a->sun_path - 1);
}

/* Wait until fd is readable. 1 = readable, 0 = timeout, -1 = error. */
static int wait_readable(int fd, long deadline_ms)
{
    for (;;) {
        long left = deadline_ms - now_ms();
        if (left < 0) left = 0;
        struct pollfd p = { .fd = fd, .events = POLLIN };
        int r = poll(&p, 1, (int)left);
        if (r > 0) return 1;
        if (r == 0) return 0;
        if (errno != EINTR) return -1;
    }
}

int sock_ctrl_server_start(void)
{
    struct sockaddr_un a;
    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) return -1;

    make_addr(&a);
    unlink(SIM_CTRL_PATH);                       /* remove stale socket file */
    if (bind(fd, (struct sockaddr *)&a, sizeof a) < 0 || listen(fd, 8) < 0) {
        int e = errno;
        close(fd);
        unlink(SIM_CTRL_PATH);
        errno = e;
        return -1;
    }
    chmod(SIM_CTRL_PATH, 0600);
    return fd;
}

int sock_ctrl_accept(int server_fd, char *name, size_t name_size, int timeout_ms)
{
    long deadline = now_ms() + timeout_ms;
    int r = wait_readable(server_fd, deadline);
    if (r == 0) { errno = ETIMEDOUT; return -1; }
    if (r < 0) return -1;

    int fd = accept4(server_fd, NULL, NULL, SOCK_CLOEXEC);
    if (fd < 0) return -1;

    char line[CTRL_MSG_MAX];
    int n = sock_ctrl_recv(fd, line, sizeof line, (int)(deadline - now_ms() > 0 ? deadline - now_ms() : 1));
    if (n <= 0 || strncmp(line, "HELLO ", 6) != 0) {
        close(fd);
        errno = (n == 0) ? ETIMEDOUT : EPROTO;
        return -1;
    }
    if (name && name_size) {
        strncpy(name, line + 6, name_size - 1);
        name[name_size - 1] = '\0';
    }
    return fd;
}

int sock_ctrl_client_connect(const char *name)
{
    struct sockaddr_un a;
    make_addr(&a);

    for (int attempt = 0; attempt < 20; attempt++) {     /* up to ~2 s */
        int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
        if (fd < 0) return -1;
        if (connect(fd, (struct sockaddr *)&a, sizeof a) == 0) {
            char msg[CTRL_MSG_MAX];
            snprintf(msg, sizeof msg, "HELLO %s", name);
            if (sock_ctrl_send(fd, msg) < 0) { close(fd); return -1; }
            return fd;
        }
        int e = errno;
        close(fd);
        if (e != ENOENT && e != ECONNREFUSED) { errno = e; return -1; }
        usleep(100 * 1000);
    }
    errno = ECONNREFUSED;
    return -1;
}

int sock_ctrl_send(int fd, const char *msg)
{
    char line[CTRL_MSG_MAX + 2];
    size_t len = strlen(msg);
    if (len > CTRL_MSG_MAX - 1) { errno = EMSGSIZE; return -1; }
    memcpy(line, msg, len);
    line[len++] = '\n';

    size_t off = 0;
    while (off < len) {
        ssize_t w = send(fd, line + off, len - off, MSG_NOSIGNAL);
        if (w < 0) { if (errno == EINTR) continue; return -1; }
        off += (size_t)w;
    }
    return 0;
}

int sock_ctrl_recv(int fd, char *buf, size_t size, int timeout_ms)
{
    long deadline = now_ms() + timeout_ms;
    size_t n = 0;

    if (size < 2) { errno = EINVAL; return -1; }
    for (;;) {
        int r = wait_readable(fd, deadline);
        if (r == 0) return 0;                    /* timeout */
        if (r < 0) return -1;

        char c;
        ssize_t got = recv(fd, &c, 1, 0);
        if (got == 0) { errno = ECONNRESET; return -1; }   /* peer closed */
        if (got < 0) { if (errno == EINTR) continue; return -1; }

        if (c == '\n') {
            if (n == 0) continue;                /* ignore empty lines */
            buf[n] = '\0';
            return (int)n;
        }
        if (n < size - 1) buf[n++] = c;          /* longer lines are truncated */
    }
}

void sock_ctrl_close(int fd)
{
    if (fd >= 0) close(fd);
}

void sock_ctrl_unlink(void)
{
    unlink(SIM_CTRL_PATH);
}

/* ---------- responder thread (PING -> PONG, SHUTDOWN -> callback) ---------- */

struct responder_arg { int fd; void (*on_shutdown)(void); };

static void *responder_main(void *p)
{
    struct responder_arg a = *(struct responder_arg *)p;
    free(p);
    char msg[CTRL_MSG_MAX];

    for (;;) {
        int n = sock_ctrl_recv(a.fd, msg, sizeof msg, 1000);
        if (n == 0) continue;
        if (n < 0) break;                        /* launcher gone -> treat as shutdown */
        if (strcmp(msg, "PING") == 0) {
            if (sock_ctrl_send(a.fd, "PONG") < 0) break;
        } else if (strcmp(msg, "SHUTDOWN") == 0) {
            break;
        }
    }
    if (a.on_shutdown) a.on_shutdown();
    else raise(SIGTERM);
    return NULL;
}

int sock_ctrl_start_responder(int fd, void (*on_shutdown)(void))
{
    struct responder_arg *a = malloc(sizeof *a);
    pthread_t t;
    if (!a) return -1;
    a->fd = fd;
    a->on_shutdown = on_shutdown;
    int rc = pthread_create(&t, NULL, responder_main, a);
    if (rc != 0) { free(a); errno = rc; return -1; }
    pthread_detach(t);
    return 0;
}
