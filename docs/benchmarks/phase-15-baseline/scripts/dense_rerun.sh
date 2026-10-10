# Re-runs the dense benchmark twice (runs e and f) with every repetition
# kept, guarded on AC power, and copies the output into the repository.
set -e
cd ~/MENOS
git fetch -q origin && git merge -q --ff-only origin/phase-15-dense-cpu-performance
power() {
    .venv/bin/python -c "
import importlib.util, json
spec = importlib.util.spec_from_file_location('d', 'tools/dashboard/menos_dashboard.py')
d = importlib.util.module_from_spec(spec); spec.loader.exec_module(d)
print(json.dumps(d.power_state()))"
}
cmake --build build/bench -j"$(nproc)" --target menos_dense_gate_application_benchmark > /dev/null
D=/mnt/c/Users/janak/MENOS/docs/benchmarks/phase-15-baseline
for RUN in e f; do
    OUT=build/phase15-baseline/dense-$RUN
    rm -rf $OUT && mkdir -p $OUT
    git log --oneline -1 > $OUT/commit.txt
    git status --short --untracked-files=no >> $OUT/commit.txt
    for when in start end; do
        p=$(power)
        echo "$(date -u +%T) $when power: $p" | tee -a $OUT/power.log
        case "$p" in *'"ac_online": true'*) ;; *) echo "NOT ON AC: dense run $RUN is invalid"; exit 3;; esac
        if [ $when = start ]; then
            build/bench/benchmarks/menos_dense_gate_application_benchmark --benchmark_out=$OUT/dense.json --benchmark_out_format=json > $OUT/dense.log 2>&1
        fi
    done
    mkdir -p $D/dense-$RUN
    cp $OUT/dense.json $OUT/dense.log $OUT/power.log $OUT/commit.txt $D/dense-$RUN/
done
python3 - <<'PY'
import json, statistics
def runs(r):
    out = {}
    for b in json.load(open(f"build/phase15-baseline/dense-{r}/dense.json"))["benchmarks"]:
        if b.get("run_type") == "iteration":
            out.setdefault(b["run_name"].replace("/repeats:10", ""), []).append(b["real_time"])
    return out
e, f = runs("e"), runs("f")
print("| benchmark | E median ms (min-max) | F median ms (min-max) | F vs E | E cv | F cv |")
print("|---|---|---|---|---|---|")
cv = lambda x: 100 * statistics.stdev(x) / statistics.mean(x)
for k in e:
    a, b = e[k], f[k]
    assert len(a) == 10 and len(b) == 10, k
    print(f"| {k} | {statistics.median(a):.3g} ({min(a):.3g}-{max(a):.3g}) | {statistics.median(b):.3g} ({min(b):.3g}-{max(b):.3g}) | {100*(statistics.median(b)/statistics.median(a)-1):+.1f}% | {cv(a):.1f}% | {cv(b):.1f}% |")
PY
