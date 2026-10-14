# OS Process Scheduler Simulator

Three processes (UI, Core, Logger) talking through POSIX message queues. Linux only.

## Quick start (Kali / Debian / Ubuntu / WSL)

    sudo apt update && sudo apt install -y build-essential
    chmod +x run.sh
    make
    ./run.sh --demo --tick 0

You should see FCFS, SJF and RR results. Then look at `simulator.log` and `errors.log`.

## Interactive use

    ./run.sh

Commands at the `sched>` prompt: `add P1 5`, `algo fcfs|sjf|rr`, `quantum 2`,
`step`, `run`, `status`, `reset`, `quit`, `help`.

## Clean up

    make clean-ipc     # remove leftover queues after a crash
    make distclean     # remove build output and logs

