# Phase 16 gate run $1 (a, b, ...): docs/BUILD_PLAN.md, Phase 16 gate.
# Run in the WSL clone (the gate machine, owner Q1), cuda12-release with
# benchmarks. Refuses to start, and marks the run invalid, unless the machine
# is on AC before and after every tool (the check of
# docs/benchmarks/phase-16-baseline/scripts/baseline.sh).
#
# The kernel benchmark gives criteria 1, 2 and 4 (same-run ratios against the
# generic kernel, and the bitwise flag); the crossover benchmark gives the
# recalibration figures and the forced CPU/CUDA crossover (criterion 6).
set -e
export PATH="$HOME/.local/bin:$PATH"
cd ~/MENOS
git fetch -q origin && git merge -q --ff-only origin/phase-16-specialised-cuda-kernels
RUN=$1
OUT=build/phase16-gate/run-$RUN
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
cmake --build build/cuda12-release -j"$(nproc)" \
    --target menos_cuda_crossover_benchmark menos_cuda_kernel_benchmark > /dev/null
BIN=build/cuda12-release/benchmarks

echo "== kernel benchmark $(date -u +%T)"
MENOS_CUDA_KERNEL_JSON=$OUT/kernel.json \
    $BIN/menos_cuda_kernel_benchmark > $OUT/kernel.log 2>&1 || echo "kernel benchmark exit $?"
check_ac after-kernel

echo "== crossover, 10 repeats $(date -u +%T)"
MENOS_CROSSOVER_REPEATS=10 MENOS_CROSSOVER_JSON=$OUT/crossover.json \
    $BIN/menos_cuda_crossover_benchmark > $OUT/crossover.log 2>&1 || echo "crossover exit $?"
check_ac end
echo "== done $(date -u +%T)"
