/*
 * metrics.c - computes per-process and average performance metrics.
 */
#include "common.h"

void compute_metrics(Process p[], int n, Gantt *g, Metrics *m)
{
    float sum_wt = 0.0f, sum_tat = 0.0f, sum_rt = 0.0f;
    int busy = 0;   /* total time the CPU spent running real processes */
    int i;

    /* --- 1. per-process values (written back into p[]) --- */
    for (i = 0; i < n; i++) {
        p[i].turnaround = p[i].completion - p[i].arrival;   /* TAT = CT - AT  */
        p[i].waiting    = p[i].turnaround - p[i].burst;     /* WT  = TAT - BT */
        p[i].response   = p[i].start_time - p[i].arrival;   /* RT  = first CPU - AT */

        sum_wt  += (float)p[i].waiting;
        sum_tat += (float)p[i].turnaround;
        sum_rt  += (float)p[i].response;
    }

    /* --- 2. averages (guard: n == 0 would divide by zero) --- */
    if (n > 0) {
        m->avg_wt  = sum_wt  / (float)n;
        m->avg_tat = sum_tat / (float)n;
        m->avg_rt  = sum_rt  / (float)n;
    } else {
        m->avg_wt = m->avg_tat = m->avg_rt = 0.0f;
    }

    /* --- 3. total time = end of the last Gantt slice --- */
    m->total_time = (g->count > 0) ? g->entries[g->count - 1].end : 0;

    /* --- 4. busy time = slices that are not idle (pid != -1) --- */
    for (i = 0; i < g->count; i++) {
        if (g->entries[i].pid != -1)
            busy += g->entries[i].end - g->entries[i].start;
    }

    /* --- 5. CPU utilisation = busy / total * 100 (guard: total == 0) --- */
    if (m->total_time > 0)
        m->cpu_util = (float)busy / (float)m->total_time * 100.0f;
    else
        m->cpu_util = 0.0f;
}
