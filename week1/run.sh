#!/usr/bin/env bash
# run.sh - start Logger, Core and UI together; UI runs in this terminal.
#
#   ./run.sh                interactive session
#   ./run.sh --demo         scripted demo of FCFS, SJF, RR and the error cases
#   ./run.sh --tick 0       no simulated CPU delay (fast; use for benchmarks)
#   ./run.sh --echo         Logger also prints its lines to this terminal
#
# Core and Logger run in the background; their own messages go to
# run/core.out and run/logger.out so they do not mix with the UI prompt.
# Log files: simulator.log and errors.log (in the current directory).

set -u
cd "$(dirname "$0")" || exit 1

usage() {
    sed -n '2,11p' "$0" | sed 's/^# \{0,1\}//'
}

TICK=""
ECHO=0
DEMO=0

while [ $# -gt 0 ]; do
    case "$1" in
        --tick)
            if [ $# -lt 2 ] || ! [[ "$2" =~ ^[0-9]+$ ]]; then
                echo "run.sh: --tick needs a number of milliseconds" >&2
                exit 2
            fi
            TICK="$2"; shift 2 ;;
        --echo) ECHO=1; shift ;;
        --demo) DEMO=1; shift ;;
        -h|--help) usage; exit 0 ;;
        *) echo "run.sh: unknown option '$1'" >&2; usage >&2; exit 2 ;;
    esac
done

if [ "$(uname -s)" != "Linux" ]; then
    echo "run.sh: POSIX message queues (mq_open) need Linux." >&2
    echo "        On Windows use WSL; on macOS use a Linux VM." >&2
    exit 1
fi

# Build (only recompiles what changed).
make -s all || { echo "run.sh: build failed" >&2; exit 1; }

# Start from a clean slate: stale queues from a crashed run may hold old messages.
make -s clean-ipc

mkdir -p run
[ -n "$TICK" ] && export SCHED_TICK_MS="$TICK"
[ "$ECHO" -eq 1 ] && export SCHED_LOG_ECHO=1

LOGGER_PID=""
CORE_PID=""

cleanup() {
    trap - EXIT INT TERM

    # After `quit` the Core and Logger exit by themselves. Give them a moment,
    # then ask any survivor to stop (they remove the queues on SIGTERM).
    for _ in 1 2 3 4 5 6 7 8 9 10; do
        alive=0
        for pid in $CORE_PID $LOGGER_PID; do
            kill -0 "$pid" 2>/dev/null && alive=1
        done
        [ "$alive" -eq 0 ] && break
        sleep 0.1
    done
    for pid in $CORE_PID $LOGGER_PID; do
        kill -TERM "$pid" 2>/dev/null
    done
    wait 2>/dev/null

    make -s clean-ipc
}
trap cleanup EXIT
trap 'exit 130' INT TERM

# Start order does not matter: every process opens its queues with O_CREAT.
bin/logger 2>run/logger.out &
LOGGER_PID=$!
bin/core 2>run/core.out &
CORE_PID=$!

demo_input() {
cat <<'EOF'
help
add P1 5
add P2 3
add P3 1
algo fcfs
run
reset
add P1 5
add P2 3
add P3 1
algo sjf
run
reset
add P1 5
add P2 3
add P3 1
algo rr
quantum 2
run
status
add P9 -4
algo banana
reset
step
quit
EOF
}

if [ "$DEMO" -eq 1 ]; then
    demo_input | bin/ui
    UI_RC=$?
else
    bin/ui
    UI_RC=$?
fi

echo
echo "Logs:  simulator.log   errors.log"
echo "Other: run/core.out    run/logger.out"
exit "$UI_RC"
