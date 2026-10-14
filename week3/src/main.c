/*
 * main.c - CLI menu for the CPU Process Scheduling Simulator.
 *
 * The user enters (or generates) processes once. Every algorithm then runs on
 * a FRESH COPY, so one algorithm never affects the next.
 */
#include <string.h>

#include "common.h"
#include "input.h"

static void print_menu(void)
{
    printf("\n==========================================\n");
    printf("   CPU PROCESS SCHEDULING SIMULATOR\n");
    printf("==========================================\n");
    printf(" 1. Enter processes manually\n");
    printf(" 2. Generate random processes\n");
    printf(" 3. FCFS\n");
    printf(" 4. SJF (Non-preemptive)\n");
    printf(" 5. SJF (Preemptive / SRTF)\n");
    printf(" 6. Priority (Non-preemptive)\n");
    printf(" 7. Priority (Preemptive)\n");
    printf(" 8. Round Robin\n");
    printf(" 9. Compare all algorithms\n");
    printf(" 0. Exit\n");
    printf("------------------------------------------\n");
}

/* Show the input data the user entered / we generated. */
static void show_input_table(const Process p[], int n)
{
    printf("\nCurrent processes:\n");
    printf("  PID   Arrival   Burst   Priority\n");
    printf("  ---   -------   -----   --------\n");
    for (int i = 0; i < n; i++) {
        printf("  %3d   %7d   %5d   %8d\n", p[i].pid, p[i].arrival, p[i].burst, p[i].priority);
    }
}

/* Copy `original` into `work` and reset every working field. */
static void make_fresh_copy(Process work[], const Process original[], int n)
{
    memcpy(work, original, (size_t)n * sizeof(Process));
    for (int i = 0; i < n; i++) {
        work[i].remaining = work[i].burst;
        work[i].start_time = -1;
        work[i].completion = -1;
        work[i].waiting = 0;
        work[i].turnaround = 0;
        work[i].response = 0;
    }
}

/* Run one algorithm (menu choice 3..8) and print all results. */
static void run_algorithm(int choice, const Process original[], int n)
{
    Process work[MAX_PROCESSES];
    Gantt g;
    Metrics m;
    int quantum = 0;
    const char *name = "";

    if (choice == 8) {
        quantum = read_quantum();      /* ask BEFORE printing results */
    }

    make_fresh_copy(work, original, n);
    g.count = 0;
    g.overflow = 0;
    memset(&m, 0, sizeof m);

    switch (choice) {
    case 3: name = "FCFS";                        fcfs(work, n, &g);                 break;
    case 4: name = "SJF (Non-preemptive)";        sjf(work, n, &g, 0);               break;
    case 5: name = "SJF (Preemptive / SRTF)";     sjf(work, n, &g, 1);               break;
    case 6: name = "Priority (Non-preemptive)";   priority_sched(work, n, &g, 0);    break;
    case 7: name = "Priority (Preemptive)";       priority_sched(work, n, &g, 1);    break;
    case 8: name = "Round Robin";                 round_robin(work, n, &g, quantum); break;
    default: return;
    }

    printf("\n==========================================\n");
    if (choice == 8) {
        printf(" Algorithm: %s (quantum = %d)\n", name, quantum);
    } else {
        printf(" Algorithm: %s\n", name);
    }
    printf("==========================================\n");

    compute_metrics(work, n, &g, &m);
    print_process_table(work, n);
    print_gantt(&g);
    print_metrics(&m);
}

int main(void)
{
    Process original[MAX_PROCESSES];
    int n = 0;                       /* 0 = no processes entered yet */

    printf("Welcome to the CPU Process Scheduling Simulator!\n");

    for (;;) {
        print_menu();
        int choice = read_int_in_range("Enter your choice (0-9): ", 0, 9);

        if (choice == 0) {
            printf("Goodbye!\n");
            break;
        }

        if (choice == 1) {
            read_processes(original, &n);
            show_input_table(original, n);
        } else if (choice == 2) {
            n = read_int_in_range("How many random processes (1-100)? ", 1, MAX_PROCESSES);
            generate_random_processes(original, n);
            show_input_table(original, n);
        } else {
            /* Choices 3..9 need processes. */
            if (n == 0) {
                printf("\nNo processes yet! Please choose option 1 (enter) or 2 (random) first.\n");
                continue;
            }
            if (choice == 9) {
                Process work[MAX_PROCESSES];
                int quantum = read_quantum();
                make_fresh_copy(work, original, n);
                printf("\n==========================================\n");
                printf(" Comparison of all algorithms (quantum = %d)\n", quantum);
                printf("==========================================\n");
                print_comparison_table(work, n, quantum);
            } else {
                run_algorithm(choice, original, n);
            }
        }
    }

    return 0;
}
