/* sim_launcher.c - starts Logger, Core, UI; supervises them; shuts everything down cleanly.
 *
 *   ./sim_launcher                 normal run (binaries in current directory)
 *   ./sim_launcher --bin-dir DIR   use binaries from DIR
 *   ./sim_launcher --cleanup       only remove leftover IPC objects, then exit
 *
 * Exit code: 0 = clean (UI quit normally, or Ctrl-C); 1 = startup failure, crash or hang.
 */
#define _GNU_SOURCE
#include "ipc_sock.h"

#include <errno.h>
#include <mqueue.h>
#include <semaphore.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define NPROC            3
#define READY_TIMEOUT_MS 5000
#define PING_PERIOD_MS   2000
#define PONG_TIMEOUT_MS  1000
#define MAX_MISSED       3
#define GRACE_MS         3000

enum { P_LOGGER = 0, P_CORE = 1, P_UI = 2 };

typedef struct {
    const char *name;      /* name sent in HELLO */
    const char *binary;    /* executable file name */
    pid_t pid;
    int   fd;              /* control connection */
    int   alive;
    int   missed;
} proc_t;

static proc_t procs[NPROC] = {
    { "logger", "logger_process", 0, -1, 0, 0 },
    { "core",   "core_process",   0, -1, 0, 0 },
    { "ui",     "ui_process",     0, -1, 0, 0 },
};

static volatile sig_atomic_t g_stop = 0;
static int g_server = -1;
static const char *g_bin_dir = ".";

static void on_signal(int s) { (void)s; g_stop = 1; }

static void msleep(int ms)
{
    struct timespec ts = { ms / 1000, (ms % 1000) * 1000000L };
    nanosleep(&ts, NULL);                        /* returns early on a signal: fine */
}

/* Remove every IPC object of the project. Safe to call at any time (errors ignored). */
static void cleanup_ipc(void)
{
    mq_unlink("/sim_cmd");
    sem_unlink("/sim_state_lock");
    shm_unlink("/sim_state");
    unlink("/tmp/sim_log_fifo");
    sock_ctrl_unlink();
}

static int start_proc(int i)
{
    char path[512];
    snprintf(path, sizeof path, "%s/%s", g_bin_dir, procs[i].binary);

    pid_t pid = fork();
    if (pid < 0) { perror("[launcher] fork"); return -1; }
    if (pid == 0) {
        execl(path, procs[i].binary, (char *)NULL);
        fprintf(stderr, "[launcher] exec %s failed: %s\n", path, strerror(errno));
        _exit(127);
    }
    procs[i].pid = pid;
    procs[i].alive = 1;

    char hello[CTRL_MSG_MAX], msg[CTRL_MSG_MAX];
    int fd = sock_ctrl_accept(g_server, hello, sizeof hello, READY_TIMEOUT_MS);
    if (fd < 0) {
        fprintf(stderr, "[launcher] %s did not connect: %s\n", procs[i].name, strerror(errno));
        return -1;
    }
    if (strcmp(hello, procs[i].name) != 0) {
        fprintf(stderr, "[launcher] expected HELLO %s but got HELLO %s\n", procs[i].name, hello);
        sock_ctrl_close(fd);
        return -1;
    }
    procs[i].fd = fd;

    int n = sock_ctrl_recv(fd, msg, sizeof msg, READY_TIMEOUT_MS);
    if (n <= 0 || strcmp(msg, "READY") != 0) {
        fprintf(stderr, "[launcher] %s never sent READY\n", procs[i].name);
        return -1;
    }
    fprintf(stderr, "[launcher] %s READY (pid %d)\n", procs[i].name, (int)procs[i].pid);
    return 0;
}

/* Returns 1 if some process has died; sets *rc (0 only if the UI quit normally). */
static int reap_dead(int *rc)
{
    int dead = 0;
    for (int i = 0; i < NPROC; i++) {
        int st;
        if (!procs[i].alive) continue;
        if (waitpid(procs[i].pid, &st, WNOHANG) > 0) {
            procs[i].alive = 0;
            dead = 1;
            if (WIFEXITED(st))
                fprintf(stderr, "[launcher] %s exited with code %d\n", procs[i].name, WEXITSTATUS(st));
            else
                fprintf(stderr, "[launcher] %s killed by signal %d\n", procs[i].name, WTERMSIG(st));
            *rc = (i == P_UI && WIFEXITED(st) && WEXITSTATUS(st) == 0) ? 0 : 1;
        }
    }
    return dead;
}

static void shutdown_all(void)
{
    for (int i = NPROC - 1; i >= 0; i--) {       /* UI first, Logger last so logs are flushed */
        if (!procs[i].alive) continue;
        if (procs[i].fd >= 0) sock_ctrl_send(procs[i].fd, "SHUTDOWN");

        int st, waited = 0, done = 0;
        while (waited < GRACE_MS) {
            if (waitpid(procs[i].pid, &st, WNOHANG) > 0) { done = 1; break; }
            usleep(50 * 1000);
            waited += 50;
        }
        if (!done) {
            fprintf(stderr, "[launcher] %s ignored SHUTDOWN, sending SIGTERM\n", procs[i].name);
            kill(procs[i].pid, SIGTERM);
            for (waited = 0; waited < 1000 && !done; waited += 50) {
                if (waitpid(procs[i].pid, &st, WNOHANG) > 0) done = 1;
                else usleep(50 * 1000);
            }
            if (!done) {
                fprintf(stderr, "[launcher] %s still alive, sending SIGKILL\n", procs[i].name);
                kill(procs[i].pid, SIGKILL);
                waitpid(procs[i].pid, &st, 0);
            }
        }
        procs[i].alive = 0;
    }
    for (int i = 0; i < NPROC; i++) { sock_ctrl_close(procs[i].fd); procs[i].fd = -1; }
    sock_ctrl_close(g_server);
    g_server = -1;
    cleanup_ipc();                               /* safety net: nothing may be left behind */
}

/* Send PING, wait for PONG. 1 = ok, 0 = missed, -1 = connection broken. */
static int ping(int i)
{
    char msg[CTRL_MSG_MAX];
    if (sock_ctrl_send(procs[i].fd, "PING") < 0) return -1;
    for (;;) {
        int n = sock_ctrl_recv(procs[i].fd, msg, sizeof msg, PONG_TIMEOUT_MS);
        if (n == 0) return 0;
        if (n < 0) return -1;
        if (strcmp(msg, "PONG") == 0) return 1;
    }
}

int main(int argc, char **argv)
{
    int rc = 0;

    for (int a = 1; a < argc; a++) {
        if (strcmp(argv[a], "--bin-dir") == 0 && a + 1 < argc) g_bin_dir = argv[++a];
        else if (strcmp(argv[a], "--cleanup") == 0) { cleanup_ipc(); return 0; }
        else { fprintf(stderr, "usage: %s [--bin-dir DIR] [--cleanup]\n", argv[0]); return 2; }
    }

    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_signal;                   /* no SA_RESTART: sleeps get interrupted */
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    signal(SIGPIPE, SIG_IGN);

    cleanup_ipc();                               /* start from a clean slate */
    g_server = sock_ctrl_server_start();
    if (g_server < 0) { perror("[launcher] control socket"); return 1; }

    for (int i = 0; i < NPROC; i++) {            /* Logger -> Core -> UI */
        if (g_stop || start_proc(i) < 0) { rc = 1; goto stop; }
    }
    fprintf(stderr, "[launcher] all processes READY\n");

    while (!g_stop) {
        for (int t = 0; t < PING_PERIOD_MS && !g_stop; t += 100) {
            msleep(100);
            if (reap_dead(&rc)) goto stop;
        }
        if (g_stop) break;

        for (int i = 0; i < NPROC; i++) {
            int r = ping(i);
            if (r == 1) { procs[i].missed = 0; continue; }
            if (r < 0) {                         /* socket broken: most likely the process died */
                msleep(100);
                if (!reap_dead(&rc)) { fprintf(stderr, "[launcher] lost control link to %s\n", procs[i].name); rc = 1; }
                goto stop;
            }
            if (++procs[i].missed >= MAX_MISSED) {
                fprintf(stderr, "[launcher] %s is not answering PING (hung)\n", procs[i].name);
                rc = 1;
                goto stop;
            }
        }
    }

stop:
    fprintf(stderr, "[launcher] shutting down\n");
    shutdown_all();
    return rc;
}
