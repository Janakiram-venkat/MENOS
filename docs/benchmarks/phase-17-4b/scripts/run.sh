# Phase 17 item 4b gate run $1 (a, b): docs/BUILD_PLAN.md Phase 17 criterion 4.
# Run in the WSL clone (the Phase 16 gate machine), cuda12-release with
# benchmarks. Refuses to start, and marks the run invalid, unless the machine
# is on AC before and after the benchmark (the check of
# docs/benchmarks/phase-16-gate/scripts/gate.sh).
set -e
export PATH="$HOME/.local/bin:$PATH"
cd ~/MENOS
git fetch -q origin && git merge -q --ff-only origin/phase-17-api-deferrals
RUN=$1
OUT=build/phase17-4b/run-$RUN
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
nvidia-smi --query-gpu=name,driver_version,memory.total --format=csv,noheader | tee $OUT/gpu.txt
check_ac start
cmake --preset cuda12-release -DMENOS_BUILD_BENCHMARKS=ON > /dev/null
cmake --build build/cuda12-release -j"$(nproc)" --target menos_cuda_factorized_backend_benchmark > /dev/null

echo "== factorized backend benchmark $(date -u +%T)"
MENOS_FACTORIZED_BACKEND_JSON=$OUT/factorized_backend.json \
    build/cuda12-release/benchmarks/menos_cuda_factorized_backend_benchmark \
    > $OUT/factorized_backend.log 2>&1 || echo "benchmark exit $?"
check_ac end
echo "== done $(date -u +%T)"
