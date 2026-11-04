# Performance Analysis (template)

Run `make bench` on the final build, on the same machine, with nothing else running. Copy numbers from `results_sim_summary.csv`, `results_overhead.txt` and `results_ipc_summary.csv`.

**Setup:** CPU ______  cores ___  RAM ___  OS ______  gcc ____  ticks per run ______  runs ___

## 1. Standalone vs multi-process (mean +/- standard deviation, 10 runs)

| Metric | Standalone | Multi-process | Difference |
|---|---|---|---|
| Execution time (s) |  |  |  |
| CPU time user + sys (s) |  |  |  |
| CPU usage (%) |  |  |  |
| Peak memory, sum of all processes (KB) |  |  |  |
| IPC overhead per tick (us) | - |  |  |

## 2. IPC mechanisms (64-byte messages)

| Technique | Used for | Avg one-way latency (us) | p99 latency (us) | Throughput (msg/s) |
|---|---|---|---|---|
| POSIX message queue | UI -> Core commands |  |  |  |
| Shared memory + semaphore | Core -> UI state |  |  |  |
| Named pipe (FIFO) | Core -> Logger logs |  |  |  |
| Unix domain socket | Launcher control |  |  |  |

## 3. Analysis (write 1-2 sentences under each heading, using your numbers)

- **What multi-process costs:** extra memory (each process has its own runtime and buffers), extra CPU for system calls and context switches, extra latency per message, more complex start-up, shutdown and error handling.
- **What it gives us:** isolation (a Logger or UI crash does not corrupt the Core), independent development and testing, parallelism on several cores, clear interfaces.
- **Which technique was fastest / slowest and why:**
- **Does each technique fit its job?** (commands are small and rare; state is read often; logs are a one-way stream; control must detect death)
- **Bottleneck found:**
- **Conclusion (is the multi-process design worth it for this simulator?):**

## README outline for the GitHub update

1. Project title and one-paragraph summary
2. Team and roles (UI: Rian, Core: Varun, Logging: Rajath, Integration: Mia)
3. Architecture: IPC diagram (from `ARCHITECTURE.md`) and the table of channels
4. IPC mechanisms and justification (one short paragraph per member)
5. Shared interfaces (`sim_types.h`, control protocol)
6. Build and run (`make`, `make run`, `./sim_launcher --cleanup`)
7. Testing: `make test`, what T1-T5 check, members' IPC test cases
8. Benchmark: how to run (`make bench`), results tables, analysis
9. Troubleshooting (leftover IPC objects, `./sim_launcher --cleanup`, missing `-lrt`)
10. Repository layout and contributors
