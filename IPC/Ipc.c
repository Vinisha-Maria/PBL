ime.h>

#include "ipc.h"

/* Receive buffer must be >= the queue's mq_msgsize. Linux's default
 * ceiling is 8192,/* ipc.c - POSIX message queue wrapper (see ipc.h) */

#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <t so this also copes with a stale queue created by an
 * older build with a different message size. */
#define RECV_BUF_SIZE 8192

static void deadline_in(struct timespec *ts, int ms)
{
    clock_gettime(CLOCK_REALTIME, ts);
    ts->tv_sec  += ms / 1000;
    ts->tv_nsec += (long)(ms % 1000) * 1000000L;
    if (ts->tv_nsec >= 1000000000L) {
        ts->tv_sec++;
        ts->tv_nsec -= 1000000000L;
    }
}

mqd_t ipc_open(const char *name, int mode)
{
    struct mq_attr attr;
    memset(&attr, 0, sizeof attr);
    attr.mq_maxmsg  = MQ_MAX_MESSAGES;
    attr.mq_msgsize = MSG_SIZE;

    int flags = (mode == IPC_READ ? O_RDONLY : O_WRONLY) | O_CREAT;
    mqd_t q = mq_open(name, flags, 0600, &attr);
    if (q == (mqd_t)-1)
        fprintf(stderr, "ipc_open(%s): %s\n", name, strerror(errno));
    return q;
}

void ipc_close(mqd_t q)
{
    if (q != (mqd_t)-1)
        mq_close(q);
}

void ipc_unlink_all(void)
{
    mq_unlink(QUEUE_CMD);
    mq_unlink(QUEUE_RESP);
    mq_unlink(QUEUE_LOG);
}

int ipc_send(mqd_t q, const message_t *m)
{
    int r;
    do {
        r = mq_send(q, (const char *)m, sizeof *m, MQ_PRIORITY);
    } while (r == -1 && errno == EINTR);
    return r;
}

int ipc_send_timeout(mqd_t q, const message_t *m, int timeout_ms)
{
    struct timespec ts;
    deadline_in(&ts, timeout_ms);
    int r;
    do {
        r = mq_timedsend(q, (const char *)m, sizeof *m, MQ_PRIORITY, &ts);
    } while (r == -1 && errno == EINTR);
    return r;
}

static int finish_recv(ssize_t n, const char *buf, message_t *m)
{
    if (n < 0)
        return -1;
    if ((size_t)n < sizeof *m) {
        errno = EBADMSG;
        return -1;
    }
    memcpy(m, buf, sizeof *m);
    return 0;
}

int ipc_recv(mqd_t q, message_t *m)
{
    char buf[RECV_BUF_SIZE];
    ssize_t n;
    do {
        n = mq_receive(q, buf, sizeof buf, NULL);
    } while (n == -1 && errno == EINTR);
    return finish_recv(n, buf, m);
}

int ipc_recv_timeout(mqd_t q, message_t *m, int timeout_ms)
{
    char buf[RECV_BUF_SIZE];
    struct timespec ts;
    deadline_in(&ts, timeout_ms);
    ssize_t n;
    do {
        n = mq_timedreceive(q, buf, sizeof buf, NULL, &ts);
    } while (n == -1 && errno == EINTR);
    return finish_recv(n, buf, m);
}

