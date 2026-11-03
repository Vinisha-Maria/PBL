/*
 * common.h - Shared contract for the CPU Process Scheduling Simulator.
 *
 * EVERY team member includes this file. Do NOT rename or change anything here
 * without telling the whole team.
 */
#ifndef COMMON_H
#define COMMON_H

#include <stdio.h>
#include <stdlib.h>

#define MAX_PROCESSES 100
#define MAX_GANTT 1000

typedef struct {
    int pid, arrival, burst, priority;   // input (lower priority number = higher priority)
    int remaining;                       // working copy of burst
    int start_time;                      // first time CPU is given (-1 if not yet)
    int completion;                      // completion time (-1 if not finished)
    int waiting, turnaround, response;   // computed by metrics.c
} Process;

typedef struct { int pid; int start; int end; } GanttEntry;   // pid = -1 means CPU idle
typedef struct { GanttEntry entries[MAX_GANTT]; int count; int overflow; } Gantt;

typedef struct {
    float avg_wt, avg_tat, avg_rt, cpu_util;
    int total_time, busy_time, idle_time;
} Metrics;

// helper (in common.c, by Member 1): appends a slice; merges with previous if same pid
void add_gantt(Gantt *g, int pid, int start, int end);

// schedulers: each must fill p[i].start_time, p[i].completion and the Gantt chart
void fcfs(Process p[], int n, Gantt *g);
void sjf(Process p[], int n, Gantt *g, int preemptive);
void priority_sched(Process p[], int n, Gantt *g, int preemptive);
void round_robin(Process p[], int n, Gantt *g, int quantum);

// metrics & display (Student 3)
void compute_metrics(Process p[], int n, Gantt *g, Metrics *m);
void print_process_table(Process p[], int n);
void print_gantt(Gantt *g);
void print_metrics(Metrics *m);
void print_comparison_table(Process p[], int n, int quantum);

#endif /* COMMON_H */
