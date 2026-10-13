/*
 * ui.c - UI process of the OS process scheduler simulator
 *
 * Reads commands from the keyboard, sends them to the Core over
 * QUEUE_CMD, then prints whatever the Core sends back on QUEUE_RESP
 * until it sees RESP_END.
 *
 * Commands:
 *   add <name> <burst>     algo <fcfs|sjf|rr>     quantum <n>
 *   step   run   status   reset   quit   help
 *
 * Validation split:
 *   UI   : syntax only (right number of arguments, name length).
 *   Core : meaning (invalid burst time, unknown algorithm, no processes).
 *          A non-numeric number or unknown algorithm name is therefore
 *          forwarded as an invalid value (-1) so the Core reports the error.
 */

#define _GNU_SOURCE
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ipc.h"

#define LINE_MAX_LEN   256
#define MAX_TOKENS     4
#define REPLY_TIMEOUT  10000   /* ms to wait for each reply from Core */
#define SEND_TIMEOUT   3000    /* ms to wait if the command queue is full */

static int g_seq = 0;

static void print_help(void)
{
    puts("Commands:\n"
         "  add <name> <burst>   add a process, e.g. add P1 5\n"
         "  algo <fcfs|sjf|rr>   choose scheduling algorithm\n"
         "  quantum <n>          set Round Robin time quantum\n"
         "  step                 run one scheduling step\n"
         "  run                  run until all processes finish\n"
         "  status               show ready queue and timeline\n"
         "  reset                clear all processes\n"
         "  quit                 stop the simulator\n"
         "  help                 show this text");
}

/* Parse a whole-string integer; return -1 if it is not a number so the
 * Core can reject it with its own error message. */
static int parse_int(const char *s)
{
    char *end;
    errno = 0;
    long v = strtol(s, &end, 10);
    if (end == s || *end != '\0' || errno != 0 || v > 1000000 || v < -1000000)
        return -1;
    return (int)v;
}

static int parse_algo(const char *s)
{
    if (strcmp(s, "fcfs") == 0) return ALGO_FCFS;
    if (strcmp(s, "sjf")  == 0) return ALGO_SJF;
    if (strcmp(s, "rr")   == 0) return ALGO_RR;
    return -1;                       /* Core replies "unknown algorithm" */
}

/* Throw away stale replies (e.g. from a command that timed out). */
static void drain_replies(mqd_t respq)
{
    message_t junk;
    while (ipc_recv_timeout(respq, &junk, 0) == 0)
        ;
}

static int send_cmd(mqd_t cmdq, message_t *m)
{
    m->seq = ++g_seq;
    if (ipc_send_timeout(cmdq, m, SEND_TIMEOUT) == -1) {
        if (errno == ETIMEDOUT)
            fprintf(stderr, "[ui] command queue is full - is the Core running?\n");
        else
            fprintf(stderr, "[ui] send failed: %s\n", strerror(errno));
        return -1;
    }
    return 0;
}

/* Print replies until RESP_END. Returns 0 if the reply was complete. */
static int show_replies(mqd_t respq)
{
    message_t r;
    for (;;) {
        if (ipc_recv_timeout(respq, &r, REPLY_TIMEOUT) == -1) {
            if (errno == ETIMEDOUT)
                fprintf(stderr, "[ui] no reply from Core (is it running?)\n");
            else
                fprintf(stderr, "[ui] receive failed: %s\n", strerror(errno));
            return -1;
        }
        switch (r.type) {
        case RESP_STATE:
            r.payload.text[MAX_TEXT_LEN - 1] = '\0';
            printf("%s\n", r.payload.text);
            break;
        case RESP_ERROR:
            r.payload.text[MAX_TEXT_LEN - 1] = '\0';
            printf("error: %s\n", r.payload.text);
            break;
        case RESP_END:
            return 0;
        default:
            fprintf(stderr, "[ui] unexpected message type %d\n", r.type);
            break;
        }
    }
}

int main(void)
{
    mqd_t cmdq  = ipc_open(QUEUE_CMD,  IPC_WRITE);
    mqd_t respq = ipc_open(QUEUE_RESP, IPC_READ);
    if (cmdq == (mqd_t)-1 || respq == (mqd_t)-1) {
        fprintf(stderr, "[ui] could not open queues "
                        "(POSIX message queues need Linux)\n");
        return 1;
    }

    puts("Scheduler simulator - type 'help' for commands.");

    char line[LINE_MAX_LEN];
    for (;;) {
        printf("sched> ");
        fflush(stdout);

        message_t m;
        memset(&m, 0, sizeof m);

        if (!fgets(line, sizeof line, stdin)) {      /* EOF (Ctrl-D) = quit */
            putchar('\n');
            strcpy(line, "quit");
        } else if (!strchr(line, '\n') && !feof(stdin)) {
            int c;                                   /* line too long */
            while ((c = getchar()) != '\n' && c != EOF)
                ;
            fprintf(stderr, "[ui] line too long, ignored\n");
            continue;
        }

        /* Split into tokens */
        char *tok[MAX_TOKENS];
        int n = 0;
        char *sp;
        for (char *t = strtok_r(line, " \t\r\n", &sp);
             t && n < MAX_TOKENS;
             t = strtok_r(NULL, " \t\r\n", &sp))
            tok[n++] = t;

        if (n == 0)
            continue;

        for (char *p = tok[0]; *p; p++)
            *p = (char)tolower((unsigned char)*p);

        const char *cmd = tok[0];
        int quitting = 0;

        if (strcmp(cmd, "help") == 0) {
            print_help();
            continue;

        } else if (strcmp(cmd, "add") == 0) {
            if (n != 3) { fprintf(stderr, "usage: add <name> <burst>\n"); continue; }
            if (strlen(tok[1]) >= MAX_NAME_LEN) {
                fprintf(stderr, "[ui] process name too long (max %d chars)\n",
                        MAX_NAME_LEN - 1);
                continue;
            }
            m.type = CMD_ADD;
            strncpy(m.payload.add.name, tok[1], MAX_NAME_LEN - 1);
            m.payload.add.burst = parse_int(tok[2]);

        } else if (strcmp(cmd, "algo") == 0) {
            if (n != 2) { fprintf(stderr, "usage: algo <fcfs|sjf|rr>\n"); continue; }
            for (char *p = tok[1]; *p; p++)
                *p = (char)tolower((unsigned char)*p);
            m.type = CMD_ALGO;
            m.payload.algo.id = parse_algo(tok[1]);

        } else if (strcmp(cmd, "quantum") == 0) {
            if (n != 2) { fprintf(stderr, "usage: quantum <n>\n"); continue; }
            m.type = CMD_QUANTUM;
            m.payload.quantum.value = parse_int(tok[1]);

        } else if (strcmp(cmd, "step")   == 0 ||
                   strcmp(cmd, "run")    == 0 ||
                   strcmp(cmd, "status") == 0 ||
                   strcmp(cmd, "reset")  == 0 ||
                   strcmp(cmd, "quit")   == 0) {
            if (n != 1) { fprintf(stderr, "usage: %s\n", cmd); continue; }
            if      (strcmp(cmd, "step")   == 0) m.type = CMD_STEP;
            else if (strcmp(cmd, "run")    == 0) m.type = CMD_RUN;
            else if (strcmp(cmd, "status") == 0) m.type = CMD_STATUS;
            else if (strcmp(cmd, "reset")  == 0) m.type = CMD_RESET;
            else { m.type = CMD_QUIT; quitting = 1; }

        } else {
            fprintf(stderr, "unknown command '%s' (type 'help')\n", cmd);
            continue;
        }

        drain_replies(respq);
        if (send_cmd(cmdq, &m) == 0)
            show_replies(respq);

        if (quitting)
            break;
    }

    ipc_close(cmdq);
    ipc_close(respq);
    return 0;
}
