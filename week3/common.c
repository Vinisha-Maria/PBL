/*
 * common.c - Shared helper used by all scheduling algorithms.
 */
#include "common.h"

/*
 * add_gantt: record that process `pid` ran from time `start` to time `end`.
 *   - pid = -1 means the CPU was idle.
 *   - If the previous slice belongs to the same pid and ends exactly where
 *     this one starts, the previous slice is extended (slices are merged).
 *   - Empty or negative slices (end <= start) are ignored.
 *   - If the chart is full (MAX_GANTT), a warning is printed and the slice is dropped.
 */
void add_gantt(Gantt *g, int pid, int start, int end)
{
    if (g == NULL || end <= start) {
        return;
    }

    /* Try to merge with the previous slice. */
    if (g->count > 0) {
        GanttEntry *last = &g->entries[g->count - 1];
        if (last->pid == pid && last->end == start) {
            last->end = end;
            return;
        }
    }

    /* Guard against overflowing the array. */
    if (g->count >= MAX_GANTT) {
        fprintf(stderr, "Warning: Gantt chart is full (%d entries); slice dropped.\n", MAX_GANTT);
        return;
    }

    g->entries[g->count].pid = pid;
    g->entries[g->count].start = start;
    g->entries[g->count].end = end;
    g->count++;
}
