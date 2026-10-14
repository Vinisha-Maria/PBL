/*
 * gantt.c - text output: Gantt chart, process table, metrics summary.
 */
#include <stdio.h>
#include <string.h>
#include "scheduler.h"

#define LINE_WIDTH 60    /* start a new chart line after about this many chars */
#define BUF_SIZE   256   /* big enough for one chart line */
#define BAR_WIDTH  20    /* characters inside the utilisation bar */

/* number of characters needed to print v (e.g. 100 -> 3) */
static int num_digits(int v)
{
    int d = 1;
    if (v < 0) { d++; v = -v; }
    while (v >= 10) { v /= 10; d++; }
    return d;
}

/* Finish one chart line: closing '|' on top, last time label below, then print. */
static void flush_line(char *top, char *bot, int col, int end_time)
{
    char tmp[16];
    int len;

    top[col] = '|';
    top[col + 1] = '\0';

    len = snprintf(tmp, sizeof tmp, "%d", end_time);
    memcpy(bot + col, tmp, (size_t)len);
    bot[col + len] = '\0';

    printf("%s\n%s\n", top, bot);
}

/* Fill both line buffers with spaces (start of a new line). */
static void reset_line(char *top, char *bot)
{
    memset(top, ' ', BUF_SIZE);
    memset(bot, ' ', BUF_SIZE);
}

void print_gantt(Gantt *g)
{
    char top[BUF_SIZE], bot[BUF_SIZE];   /* row of boxes, row of time labels */
    char label[16], tmp[16];
    int col = 0;                         /* current column in the line */
    int i;

    if (g->count == 0) {
        printf("(Gantt chart is empty)\n");
        return;
    }

    printf("Gantt Chart:\n");
    reset_line(top, bot);

    for (i = 0; i < g->count; i++) {
        GanttEntry *e = &g->entries[i];
        int len, w, pad, d;

        /* text inside the box */
        if (e->pid == -1) len = snprintf(label, sizeof label, "IDLE");
        else              len = snprintf(label, sizeof label, "P%d", e->pid);

        /* box width = label + 1 space each side, but wide enough for the
           time numbers too, so labels never collide on multi-digit times */
        w = len + 2;
        d = num_digits(e->start);  if (w < d) w = d;
        d = num_digits(e->end);    if (w < d) w = d;

        /* wrap: this box would pass the line limit -> print line, start new */
        if (col > 0 && col + w + 1 > LINE_WIDTH) {
            flush_line(top, bot, col, g->entries[i - 1].end);
            reset_line(top, bot);
            col = 0;
        }

        top[col] = '|';                              /* left border        */
        pad = (w - len) / 2;                         /* centre the label   */
        memcpy(top + col + 1 + pad, label, (size_t)len);

        d = snprintf(tmp, sizeof tmp, "%d", e->start);  /* start time under the border */
        memcpy(bot + col, tmp, (size_t)d);

        col += w + 1;                                /* move past the box  */
    }

    flush_line(top, bot, col, g->entries[g->count - 1].end);
}

void print_process_table(Process p[], int n)
{
    int i;

    printf("%-5s %-5s %-5s %-9s %-5s %-5s %-5s %-5s\n",
           "PID", "AT", "BT", "Priority", "CT", "TAT", "WT", "RT");
    printf("---------------------------------------------------------\n");
    for (i = 0; i < n; i++) {
        printf("%-5d %-5d %-5d %-9d %-5d %-5d %-5d %-5d\n",
               p[i].pid, p[i].arrival, p[i].burst, p[i].priority,
               p[i].completion, p[i].turnaround, p[i].waiting, p[i].response);
    }
}

void print_metrics(Metrics *m)
{
    int filled, i;
    /* idle = total - busy; busy = total * util / 100 (rounded to whole units) */
    int busy = (int)((float)m->total_time * m->cpu_util / 100.0f + 0.5f);
    int idle = m->total_time - busy;

    printf("Average Waiting Time    : %.2f\n", m->avg_wt);
    printf("Average Turnaround Time : %.2f\n", m->avg_tat);
    printf("Average Response Time   : %.2f\n", m->avg_rt);
    printf("Total Time              : %d\n", m->total_time);
    printf("Idle Time               : %d\n", idle);
    printf("CPU Utilization         : %.2f%%\n", m->cpu_util);

    /* text bar: '#' for used part, '-' for the rest */
    filled = (int)(m->cpu_util / 100.0f * BAR_WIDTH + 0.5f);
    if (filled > BAR_WIDTH) filled = BAR_WIDTH;
    printf("[");
    for (i = 0; i < BAR_WIDTH; i++)
        putchar(i < filled ? '#' : '-');
    printf("] %.0f%%\n", m->cpu_util);
}
