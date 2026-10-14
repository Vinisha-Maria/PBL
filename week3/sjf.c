// sjf.c -- Shortest Job First scheduling
//   preemptive == 0 : non-preemptive SJF (choose by smallest burst, run to the end)
//   preemptive == 1 : preemptive SJF = SRTF (Shortest Remaining Time First)
//
// Tie-breaking (both versions): smaller key, then earlier arrival, then lower pid.
// Assumption: every burst is at least 1.

#include "common.h"

// Returns the index of the process that should run at time "now",
// or -1 if no unfinished process has arrived yet.
// use_remaining = 0 -> compare burst time      (non-preemptive SJF)
// use_remaining = 1 -> compare remaining time  (SRTF)
static int pick_next(Process p[], int n, int now, int use_remaining)
{
    int best = -1;
    int i;

    for (i = 0; i < n; i++) {
        int key_i, key_best;

        // Skip finished processes and ones that have not arrived yet.
        if (p[i].remaining <= 0 || p[i].arrival > now) {
            continue;
        }
        if (best == -1) {
            best = i;
            continue;
        }

        key_i = use_remaining ? p[i].remaining : p[i].burst;
        key_best = use_remaining ? p[best].remaining : p[best].burst;

        if (key_i < key_best ||
            (key_i == key_best && p[i].arrival < p[best].arrival) ||
            (key_i == key_best && p[i].arrival == p[best].arrival &&
             p[i].pid < p[best].pid)) {
            best = i;
        }
    }
    return best;
}

// Earliest arrival time among unfinished processes (used to skip idle gaps).
// Only called when nothing has arrived, so at least one unfinished process exists.
static int next_arrival(Process p[], int n)
{
    int earliest = -1;
    int i;

    for (i = 0; i < n; i++) {
        if (p[i].remaining > 0 &&
            (earliest == -1 || p[i].arrival < earliest)) {
            earliest = p[i].arrival;
        }
    }
    return earliest;
}

void sjf(Process p[], int n, Gantt *g, int preemptive)
{
    int time = 0;   // simulated clock
    int left = n;   // processes not finished yet
    int i;

    // Initialise working fields.
    for (i = 0; i < n; i++) {
        p[i].remaining = p[i].burst;
        p[i].start_time = -1;
        p[i].completion = 0;
    }

    while (left > 0) {
        int idx = pick_next(p, n, time, preemptive);

        if (idx == -1) {
            // Nothing has arrived: CPU is idle until the next arrival.
            int t = next_arrival(p, n);
            add_gantt(g, -1, time, t);
            time = t;
            continue;
        }

        // First time this process gets the CPU.
        if (p[idx].start_time == -1) {
            p[idx].start_time = time;
        }

        if (preemptive == 0) {
            // Non-preemptive: run the whole burst in one go.
            add_gantt(g, p[idx].pid, time, time + p[idx].remaining);
            time += p[idx].remaining;
            p[idx].remaining = 0;
        } else {
            // SRTF: run exactly one time unit, then choose again.
            add_gantt(g, p[idx].pid, time, time + 1);
            time += 1;
            p[idx].remaining -= 1;
        }

        if (p[idx].remaining == 0) {
            p[idx].completion = time;
            left--;
        }
    }
}
