/*
 * compare.c - runs every algorithm on the same input and prints one table.
 */
#include <stdio.h>
#include <string.h>
#include "common.h"

#define NUM_ALGOS 6

static const char *ALGO_NAMES[NUM_ALGOS] = {
    "FCFS", "SJF (non-preemptive)", "SJF (preemptive/SRTF)",
    "Priority (non-preemptive)", "Priority (preemptive)", "Round Robin"
};

/* Run algorithm number 'which' on a FRESH copy of the input and return its metrics. */
static void run_one(int which, Process src[], int n, int quantum, Metrics *m)
{
    Process copy[MAX_PROCESSES];
    Gantt g;
    int i;

    memcpy(copy, src, (size_t)n * sizeof(Process));  /* never touch the original */
    for (i = 0; i < n; i++) {                        /* reset what schedulers expect */
        copy[i].remaining  = copy[i].burst;
        copy[i].start_time = -1;
        copy[i].completion = -1;
        copy[i].waiting = copy[i].turnaround = copy[i].response = 0;
    }
    g.count = 0;

    switch (which) {
    case 0: fcfs(copy, n, &g);                  break;
    case 1: sjf(copy, n, &g, 0);                break;
    case 2: sjf(copy, n, &g, 1);                break;
    case 3: priority_sched(copy, n, &g, 0);     break;
    case 4: priority_sched(copy, n, &g, 1);     break;
    default: round_robin(copy, n, &g, quantum); break;
    }

    compute_metrics(copy, n, &g, m);
}

void print_comparison_table(Process p[], int n, int quantum)
{
    Metrics res[NUM_ALGOS];
    float best;
    int i;

    if (n > MAX_PROCESSES) n = MAX_PROCESSES;   /* protect the local array */

    for (i = 0; i < NUM_ALGOS; i++)
        run_one(i, p, n, quantum, &res[i]);

    /* find the lowest average waiting time */
    best = res[0].avg_wt;
    for (i = 1; i < NUM_ALGOS; i++)
        if (res[i].avg_wt < best) best = res[i].avg_wt;

    printf("Algorithm Comparison (Round Robin quantum = %d)\n", quantum);
    printf("%-27s | %-8s | %-8s | %-8s | %-10s\n",
           "Algorithm", "Avg WT", "Avg TAT", "Avg RT", "CPU Util %");
    printf("----------------------------+----------+----------+----------+-----------\n");
    for (i = 0; i < NUM_ALGOS; i++) {
        printf("%-27s | %-8.2f | %-8.2f | %-8.2f | %-10.2f",
               ALGO_NAMES[i], res[i].avg_wt, res[i].avg_tat,
               res[i].avg_rt, res[i].cpu_util);
        /* mark every algorithm tied for the best (within rounding error) */
        if (res[i].avg_wt <= best + 0.0001f)
            printf("  <-- best");
        printf("\n");
    }
}
