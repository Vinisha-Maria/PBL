#!/bin/bash
# bench.sh [RUNS=10] [TICKS=100000]
# Standalone vs multi-process benchmark + IPC micro-benchmark.
# Assumption (agree with the team): the standalone simulator (./sim_standalone, or $SIM_STANDALONE) and the
# three-process simulator (./sim_launcher) both run headless when SIM_BENCH_TICKS=<N> is set: they simulate N
# ticks, then exit with code 0. (UI quits by itself, so the launcher ends.)
RUNS=${1:-10}
TICKS=${2:-100000}
STANDALONE=${SIM_STANDALONE:-./sim_standalone}
export SIM_BENCH_TICKS=$TICKS

for f in bench_runner bench_ipc sim_launcher "$STANDALONE"; do
  [ -x "$f" ] || { echo "missing $f - run 'make' first"; exit 1; }
done

HDR="label,run,wall_s,user_s,sys_s,cpu_pct,peak_rss_kb"
echo "$HDR" > results_sim.csv
./sim_launcher --cleanup
echo "Running standalone x$RUNS ($TICKS ticks) ..."
./bench_runner standalone "$RUNS" "$STANDALONE" >> results_sim.csv
echo "Running multi-process x$RUNS ($TICKS ticks) ..."
./bench_runner multi "$RUNS" ./sim_launcher >> results_sim.csv
./sim_launcher --cleanup

echo "Running IPC micro-benchmark x$RUNS ..."
echo "run,mechanism,avg_oneway_us,min_oneway_us,p99_oneway_us,throughput_msgs_per_s" > results_ipc_raw.csv
for r in $(seq "$RUNS"); do ./bench_ipc 50000 200000 | tail -n +2 | sed "s/^/$r,/" >> results_ipc_raw.csv; done

# mean and sample standard deviation per group (key = column 1 / key col, numeric columns from 'from')
summ() { # summ file keycol from "name1,name2,..."
  awk -F, -v key="$2" -v from="$3" -v names="$4" 'NR==1{nc=split(names,nm,","); next}
    { k=$key; if(!(k in seen)){seen[k]=1; order[++g]=k}
      n[k]++; for(c=0;c<nc;c++){v=$(from+c); s[k,c]+=v; q[k,c]+=v*v} }
    END{ printf "group,n"; for(c=1;c<=nc;c++) printf ",%s_mean,%s_sd", nm[c], nm[c]; print "";
      for(i=1;i<=g;i++){k=order[i]; printf "%s,%d",k,n[k];
        for(c=0;c<nc;c++){m=s[k,c]/n[k]; var=(n[k]>1)?(q[k,c]-n[k]*m*m)/(n[k]-1):0; if(var<0)var=0; printf ",%.4f,%.4f",m,sqrt(var)} print ""} }' "$1"
}
summ results_sim.csv 1 3 "wall_s,user_s,sys_s,cpu_pct,peak_rss_kb" > results_sim_summary.csv
summ results_ipc_raw.csv 2 3 "avg_oneway_us,min_oneway_us,p99_oneway_us,throughput_msgs_per_s" > results_ipc_summary.csv

# approximate IPC overhead per tick = (multi wall - standalone wall) / ticks
awk -F, -v t="$TICKS" 'NR>1 && $1=="standalone"{a=$3} NR>1 && $1=="multi"{b=$3}
  END{printf "\nIPC overhead per tick (approx.) = %.3f microseconds  [(multi-standalone)/%d ticks]\n",(b-a)/t*1e6,t}' results_sim_summary.csv > results_overhead.txt

echo; echo "== Standalone vs multi-process"; column -s, -t results_sim_summary.csv 2>/dev/null || cat results_sim_summary.csv
echo; cat results_overhead.txt
echo; echo "== IPC mechanisms";             column -s, -t results_ipc_summary.csv 2>/dev/null || cat results_ipc_summary.csv
echo; echo "Raw data: results_sim.csv results_ipc_raw.csv   Summaries: results_sim_summary.csv results_ipc_summary.csv results_overhead.txt"
