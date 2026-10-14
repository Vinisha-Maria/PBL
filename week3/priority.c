/*
 * priority.c
 *
 * Priority scheduling:
 *   - Non-preemptive: once a process is selected, it runs until completion.
 *   - Preemptive: the scheduler rechecks priorities after every time unit.
 *
 * IMPORTANT: a SMALLER priority number means a HIGHER priority.
 * For example, priority 1 beats priority 5.
 *
 * Tie-breaking rule:
 *   1. Better (smaller) priority number
 *   2. Earlier arrival time
 *   3. Smaller pid
 *
 * Build with: gcc -std=c99 -Wall -Wextra -c priority.c
 */

#include <stddef.h>
#include "common.h"

/*
 * Returns the index of the best ready process at the given time,
 * or -1 if no arrived process still needs the CPU.
 *
 * A process is ready if:
 *   - it has arrived: arrival <= now
 *   - it is unfinished: remaining > 0
 */
static int pick_best_priority(const Process p[], int n, int now)
{
    int best = -1;

    for (int i = 0; i < n; i++) {
        if (p[i].arrival > now || p[i].remaining <= 0) {
            continue;
        }

        if (best == -1) {
            best = i;
            continue;
        }

        /* Smaller priority number wins. */
        if (p[i].priority < p[best].priority) {
            best = i;
        }
        /* Equal priority: earlier arrival wins. */
        else if (p[i].priority == p[best].priority &&
                 p[i].arrival < p[best].arrival) {
            best = i;
        }
        /* Equal priority and arrival: smaller pid wins. */
        else if (p[i].priority == p[best].priority &&
                 p[i].arrival == p[best].arrival &&
                 p[i].pid < p[best].pid) {
            best = i;
        }
    }

    return best;
}

/*
 * Finds the earliest arrival time among unfinished processes.
 * Used to skip idle CPU time.
 */
static int next_arrival_time(const Process p[], int n, int now)
{
    int next = -1;

    for (int i = 0; i < n; i++) {
        if (p[i].remaining <= 0 || p[i].arrival <= now) {
            continue;
        }

        if (next == -1 || p[i].arrival < next) {
            next = p[i].arrival;
        }
    }

    return next;
}

void priority_sched(Process p[], int n, Gantt *g, int preemptive)
{
    /* Reset per-run fields for every process. */
    for (int i = 0; i < n; i++) {
        p[i].remaining = p[i].burst;
        p[i].start_time = -1;
        p[i].completion = -1;
        p[i].waiting = 0;
        p[i].turnaround = 0;
        p[i].response = 0;
    }

    int completed = 0;
    int now = 0;

    while (completed < n) {
        int current = pick_best_priority(p, n, now);

        /* No process has arrived yet: record an idle gap. */
        if (current == -1) {
            int next = next_arrival_time(p, n, now);

            /*
             * If next is still -1, there is an input problem:
             * unfinished processes exist but none can ever arrive.
             * Stop safely rather than looping forever.
             */
            if (next == -1) {
                break;
            }

            add_gantt(g, -1, now, next);
            now = next;
            continue;
        }

        /* First time this process gets the CPU. */
        if (p[current].start_time == -1) {
            p[current].start_time = now;
        }

        if (preemptive == 0) {
            /*
             * Non-preemptive:
             * run the selected process for its entire remaining burst.
             */
            int run_time = p[current].remaining;
            int end = now + run_time;

            add_gantt(g, p[current].pid, now, end);

            now = end;
            p[current].remaining = 0;
            p[current].completion = now;
            completed++;
        } else {
            /*
             * Preemptive:
             * run for exactly one time unit, then choose again.
             * A newly arrived higher-priority process can therefore
             * take the CPU at the next unit.
             */
            add_gantt(g, p[current].pid, now, now + 1);

            now++;
            p[current].remaining--;

            if (p[current].remaining == 0) {
                p[current].completion = now;
                completed++;
            }
        }
    }
}
