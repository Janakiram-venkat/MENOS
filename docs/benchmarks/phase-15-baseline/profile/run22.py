import importlib.util, sys, time
spec = importlib.util.spec_from_file_location("d", "tools/dashboard/menos_dashboard.py")
d = importlib.util.module_from_spec(spec); spec.loader.exec_module(d)
n = int(sys.argv[1])
qc = d.build_circuit(n)
t = time.perf_counter()
seconds, result = d._run_menos(qc)
print(f"n={n} gates={len(qc.data)} wall={seconds:.3f}s execute={result.execute_seconds:.3f}s backend={d.planner_backend(result)}")
