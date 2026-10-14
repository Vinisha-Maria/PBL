/*
 * input.c - Safe user input and random process generation.
 *
 * All numbers are read as a whole line (fgets) and converted with strtol,
 * so typing letters or leaving the line empty never breaks the program.
 */
#include <ctype.h>
#include <errno.h>
#include <string.h>
#include <time.h>

#include "input.h"

/* Read one line from stdin into buf.
 * Returns 1 = OK, 0 = end of input (EOF), 2 = line was too long. */
static int read_line(char *buf, size_t size)
{
    if (fgets(buf, (int)size, stdin) == NULL) {
        return 0;
    }
    if (strchr(buf, '\n') == NULL) {
        /* No newline: either the line was too long, or EOF ended the last line. */
        int c, extra = 0;
        while ((c = getchar()) != '\n' && c != EOF) {
            extra = 1;
        }
        if (extra) {
            return 2;
        }
    }
    return 1;
}

int read_int_in_range(const char *prompt, int min, int max)
{
    char buf[64];

    for (;;) {
        printf("%s", prompt);
        fflush(stdout);

        int status = read_line(buf, sizeof buf);
        if (status == 0) {
            printf("\nInput closed. Exiting.\n");
            exit(0);
        }
        if (status == 2) {
            printf("  Input too long. Please enter a number between %d and %d.\n", min, max);
            continue;
        }

        char *end;
        errno = 0;
        long value = strtol(buf, &end, 10);

        if (end == buf) {                       /* nothing numeric at all */
            printf("  Invalid input. Please enter a number between %d and %d.\n", min, max);
            continue;
        }
        while (isspace((unsigned char)*end)) {  /* allow trailing spaces/newline */
            end++;
        }
        if (*end != '\0') {                     /* leftover junk like "12abc" */
            printf("  Invalid input. Please enter a whole number only.\n");
            continue;
        }
        if (errno == ERANGE || value < min || value > max) {
            printf("  Out of range. Please enter a number between %d and %d.\n", min, max);
            continue;
        }
        return (int)value;
    }
}

/* Set the working fields of a process to their starting values. */
static void init_process_fields(Process *p)
{
    p->remaining = p->burst;
    p->start_time = -1;
    p->completion = 0;
    p->waiting = 0;
    p->turnaround = 0;
    p->response = 0;
}

/* Return 1 if `pid` is already used by one of the first `count` processes. */
static int pid_exists(const Process p[], int count, int pid)
{
    for (int i = 0; i < count; i++) {
        if (p[i].pid == pid) {
            return 1;
        }
    }
    return 0;
}

void read_processes(Process p[], int *n)
{
    int count = read_int_in_range("Enter number of processes (1-100): ", 1, MAX_PROCESSES);

    for (int i = 0; i < count; i++) {
        printf("\n--- Process %d of %d ---\n", i + 1, count);

        int pid;
        for (;;) {
            pid = read_int_in_range("  Process ID (positive, unique): ", 1, MAX_INPUT_VALUE);
            if (!pid_exists(p, i, pid)) {
                break;
            }
            printf("  PID %d is already used. Please choose a different PID.\n", pid);
        }

        p[i].pid      = pid;
        p[i].arrival  = read_int_in_range("  Arrival Time (>= 0): ", 0, MAX_INPUT_VALUE);
        p[i].burst    = read_int_in_range("  Burst Time (> 0): ", 1, MAX_INPUT_VALUE);
        p[i].priority = read_int_in_range("  Priority (>= 0, lower = higher priority): ", 0, MAX_INPUT_VALUE);
        init_process_fields(&p[i]);
    }

    *n = count;
}

int read_quantum(void)
{
    return read_int_in_range("Enter time quantum (> 0): ", 1, MAX_INPUT_VALUE);
}

void generate_random_processes(Process p[], int n)
{
    static int seeded = 0;       /* seed only once so repeated calls differ */
    if (!seeded) {
        srand((unsigned int)time(NULL));
        seeded = 1;
    }

    for (int i = 0; i < n; i++) {
        p[i].pid      = i + 1;
        p[i].arrival  = rand() % 11;        /* 0 - 10 */
        p[i].burst    = 1 + rand() % 10;    /* 1 - 10 */
        p[i].priority = 1 + rand() % 5;     /* 1 - 5  */
        init_process_fields(&p[i]);
    }
}
