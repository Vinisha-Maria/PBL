/*
 * ipc_mq.h - UI -> Core commands over a POSIX message queue (/sim_cmd).
 *
 * Roles:
 *   Core (receiver) creates the queue:      mq_cmd_open_receiver()
 *   UI   (sender)   opens the existing one: mq_cmd_open_sender()
 *
 * All functions return 0 on success and -1 on error with errno set
 * (mq_cmd_receive: ETIMEDOUT means "no message in time").
 * One process uses one queue descriptor, kept inside this module.
 */
#ifndef IPC_MQ_H
#define IPC_MQ_H

#include "sim_types.h"

#define MQ_CMD_NAME     "/sim_cmd"

/* Queue size limits, set explicitly in mq_open (see ipc_mq.c for why). */
#define MQ_CMD_MAXMSG   10                          /* max messages waiting */
#define MQ_CMD_MSGSIZE  ((long)sizeof(struct sim_cmd)) /* bytes per message  */

/* Sender (UI). Opens the EXISTING queue, non-blocking.
 * errno == ENOENT means the Core has not created the queue yet. */
int mq_cmd_open_sender(void);

/* Receiver (Core). Creates the queue if needed (mode 0600) and opens it. */
int mq_cmd_open_receiver(void);

/* Send one command. EINTR is retried. errno == EAGAIN means queue full,
 * EINVAL means invalid command type, EBADF means queue not open. */
int mq_cmd_send(const struct sim_cmd *cmd);

/* Receive one command, waiting at most timeout_ms milliseconds
 * (0 = just look, negative = wait forever). EINTR is retried until the
 * original deadline. errno == ETIMEDOUT: nothing arrived in time.
 * EBADMSG: wrong message size. EINVAL: invalid command type
 * (the bad message is removed from the queue). */
int mq_cmd_receive(struct sim_cmd *cmd, int timeout_ms);

/* Close this process's descriptor (safe to call when not open). */
int mq_cmd_close(void);

/* Remove the queue name from the system. Normally done by the Core
 * (the creator) when it quits. A missing queue is not an error. */
int mq_cmd_unlink(void);

#endif
