/* ipc_sock.h - control channel over a Unix domain socket (owner: Mia, Team Leader)
 *
 * Protocol: newline-terminated text lines on /tmp/sim_ctrl.sock
 *   client -> launcher : HELLO <name>   (sent automatically by sock_ctrl_client_connect)
 *                        READY          (sent by the process once it is fully initialised)
 *                        PONG           (answer to PING)
 *   launcher -> client : PING, SHUTDOWN
 *
 * Return convention: 0 / fd >= 0 on success, -1 on error (errno set).
 * sock_ctrl_recv returns >0 = message length, 0 = timeout, -1 = error or peer closed.
 */
#ifndef IPC_SOCK_H
#define IPC_SOCK_H

#include <stddef.h>

#define SIM_CTRL_PATH "/tmp/sim_ctrl.sock"
#define CTRL_MSG_MAX  128

/* ---- launcher (server) side ---- */
int  sock_ctrl_server_start(void);
int  sock_ctrl_accept(int server_fd, char *name, size_t name_size, int timeout_ms);

/* ---- process (client) side ---- */
int  sock_ctrl_client_connect(const char *name);

/* ---- both sides ---- */
int  sock_ctrl_send(int fd, const char *msg);
int  sock_ctrl_recv(int fd, char *buf, size_t size, int timeout_ms);
void sock_ctrl_close(int fd);
void sock_ctrl_unlink(void);

/* Convenience for UI / Core / Logger: starts a background thread that answers
 * PING with PONG and calls on_shutdown() when SHUTDOWN arrives (or when the
 * launcher disappears). If on_shutdown is NULL the process gets SIGTERM instead.
 * Call it right after sending READY. */
int  sock_ctrl_start_responder(int fd, void (*on_shutdown)(void));

#endif
