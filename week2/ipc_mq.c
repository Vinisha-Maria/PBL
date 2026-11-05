/*
 * ipc_mq.c - POSIX message queue for UI -> Core commands.
 *
 * WHY THESE QUEUE SIZES
 *   mq_msgsize = sizeof(struct sim_cmd) (12 bytes on Linux x86-64)
 *     Every message is exactly one struct, so the receiver always gets a
 *     whole command. A bigger value would only waste kernel memory.
 *   mq_maxmsg  = 10
 *     The UI is driven by a human, so only a few commands can be waiting.
 *     10 is also the default Linux limit (/proc/sys/fs/mqueue/msg_max):
 *     a normal (non-root) user may not ask for more, mq_open would fail
 *     with EINVAL. 10 x 12 bytes = 120 bytes of queue data only.
 *     A full queue is reported to the user ("Core is busy"), not hidden.
 *
 * The queue descriptor is a module variable because the agreed API has
 * no handle parameter. Each process has its own copy.
 */

#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <fcntl.h>
#include <mqueue.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include "ipc_mq.h"

static mqd_t g_mq = (mqd_t)-1;      /* this process's queue descriptor */

/* 1 if the command type is one of the 7 agreed types. */
static int type_is_valid(int type)
{
    return type >= CMD_TYPE_MIN && type <= CMD_TYPE_MAX;
}

/* An old queue with other sizes would break the protocol: refuse it. */
static int check_attr(mqd_t q)
{
    struct mq_attr a;

    if (mq_getattr(q, &a) != 0)
        return -1;
    if (a.mq_msgsize != MQ_CMD_MSGSIZE) {
        errno = EPROTO;
        return -1;
    }
    return 0;
}

/* Keep the descriptor if it is usable, otherwise close it again. */
static int adopt(mqd_t q)
{
    if (q == (mqd_t)-1)
        return -1;
    if (check_attr(q) != 0) {
        int saved = errno;
        mq_close(q);
        errno = saved;
        return -1;
    }
    g_mq = q;
    return 0;
}

/* UI side: open the existing queue for writing. O_NONBLOCK so the UI
 * never freezes: a full queue gives EAGAIN at once. */
int mq_cmd_open_sender(void)
{
    if (g_mq != (mqd_t)-1) {
        errno = EALREADY;
        return -1;
    }
    /* No O_CREAT: if the Core is not running, this fails with ENOENT. */
    return adopt(mq_open(MQ_CMD_NAME, O_WRONLY | O_NONBLOCK));
}

/* Core side: create the queue (or open it if it exists) for reading.
 * Blocking mode, so mq_timedreceive really waits for the timeout. */
int mq_cmd_open_receiver(void)
{
    struct mq_attr attr;

    if (g_mq != (mqd_t)-1) {
        errno = EALREADY;
        return -1;
    }
    memset(&attr, 0, sizeof attr);
    attr.mq_flags   = 0;
    attr.mq_maxmsg  = MQ_CMD_MAXMSG;
    attr.mq_msgsize = MQ_CMD_MSGSIZE;

    return adopt(mq_open(MQ_CMD_NAME, O_RDONLY | O_CREAT, 0600, &attr));
}

/* Send one command (priority 0 for all commands). */
int mq_cmd_send(const struct sim_cmd *cmd)
{
    if (cmd == NULL) {
        errno = EINVAL;
        return -1;
    }
    if (g_mq == (mqd_t)-1) {
        errno = EBADF;
        return -1;
    }
    if (!type_is_valid(cmd->type)) {
        errno = EINVAL;
        return -1;
    }

    for (;;) {
        if (mq_send(g_mq, (const char *)cmd, sizeof *cmd, 0) == 0)
            return 0;
        if (errno == EINTR)         /* interrupted by a signal: try again */
            continue;
        return -1;                  /* EAGAIN = queue full, others = real error */
    }
}

/* Receive one command with a timeout in milliseconds. */
int mq_cmd_receive(struct sim_cmd *cmd, int timeout_ms)
{
    struct sim_cmd msg;
    struct timespec deadline;
    ssize_t n;

    if (cmd == NULL) {
        errno = EINVAL;
        return -1;
    }
    if (g_mq == (mqd_t)-1) {
        errno = EBADF;
        return -1;
    }

    /* mq_timedreceive wants an absolute time on CLOCK_REALTIME.
     * We compute it once, so a retry after EINTR keeps the same deadline. */
    if (timeout_ms >= 0) {
        if (clock_gettime(CLOCK_REALTIME, &deadline) != 0)
            return -1;
        deadline.tv_sec  += timeout_ms / 1000;
        deadline.tv_nsec += (long)(timeout_ms % 1000) * 1000000L;
        if (deadline.tv_nsec >= 1000000000L) {
            deadline.tv_sec  += 1;
            deadline.tv_nsec -= 1000000000L;
        }
    }

    for (;;) {
        memset(&msg, 0, sizeof msg);
        if (timeout_ms < 0)
            n = mq_receive(g_mq, (char *)&msg, sizeof msg, NULL);
        else
            n = mq_timedreceive(g_mq, (char *)&msg, sizeof msg, NULL, &deadline);

        if (n < 0) {
            if (errno == EINTR)     /* signal: wait again until the deadline */
                continue;
            return -1;              /* ETIMEDOUT, EAGAIN or a real error */
        }
        if ((size_t)n != sizeof msg) {
            errno = EBADMSG;        /* someone sent a wrong-sized message */
            return -1;
        }
        if (!type_is_valid(msg.type)) {
            errno = EINVAL;         /* never pass garbage to the Core */
            return -1;
        }
        *cmd = msg;
        return 0;
    }
}

/* Close the descriptor. */
int mq_cmd_close(void)
{
    int rc;

    if (g_mq == (mqd_t)-1)
        return 0;
    rc = mq_close(g_mq);
    g_mq = (mqd_t)-1;               /* never use a closed descriptor again */
    return rc;
}

/* Remove the queue name. ENOENT (already gone) counts as success. */
int mq_cmd_unlink(void)
{
    if (mq_unlink(MQ_CMD_NAME) == 0)
        return 0;
    if (errno == ENOENT)
        return 0;
    return -1;
}
