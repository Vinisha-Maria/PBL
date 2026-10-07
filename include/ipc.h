/*
 * ipc.h - thin wrapper over POSIX message queues
 *
 * Everyone calls these instead of raw mq_* functions.
 * Every process opens its queues with O_CREAT, so start order
 * does not matter (UI, Core and Logger can launch in any order).
 */

#ifndef IPC_H
#define IPC_H

#include <mqueue.h>
#include "common.h"

#define IPC_READ  0
#define IPC_WRITE 1

/* Open (and create if missing) a queue. Returns (mqd_t)-1 on error. */
mqd_t ipc_open(const char *name, int mode);

/* Close the descriptor (does not remove the queue). */
void ipc_close(mqd_t q);

/* Remove all three queues from the system. Call once, at shutdown. */
void ipc_unlink_all(void);

/* Blocking send / receive. Return 0 on success, -1 on error (errno set). */
int ipc_send(mqd_t q, const message_t *m);
int ipc_recv(mqd_t q, message_t *m);

/* Same, but give up after timeout_ms (errno == ETIMEDOUT).
 * timeout_ms == 0 means "do not wait": useful for draining a queue. */
int ipc_send_timeout(mqd_t q, const message_t *m, int timeout_ms);
int ipc_recv_timeout(mqd_t q, message_t *m, int timeout_ms);

#endif /* IPC_H */
