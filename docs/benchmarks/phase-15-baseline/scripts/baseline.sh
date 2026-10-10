# Phase 15 step-1 baseline, run $1 (a, b, ...). Release builds, current branch.
# Refuses to start, and marks the run invalid, unless the machine is on AC
# before and after every tool.
set -e
export PATH="$HOME/.local/bin:$PATH"
cd ~/MENOS
RUN=$1
OUT=build/phase15-baseline/run-$RUN
power() {
    .venv/bin/python -c "
import importlib.util, json
spec = importlib.util.spec_from_file_location('d', 'tools/dashboard/menos_dashboard.py')
d = importlib.util.module_from_spec(spec); spec.loader.exec_module(d)
print(json.dumps(d.power_state()))"
}
check_ac() {
    local p; p=$(power)
    echo "$(date -u +%T) $1 power: $p" | tee -a $OUT/power.log
    case "$p" in *'"ac_online": true'*) ;; *) echo "NOT ON AC at $1: run $RUN is invalid"; exit 3;; esac
}
rm -rf $OUT && mkdir -p $OUT
git log --oneline -1 | tee $OUT/commit.txt
git status --short --untracked-files=no | tee -a $OUT/commit.txt
check_ac start
cmake --build build/bench -j"$(nproc)" --target menos_dense_gate_application_benchmark > /dev/null
cmake --build build/cuda12-release -j"$(nproc)" --target menos_cuda_crossover_benchmark > /dev/null
echo "== crossover $(date -u +%T)"
MENOS_CROSSOVER_JSON=$OUT/crossover.json build/cuda12-release/benchmarks/menos_cuda_crossover_benchmark > $OUT/crossover.log 2>&1 || echo "crossover exit $?"
check_ac after-crossover
echo "== dashboard $(date -u +%T)"
XO=""; [ -s $OUT/crossover.json ] && XO="--crossover-json $OUT/crossover.json"
.venv/bin/python tools/dashboard/menos_dashboard.py run --min-qubits 4 --max-qubits 24 --step 2 --repeats 10 $XO --out $OUT/results.json
.venv/bin/python tools/dashboard/menos_dashboard.py render --results $OUT/results.json --out $OUT/dashboard.html
check_ac after-dashboard
echo "== dense benchmark $(date -u +%T)"
build/bench/benchmarks/menos_dense_gate_application_benchmark --benchmark_report_aggregates_only=false --benchmark_out=$OUT/dense.json --benchmark_out_format=json > $OUT/dense.log 2>&1
grep -E "_median" $OUT/dense.log
check_ac end
echo "== done $(date -u +%T)"
