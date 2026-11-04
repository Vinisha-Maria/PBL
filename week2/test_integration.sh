#!/bin/bash
# test_integration.sh - system tests for the 3-process simulator.
#   ./test_integration.sh            real processes (./logger_process ./core_process ./ui_process)
#   MODE=mock ./test_integration.sh  mock processes in ./mock (control-plane tests only)
# Tests: T1 full flow, T2 wrong start order, T3 process crash, T4 restart, T5 clean shutdown.

MODE=${MODE:-real}
BIN=.; [ "$MODE" = mock ] && BIN=./mock
LOGDIR=${LOGDIR:-.}                 # where the Logger writes sim_execution.log / sim_error.log
LOG=/tmp/launcher_test.log
PASS=0; FAIL=0

ok()   { echo "  [PASS] $1"; PASS=$((PASS+1)); }
bad()  { echo "  [FAIL] $1"; FAIL=$((FAIL+1)); }
check(){ if eval "$2"; then ok "$1"; else bad "$1"; fi; }

leftovers() {
  local l=""
  [ -e /dev/shm/sim_state ]          && l="$l shm"
  [ -e /dev/shm/sem.sim_state_lock ] && l="$l semaphore"
  [ -e /dev/mqueue/sim_cmd ]         && l="$l msgqueue"
  [ -e /tmp/sim_log_fifo ]           && l="$l fifo"
  [ -e /tmp/sim_ctrl.sock ]          && l="$l socket"
  echo "$l"
}
strays() { pgrep -x -d, 'logger_process|core_process|ui_process' ; }
wait_gone() {  # wait_gone PID SECONDS
  for _ in $(seq $(( $2 * 10 )) ); do kill -0 "$1" 2>/dev/null || return 0; sleep 0.1; done; return 1; }
start_launcher() {  # keeps stdin open so a real UI does not see EOF
  exec 3< <(sleep 90)
  ./sim_launcher --bin-dir "$BIN" <&3 >"$LOG" 2>&1 &
  LPID=$!
}

./sim_launcher --cleanup
echo "== T1 full flow (UI -> Core -> CPU/Memory/Stack/Queue -> Logger)"
if [ "$MODE" = real ]; then
  rm -f "$LOGDIR"/sim_execution.log "$LOGDIR"/sim_error.log
  SIM_BENCH_TICKS=200 timeout 60 ./sim_launcher --bin-dir "$BIN" </dev/null >"$LOG" 2>&1; RC=$?
  check "launcher exit code 0 (headless run of 200 ticks)" "[ $RC -eq 0 ]"
  check "execution log not empty" "[ -s '$LOGDIR/sim_execution.log' ]"
  check "log lines follow timestamp|LEVEL|SOURCE|message" \
        "! grep -Eqv '^[^|]+\\|(INFO|WARN|ERROR)\\|[^|]+\\|.+' '$LOGDIR/sim_execution.log'"
  check "no leftover IPC objects" "[ -z \"\$(leftovers)\" ]"
else
  echo "  (skipped in mock mode)"
fi

echo "== T2 wrong start order (each module started alone, no launcher)"
for p in ui_process core_process logger_process; do
  timeout 5 "$BIN/$p" </dev/null >/dev/null 2>&1; RC=$?
  # 124 = still running when timeout hit (waiting politely); <128 = clean exit with error. >=128 = crashed.
  check "$p alone: no crash/signal (exit $RC)" "[ $RC -eq 124 ] || [ $RC -lt 128 ]"
done
./sim_launcher --cleanup
check "no stray processes" "[ -z \"\$(strays)\" ]"

echo "== T3 process crash (kill -9 core_process)"
start_launcher; sleep 3
CPID=$(pgrep -x core_process | head -1)
check "core_process is running" "[ -n '$CPID' ]"
kill -9 "$CPID" 2>/dev/null
wait_gone $LPID 10; GONE=$?
check "launcher detected crash and stopped within 10 s" "[ $GONE -eq 0 ]"
wait $LPID 2>/dev/null; RC=$?
check "launcher exit code non-zero (crash reported)" "[ $RC -ne 0 ]"
check "no stray processes after crash" "[ -z \"\$(strays)\" ]"
check "no leftover IPC objects after crash" "[ -z \"\$(leftovers)\" ]"

echo "== T4 restart after crash"
start_launcher; sleep 3
check "launcher running again" "kill -0 $LPID 2>/dev/null"
check "all processes READY again" "grep -q 'all processes READY' '$LOG'"

echo "== T5 clean shutdown (Ctrl-C to launcher)"
kill -INT $LPID
wait_gone $LPID 10; GONE=$?
check "launcher stopped within 10 s" "[ $GONE -eq 0 ]"
wait $LPID 2>/dev/null; RC=$?
check "launcher exit code 0" "[ $RC -eq 0 ]"
check "no stray processes" "[ -z \"\$(strays)\" ]"
check "no leftover IPC objects" "[ -z \"\$(leftovers)\" ]"

pkill -f 'sleep 90' 2>/dev/null
echo; echo "RESULT: $PASS passed, $FAIL failed"
[ $FAIL -eq 0 ]
