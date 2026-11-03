// fcfs.c -- First Come First Served scheduling (non-preemptive)
//
// Idea: the process that arrives first runs first, and keeps the CPU
// until it finishes. Ties in arrival time are broken by lower pid.
//
// We do NOT reorder the caller's array. Instead we build a small array
// "order[]" holding the INDICES of the processes in FCFS order, and sort
// that. The caller's array p[] stays exactly as it was (only the
// scheduling fields are filled in).

#include "common.h"

void fcfs(Process p[], int n, Gantt *g)
{
    int order[MAX_PROCESSES];
    int i, j;
    int time = 0; // current simulated clock

    if (n > MAX_PROCESSES) {
        n = MAX_PROCESSES; // safety: never overflow order[]
    }

    // Step 1: initialise the working fields for every process.
    for (i = 0; i < n; i++) {
        p[i].remaining = p[i].burst;
        p[i].start_time = -1;
        p[i].completion = -1;
        order[i] = i; // initially in input order
    }

    // Step 2: insertion sort of the indices.
    // Sort key: arrival time first, then pid (both ascending).
    for (i = 1; i < n; i++) {
        int key = order[i];
        j = i - 1;
        while (j >= 0 &&
               (p[order[j]].arrival > p[key].arrival ||
                (p[order[j]].arrival == p[key].arrival &&
                 p[order[j]].pid > p[key].pid))) {
            order[j + 1] = order[j]; // shift bigger element right
            j--;
        }
        order[j + 1] = key;
    }

    // Step 3: run the processes one after another.
    for (i = 0; i < n; i++) {
        Process *cur = &p[order[i]];

        // CPU idle: the next process has not arrived yet.
        if (time < cur->arrival) {
            add_gantt(g, -1, time, cur->arrival);
            time = cur->arrival;
        }

        cur->start_time = time;       // first (and only) time on CPU
        add_gantt(g, cur->pid, time, time + cur->burst);
        time += cur->burst;           // run to completion
        cur->completion = time;
        cur->remaining = 0;
    }
}
