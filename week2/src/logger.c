/*
 * logger.c - Logging process of the OS process scheduler simulator
 *
 * Reads events from the Core on QUEUE_LOG and writes one line per event:
 *
 *   simulator.log   [t=0] P1 started
 *                   [t=2] context switch P1 to P2
 *                   [t=9] P2 finished, waiting time 4
 *                   Statistics [RR]: 3 process(es), total time 9, ...
 *   errors.log      2026-10-07 14:03:21 ERROR: invalid burst time for X ...
 *
 * [t=N] is the simulator's clock, not wall-clock time.
 * Files are opened in append mode and flushed after every line, so you can
 * watch them live with `tail -f simulator.log` and nothing is lost if the
 * Logger is killed.
 *
 * Stops on EVT_QUIT (sent by the Core on `quit`) or on Ctrl-C / SIGTERM.
 * Set SCHED_LOG_ECHO=1 to also print every line to the terminal.
 *
 * Build: gcc -std=gnu11 -Wall -Wextra -Iinclude \
 *            src/logger.c src/ipc.c -lrt -o logger
 */

#define _GNU_SOURCE
#include <errno.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "ipc.h"

#define POLL_MS 500

static volatile sig_atomic_t g_signalled = 0;
static FILE *g_main_log = NULL;
static FILE *g_err_log  = NULL;
static int   g_echo     = 0;

static void on_signal(int sig)
{
    (void)sig;
    g_signalled = 1;
}

/* Make sure a fixed-size char field is NUL-terminated before printing. */
static void terminate(char *s, size_t n)
{
    s[n - 1] = '\0';
}

static void write_line(FILE *f, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vfprintf(f, fmt, ap);
    va_end(ap);
    fputc('\n', f);
    fflush(f);
}

/* Write the same line to a log file (and the terminal if echo is on). */
#define LOG_TO(file, ...)                                   \
    do {                                                    \
        write_line((file), __VA_ARGS__);                    \
        if (g_echo) {                                       \
            printf(__VA_ARGS__);                            \
            putchar('\n');                                  \
            fflush(stdout);                                 \
        }                                                   \
    } while (0)

static void wall_clock(char *buf, size_t n)
{
    time_t now = time(NULL);
    struct tm tm;
    localtime_r(&now, &tm);
    strftime(buf, n, "%Y-%m-%d %H:%M:%S", &tm);
}

/* Returns 1 when the Logger should stop. */
static int handle(message_t *m)
{
    switch (m->type) {
    case EVT_START:
        terminate(m->payload.event.proc, MAX_NAME_LEN);
        LOG_TO(g_main_log, "[t=%d] %s started",
               m->payload.event.time, m->payload.event.proc);
        break;

    case EVT_SWITCH:
        terminate(m->payload.event.proc, MAX_NAME_LEN);
        terminate(m->payload.event.next, MAX_NAME_LEN);
        LOG_TO(g_main_log, "[t=%d] context switch %s to %s",
               m->payload.event.time,
               m->payload.event.proc, m->payload.event.next);
        break;

    case EVT_FINISH:
        terminate(m->payload.event.proc, MAX_NAME_LEN);
        LOG_TO(g_main_log, "[t=%d] %s finished, waiting time %d",
               m->payload.event.time, m->payload.event.proc,
               m->payload.event.waiting_time);
        break;

    case EVT_STATS:
        terminate(m->payload.text, MAX_TEXT_LEN);
        LOG_TO(g_main_log, "%s", m->payload.text);
        break;

    case EVT_ERROR: {
        char ts[32];
        terminate(m->payload.text, MAX_TEXT_LEN);
        wall_clock(ts, sizeof ts);
        LOG_TO(g_err_log, "%s ERROR: %s", ts, m->payload.text);
        break;
    }

    case EVT_QUIT:
        return 1;

    default: {
        char ts[32];
        wall_clock(ts, sizeof ts);
        LOG_TO(g_err_log, "%s ERROR: Logger received unknown message type %d",
               ts, m->type);
        break;
    }
    }
    return 0;
}

int main(void)
{
    const char *echo = getenv("SCHED_LOG_ECHO");
    g_echo = (echo && *echo && *echo != '0');

    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_signal;               /* no SA_RESTART on purpose */
    sigaction(SIGINT,  &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    g_main_log = fopen(LOG_FILE_MAIN, "a");
    g_err_log  = fopen(LOG_FILE_ERRORS, "a");
    if (!g_main_log || !g_err_log) {
        fprintf(stderr, "[logger] cannot open log files: %s\n", strerror(errno));
        return 1;
    }

    mqd_t q = ipc_open(QUEUE_LOG, IPC_READ);
    if (q == (mqd_t)-1) {
        fprintf(stderr, "[logger] could not open queue "
                        "(POSIX message queues need Linux)\n");
        return 1;
    }
    fprintf(stderr, "[logger] ready, writing %s and %s\n",
            LOG_FILE_MAIN, LOG_FILE_ERRORS);

    while (!g_signalled) {
        message_t m;
        if (ipc_recv_timeout(q, &m, POLL_MS) == -1) {
            if (errno == ETIMEDOUT || errno == EINTR)
                continue;
            fprintf(stderr, "[logger] receive failed: %s\n", strerror(errno));
            break;
        }
        if (handle(&m))
            break;
    }

    ipc_close(q);
    fclose(g_main_log);
    fclose(g_err_log);
    fprintf(stderr, "[logger] bye\n");
    return 0;
}
