#ifndef SCHEDULER_H
#define SCHEDULER_H

#define MAX_PROCESSES 100
#define MAX_GANTT 1000

typedef struct {
    int pid;
    int arrival;
    int burst;
    int priority;
    int remaining;
    int start_time;
    int completion;
    int waiting;
    int turnaround;
    int response;
} Process;

typedef struct {
    int pid;   /* -1 means the CPU was idle */
    int start;
    int end;
} GanttEntry;

typedef struct {
    GanttEntry entries[MAX_GANTT];
    int count;
} Gantt;

typedef struct {
    float avg_wt;
    float avg_tat;
    float avg_rt;
    float cpu_util;
    int total_time;
} Metrics;

/* Provided by a teammate: merges consecutive slices having the same pid. */
void add_gantt(Gantt *g, int pid, int start, int end);

/* Lower priority number means higher priority. */
void priority_sched(Process p[], int n, Gantt *g, int preemptive);

void round_robin(Process p[], int n, Gantt *g, int quantum);

#endif
