/*
 * ui_process.c - the UI process of the simulator.
 *
 * ASSUMPTION: no existing UI code was pasted, so this is a plain
 * terminal menu (numbers 1-7 = command types). If your old UI looks
 * different, change only draw_status_locked() and the menu text; the
 * IPC parts do not depend on the look.
 *
 * Threads
 *   state thread : reads the simulator state with shm_read_state() every
 *                  REFRESH_MS and redraws the status panel.
 *   main thread  : reads the user's input and sends commands over the
 *                  POSIX message queue (ipc_mq).
 * One mutex (g_ui.lock) protects the data they share AND the terminal
 * output, so two threads never print at the same time.
 *
 * The UI talks to the Core ONLY through IPC:
 *   commands: /sim_cmd (message queue)   state: /sim_state (shared memory)
 *
 * Cleanup: on QUIT, Ctrl-D (end of input), SIGINT or SIGTERM the UI stops
 * the thread and closes its queue descriptor. Removing the queue name
 * (mq_cmd_unlink) is the job of the Core, which created the queue.
 */

#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <limits.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "sim_types.h"
#include "ipc_mq.h"
#include "ipc_shm.h"
#include "ipc_sock.h"

static const char *cmd_name(int type)
{
    switch (type) {
        case CMD_START:      return "START";
        case CMD_PAUSE:      return "PAUSE";
        case CMD_RESUME:     return "RESUME";
        case CMD_STEP:       return "STEP";
        case CMD_ADD_PROCESS:return "ADD_PROCESS";
        case CMD_RESET:      return "RESET";
        case CMD_QUIT:       return "QUIT";
        default:             return "UNKNOWN";
    }
}

#define REFRESH_MS      500     /* how often the state is read */
#define SLICE_MS        50      /* the thread sleeps in small slices to stop fast */
#define INPUT_POLL_MS   200     /* how often the input loop checks for a stop */
#define STATUS_ROWS     8       /* rows 1..8: status panel */
#define MENU_ROW        10      /* menu starts here */
#define PROMPT_ROW      16      /* input prompt row */
#define SHOW_QUEUE_MAX  12      /* how many queue entries to show */

/* Data shared by the two threads. Everything is protected by 'lock'. */
struct ui_shared {
    pthread_mutex_t  lock;
    struct sim_state state;             /* last snapshot read */
    int  have_state;                    /* 1 once a snapshot was read */
    int  core_online;                   /* last shm_read_state() succeeded */
    int  shm_errno;                     /* errno of the last failed read */
    int  core_restarted;                /* Core (re)appeared: reopen the queue */
    int  queue_connected;               /* command queue is open */
    char message[160];                  /* result of the last user action */
};

static struct ui_shared g_ui = { .lock = PTHREAD_MUTEX_INITIALIZER };

static volatile sig_atomic_t g_stop = 0;    /* set by signal handler / QUIT */
static void ui_shutdown(void)
{
    g_stop = 1;
}
static int      g_tty = 0;                  /* 1 if stdout is a terminal */
static int      g_mq_open = 0;              /* main thread only */
static unsigned g_seq = 0;                  /* main thread only */

/* ---------- small helpers ---------- */

/* Text for an errno value, safe to call from both threads. */
static const char *err_text(int err, char *buf, size_t size)
{
    if (strerror_r(err, buf, size) != 0)
        snprintf(buf, size, "error %d", err);
    return buf;
}

/* Sleep for ms milliseconds, also when a signal interrupts. */
static void sleep_ms(int ms)
{
    struct timespec ts;

    ts.tv_sec  = ms / 1000;
    ts.tv_nsec = (long)(ms % 1000) * 1000000L;
    while (nanosleep(&ts, &ts) != 0 && errno == EINTR)
        ;
}

/* Signal handler: only sets a flag (this is async-signal-safe). */
static void on_signal(int sig)
{
    (void)sig;
    g_stop = 1;
}

/* No SA_RESTART: a signal must interrupt poll() so the loop sees g_stop. */
static void install_signals(void)
{
    struct sigaction sa;

    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_signal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    if (sigaction(SIGINT, &sa, NULL) != 0)
        perror("sigaction(SIGINT)");
    if (sigaction(SIGTERM, &sa, NULL) != 0)
        perror("sigaction(SIGTERM)");
}

/* ---------- drawing (always called with the lock held) ---------- */

/* Draw the status panel. On a terminal it is redrawn in place at rows
 * 1..8 (cursor saved and restored); otherwise one plain line is printed. */
static void draw_status_locked(void)
{
    const struct sim_state *s = &g_ui.state;
    char err[64], qtxt[200], line[STATUS_ROWS][220];
    size_t used = 0;
    int qlen = s->queue_len;
    const char *stale;

    if (!g_tty) {
        if (g_ui.core_online)
            printf("[state] core=ONLINE tick=%lu pid=%d queue_len=%d mem=%zu/%zu stack=%d status=%d\n",
                   s->tick, s->running_pid, s->queue_len,
                   s->mem_used, s->mem_total, s->stack_depth, s->status);
        else
            printf("[state] core=OFFLINE\n");
        fflush(stdout);
        return;
    }

    /* The snapshot comes from another process: keep the length in range. */
    if (qlen < 0)  qlen = 0;
    if (qlen > 64) qlen = 64;

    qtxt[0] = '\0';
    for (int i = 0; i < qlen && i < SHOW_QUEUE_MAX; i++) {
        int w = snprintf(qtxt + used, sizeof qtxt - used, "%d ", s->queue[i]);
        if (w < 0 || (size_t)w >= sizeof qtxt - used)
            break;
        used += (size_t)w;
    }
    if (qlen > SHOW_QUEUE_MAX && used + 4 < sizeof qtxt)
        strcpy(qtxt + used, "...");

    stale = (g_ui.have_state && !g_ui.core_online) ? "  (last known)" : "";

    snprintf(line[0], sizeof line[0], "=== Simulator UI ===");
    if (g_ui.core_online)
        snprintf(line[1], sizeof line[1], "Core link (shared memory): ONLINE");
    else
        snprintf(line[1], sizeof line[1],
                 "Core link (shared memory): OFFLINE - waiting for the Core (%s)",
                 err_text(g_ui.shm_errno, err, sizeof err));
    snprintf(line[2], sizeof line[2], "Command queue %s: %s", MQ_CMD_NAME,
             g_ui.queue_connected ? "connected" : "not connected");
    if (g_ui.have_state) {
        snprintf(line[3], sizeof line[3],
                 "Tick: %lu   Running PID: %d   Status: %d%s",
                 s->tick, s->running_pid, s->status, stale);
        snprintf(line[4], sizeof line[4], "Ready queue (%d): %s", s->queue_len, qtxt);
        snprintf(line[5], sizeof line[5], "Memory: %zu / %zu bytes", s->mem_used, s->mem_total);
        snprintf(line[6], sizeof line[6], "Stack depth: %d", s->stack_depth);
    } else {
        snprintf(line[3], sizeof line[3], "Tick: -   Running PID: -   Status: -");
        snprintf(line[4], sizeof line[4], "Ready queue: -");
        snprintf(line[5], sizeof line[5], "Memory: -");
        snprintf(line[6], sizeof line[6], "Stack depth: -");
    }
    snprintf(line[7], sizeof line[7], "Last: %s", g_ui.message);

    fputs("\033" "7", stdout);                      /* save cursor */
    for (int r = 0; r < STATUS_ROWS; r++)
        printf("\033[%d;1H\033[K%s", r + 1, line[r]);
    fputs("\033" "8", stdout);                      /* restore cursor */
    fflush(stdout);
}

/* printf into the terminal while holding the lock. */
__attribute__((format(printf, 1, 2)))
static void ui_print(const char *fmt, ...)
{
    va_list ap;

    pthread_mutex_lock(&g_ui.lock);
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    fflush(stdout);
    pthread_mutex_unlock(&g_ui.lock);
}

/* Store the result of a user action and show it. */
__attribute__((format(printf, 1, 2)))
static void set_message(const char *fmt, ...)
{
    va_list ap;

    pthread_mutex_lock(&g_ui.lock);
    va_start(ap, fmt);
    vsnprintf(g_ui.message, sizeof g_ui.message, fmt, ap);
    va_end(ap);
    if (g_tty) {
        draw_status_locked();
    } else {
        printf("[ui] %s\n", g_ui.message);
        fflush(stdout);
    }
    pthread_mutex_unlock(&g_ui.lock);
}

static void set_queue_connected(int connected)
{
    pthread_mutex_lock(&g_ui.lock);
    g_ui.queue_connected = connected;
    pthread_mutex_unlock(&g_ui.lock);
}

/* Print the input prompt on a fixed row (clears everything below it). */
static void prompt(int row, const char *text)
{
    pthread_mutex_lock(&g_ui.lock);
    if (g_tty)
        printf("\033[%d;1H\033[J%s", row, text);
    else
        printf("%s\n", text);
    fflush(stdout);
    pthread_mutex_unlock(&g_ui.lock);
}

/* The command menu (printed once). */
static void draw_menu(void)
{
    pthread_mutex_lock(&g_ui.lock);
    if (g_tty)
        printf("\033[%d;1H", MENU_ROW);
    printf("---- Commands ----\n");
    printf(" 1) START    2) PAUSE    3) RESUME    4) STEP\n");
    printf(" 5) ADD_PROCESS (asks for an integer argument)    6) RESET\n");
    printf(" 7) QUIT (tells the Core to quit, then closes this UI)\n");
    printf(" Ctrl-D or Ctrl-C closes only this UI.\n");
    fflush(stdout);
    pthread_mutex_unlock(&g_ui.lock);
}

/* ---------- state thread ---------- */

/* Read the state again and again and redraw the panel. */
static void *state_thread(void *arg)
{
    (void)arg;

    while (!g_stop) {
        struct sim_state snap;
        int rc, e;

        errno = 0;
        rc = shm_read_state(&snap);         /* Varun's function */
        e = errno;

        pthread_mutex_lock(&g_ui.lock);
        if (rc == 0) {
            if (!g_ui.core_online)
                g_ui.core_restarted = 1;    /* Core came (back): reopen queue */
            g_ui.state = snap;
            g_ui.have_state = 1;
            g_ui.core_online = 1;
            g_ui.shm_errno = 0;
        } else {
            g_ui.core_online = 0;           /* keep the old snapshot as "last known" */
            g_ui.shm_errno = e;
        }
        draw_status_locked();
        pthread_mutex_unlock(&g_ui.lock);

        for (int waited = 0; waited < REFRESH_MS && !g_stop; waited += SLICE_MS)
            sleep_ms(SLICE_MS);
    }
    return NULL;
}

/* ---------- input (main thread) ---------- */

/* Read one line. Returns 1 = got a line, 0 = end of input, -1 = stop.
 * poll() lets a signal interrupt the wait (EINTR is handled). */
static int read_line(char *buf, size_t size)
{
    for (;;) {
        struct pollfd p;
        int r;

        if (g_stop)
            return -1;
        p.fd = STDIN_FILENO;
        p.events = POLLIN;
        p.revents = 0;
        r = poll(&p, 1, INPUT_POLL_MS);
        if (r < 0) {
            if (errno == EINTR)
                continue;
            perror("poll(stdin)");
            return -1;
        }
        if (r == 0)
            continue;                       /* timeout: check g_stop again */
        if (fgets(buf, (int)size, stdin) == NULL) {
            if (ferror(stdin) && errno == EINTR) {
                clearerr(stdin);
                continue;
            }
            return 0;                       /* Ctrl-D / pipe closed */
        }
        return 1;
    }
}

/* Convert text to an int. Returns 0 on success, -1 if it is not a number. */
static int parse_int(const char *text, int *out)
{
    char *end;
    long v;

    errno = 0;
    v = strtol(text, &end, 10);
    if (end == text || errno == ERANGE || v < INT_MIN || v > INT_MAX)
        return -1;
    while (*end == ' ' || *end == '\t' || *end == '\n' || *end == '\r')
        end++;
    if (*end != '\0')
        return -1;
    *out = (int)v;
    return 0;
}

/* ---------- sending ---------- */

/* Send one command to the Core. Returns 0 if it was sent, -1 otherwise.
 * Every failure is explained to the user. */
static int send_command(int type, int arg)
{
    struct sim_cmd cmd;
    char err[64];
    int restarted;

    /* If the Core went away and came back, our old queue is dead: reopen. */
    pthread_mutex_lock(&g_ui.lock);
    restarted = g_ui.core_restarted;
    g_ui.core_restarted = 0;
    pthread_mutex_unlock(&g_ui.lock);
    if (restarted && g_mq_open) {
        mq_cmd_close();
        g_mq_open = 0;
        set_queue_connected(0);
    }

    /* Open the queue when needed. ENOENT = the Core is not running yet. */
    if (!g_mq_open) {
        if (mq_cmd_open_sender() != 0) {
            int e = errno;
            if (e == ENOENT)
                set_message("Core is not running (%s does not exist). Start the Core first.",
                            MQ_CMD_NAME);
            else
                set_message("Cannot open %s: %s", MQ_CMD_NAME, err_text(e, err, sizeof err));
            return -1;
        }
        g_mq_open = 1;
        set_queue_connected(1);
    }

    memset(&cmd, 0, sizeof cmd);
    cmd.type = type;
    cmd.arg  = arg;
    cmd.seq  = g_seq + 1;

    if (mq_cmd_send(&cmd) != 0) {
        int e = errno;
        switch (e) {
        case EAGAIN:        /* queue full: the Core is slow or stopped */
            set_message("Core is busy: the command queue is full. Try again in a moment.");
            break;
        case EINVAL:
            set_message("Command %d was rejected as invalid.", type);
            break;
        default:            /* real error: drop the queue, reopen next time */
            set_message("Send failed: %s. The queue will be reopened on the next command.",
                        err_text(e, err, sizeof err));
            mq_cmd_close();
            g_mq_open = 0;
            set_queue_connected(0);
            break;
        }
        return -1;
    }

    g_seq++;
    set_message("Sent %s (arg=%d, seq=%u).", cmd_name(type), arg, cmd.seq);
    return 0;
}

/* ---------- main ---------- */

