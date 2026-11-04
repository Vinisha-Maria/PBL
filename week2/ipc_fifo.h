/*
 * ipc_fifo.h - Core -> Logger log channel over the named pipe /tmp/sim_log_fifo
 *
 * Writer side (Core):   log_open_writer(), log_write(), log_close()
 * Reader side (Logger): fifo_create(), log_open_reader(), log_read_line(),
 *                       log_close(), fifo_unlink()
 *
 * Log line on the wire:  timestamp|LEVEL|SOURCE|message\n   (< 512 bytes total)
 */
#ifndef IPC_FIFO_H
#define IPC_FIFO_H

#include <stddef.h>
#include <sys/types.h>

#define SIM_LOG_FIFO_PATH "/tmp/sim_log_fifo"
#define LOG_LINE_MAX      512      /* a whole line incl. '\n' is always < this */

/* log_read_line() special return value: no complete line yet (timeout or
 * EINTR). Not an error - the caller should check its stop flag and call again. */
#define LOG_READ_TIMEOUT  (-2)

/* Create the FIFO (mode 0600). Succeeds if a FIFO owned by us already exists.
 * Returns 0 on success, -1 with errno set on failure. */
int fifo_create(void);

/* Writer side. Non-blocking open: returns 0 if connected, -1 (errno ENXIO or
 * ENOENT) if no Logger is reading yet. log_write() calls this by itself, so
 * calling it is optional. Also makes SIGPIPE ignored (only if it was still
 * at its default action) so a dead Logger can never kill the Core. */
int log_open_writer(void);

/* Format and send one log line. LEVEL must be INFO, WARN or ERROR.
 * Returns 0 if the line was handed to the pipe, -1 if it was dropped
 * (errno: ENXIO no Logger, EAGAIN pipe stayed full ~100 ms, EPIPE Logger
 * died, EINVAL bad arguments). Never blocks longer than ~100 ms, never
 * raises a signal, never crashes. Thread-safe. */
int log_write(const char *level, const char *source, const char *fmt, ...)
    __attribute__((format(printf, 3, 4)));

/* Reader side. Non-blocking open, so it returns at once even if the Core is
 * not running yet. Returns 0 or -1 (errno). */
int log_open_reader(void);

/* Read the next complete line (without '\n', NUL-terminated) into buf.
 * Returns  > 0   length of the line
 *            0   all writers closed; FIFO was reopened, keep calling
 *   LOG_READ_TIMEOUT (-2)  nothing complete yet (poll timeout / EINTR)
 *           -1   hard error, errno set
 * Reassembles lines that arrive split across several read() calls.
 * NOT thread-safe: call it from one thread only. */
ssize_t log_read_line(char *buf, size_t size);

/* Close whichever ends this process has open. Returns 0 or -1. */
int log_close(void);

/* Remove the FIFO file. Succeeds if it is already gone. */
int fifo_unlink(void);

#endif /* IPC_FIFO_H */
