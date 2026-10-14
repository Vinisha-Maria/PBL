/*
 * rr.c
 *
 * Round Robin scheduling using a circular-array ready queue.
 *
 * Rules implemented:
 *   - Each process runs for min(quantum, remaining) time units.
 *   - Processes arriving during a slice, including exactly at the slice's
 *     end time, enter the queue BEFORE the preempted process is re-added.
 *   - A process is never placed in the queue twice.
 *   - If no ready process exists, an idle Gantt slice is recorded and time
 *     jumps to the next arrival.
 *   - An invalid quantum (<= 0) defaults to 1.
 *
 * Build with: gcc -std=c99 -Wall -Wextra -c rr.c
 */

#include <stddef.h>
#include "scheduler.h"

/*
 * Adds every process whose arrival time is <= now and which has not
 * already been enqueued. Arrivals are added in pid order so results are
 * deterministic when several processes arrive at the same instant.
 */
static void enqueue_new_arrivals(Process p[], int n,
                                 int queue[], int *front, int *rear,
                                 int *size, int enqueued[], int now)
{
    for (int i = 0; i < n; i++) {
        if (enqueued[i] == 0 && p[i].arrival <= now) {
            queue[*rear] = i;
            *rear = (*rear + 1) % MAX_PROCESSES;
            (*size)++;
            enqueued[i] = 1;
        }
    }
}

void round_robin(Process p[], int n, Gantt *g, int quantum)
{
    /* Validate the quantum. */
    if (quantum <= 0) {
        quantum = 1;
    }

    /* Reset per-run fields for every process. */
    for (int i = 0; i < n; i++) {
        p[i].remaining = p[i].burst;
        p[i].start_time = -1;
        p[i].completion = -1;
        p[i].waiting = 0;
        p[i].turnaround = 0;
        p[i].response = 0;
    }

    int queue[MAX_PROCESSES];
    int front = 0;
    int rear = 0;
    int size = 0;

    /* enqueued[i] prevents the same process from entering the queue twice. */
    int enqueued[MAX_PROCESSES] = {0};

    int completed = 0;
    int now = 0;

    /* Put any process that has already arrived at time 0 into the queue. */
    enqueue_new_arrivals(p, n, queue, &front, &rear, &size, enqueued, now);

    while (completed < n) {
        /*
         * If the ready queue is empty, the CPU must be idle until the
         * next process arrives.
         */
        if (size == 0) {
            int next = -1;

            for (int i = 0; i < n; i++) {
                if (p[i].remaining > 0 && p[i].arrival > now) {
                    if (next == -1 || p[i].arrival < next) {
                        next = p[i].arrival;
                    }
                }
            }

            /*
             * If next remains -1, the remaining processes can never arrive.
             * Stop safely instead of looping forever.
             */
            if (next == -1) {
                break;
            }

            add_gantt(g, -1, now, next);
            now = next;

            enqueue_new_arrivals(p, n, queue, &front, &rear,
                                 &size, enqueued, now);
            continue;
        }

        /* Remove the process at the front of the circular queue. */
        int current = queue[front];
        front = (front + 1) % MAX_PROCESSES;
        size--;

        /* First time on the CPU: record response/start time. */
        if (p[current].start_time == -1) {
            p[current].start_time = now;
        }

        /*
         * Run either the full quantum or only the time left,
         * whichever is smaller.
         */
        int slice = quantum;
        if (p[current].remaining < slice) {
            slice = p[current].remaining;
        }

        add_gantt(g, p[current].pid, now, now + slice);

        now += slice;
        p[current].remaining -= slice;

        /*
         * Enqueue processes that arrived during this slice.
         * The condition arrival <= now also includes a process arriving
         * exactly when this quantum expires.
         *
         * These arrivals must be placed in the queue before the current
         * process is put back.
         */
        enqueue_new_arrivals(p, n, queue, &front, &rear,
                             &size, enqueued, now);

        if (p[current].remaining > 0) {
            /* Preempted process goes to the back of the queue. */
            queue[rear] = current;
            rear = (rear + 1) % MAX_PROCESSES;
            size++;
        } else {
            p[current].completion = now;
            completed++;
        }
    }
}
