# Phase 15 step-1 profile: where a release 22-qubit dashboard circuit spends
# its time (gate kernel, per-gate matrix generation/allocation, conversion,
# planning, output copy). Flat self-time profile (no call graph: the release
# build has no frame pointers), user space only.
set -e
cd ~/MENOS
OUT=build/phase15-baseline/profile
rm -rf $OUT && mkdir -p $OUT
cat > $OUT/run22.py <<'PY'
import importlib.util, sys, time
spec = importlib.util.spec_from_file_location("d", "tools/dashboard/menos_dashboard.py")
d = importlib.util.module_from_spec(spec); spec.loader.exec_module(d)
n = int(sys.argv[1])
qc = d.build_circuit(n)
t = time.perf_counter()
seconds, result = d._run_menos(qc)
print(f"n={n} gates={len(qc.data)} wall={seconds:.3f}s execute={result.execute_seconds:.3f}s backend={d.planner_backend(result)}")
PY
.venv/bin/python -c "import importlib.util,json; s=importlib.util.spec_from_file_location('d','tools/dashboard/menos_dashboard.py'); d=importlib.util.module_from_spec(s); s.loader.exec_module(d); print(json.dumps(d.power_state()))" | tee $OUT/power.txt
perf record -F 999 -e cpu-clock --all-user -o $OUT/perf.data -- .venv/bin/python $OUT/run22.py 22 2>&1 | tee $OUT/run.txt
perf report -i $OUT/perf.data --no-children --sort dso,symbol --stdio --percent-limit 0.3 2>/dev/null | grep -v "^$" > $OUT/report.txt
perf report -i $OUT/perf.data --no-children --sort dso --stdio 2>/dev/null | grep -v "^#" | grep -v "^$" > $OUT/report_dso.txt
head -40 $OUT/report.txt
echo "--- by object"
head -15 $OUT/report_dso.txt
