# CPU Process Scheduling Simulator (C)

## Project Description
A command-line simulator that schedules a set of processes (PID, Arrival Time, Burst Time, Priority) using different CPU scheduling algorithms and reports performance metrics.

**Algorithms:** FCFS, SJF (non-preemptive and preemptive/SRTF), Priority (non-preemptive and preemptive), Round Robin (configurable time quantum).

**Output:** Gantt chart, Waiting / Turnaround / Response time per process, average WT / TAT / RT, CPU utilization, and a comparison table.

## How to Compile and Run
```bash
cd week3
make            # builds ./scheduler
./scheduler     # or: make run
make clean      # removes build files
```
Requires `gcc` and `make`. Compiled with `-Wall -Wextra -std=c99`.

## Menu Explanation
| Option | Action |
|---|---|
| 1 | Enter processes manually (validated input) |
| 2 | Generate random processes (arrival 0–10, burst 1–10, priority 1–5) |
| 3 | FCFS |
| 4 | SJF – Non-preemptive |
| 5 | SJF – Preemptive (SRTF) |
| 6 | Priority – Non-preemptive |
| 7 | Priority – Preemptive |
| 8 | Round Robin (asks for time quantum) |
| 9 | Compare all algorithms |
| 0 | Exit |

Lower priority number = higher priority. Every algorithm runs on a fresh copy of the entered processes.

## Project Structure
```
week3/
├── src/        common.h/.c, input.h/.c, main.c, fcfs.c, sjf.c, priority.c, rr.c, metrics.c, gantt.c, compare.c
├── tests/      test inputs and outputs
├── docs/       report
├── Makefile
└── README.md
```

## Sample Input / Output
*(To be filled after integration.)*

```
[Paste sample run here: input processes, Gantt chart, metrics table]
```

## Team Contributions
| Member | Contribution | Files |
|---|---|---|
| Member 1 (Team Lead) | Project setup, shared header, input handling, menu, integration | `common.h`, `common.c`, `input.c/.h`, `main.c`, `Makefile` |
| Student 1 | FCFS, SJF (non-preemptive & preemptive) | `fcfs.c`, `sjf.c` |
| Student 2 | Priority (non-preemptive & preemptive), Round Robin | `priority.c`, `rr.c` |
| Student 3 | Metrics, Gantt chart, comparison table, tests, report | `metrics.c`, `gantt.c`, `compare.c`, `tests/`, `docs/` |
