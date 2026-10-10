"""Phase 15: end-to-end break-even of threading on the dashboard circuit.
Engine execute seconds, median and range of 7 runs, 1 thread vs default."""

import importlib.util
import statistics

from menos._core import RunOptions
from menos._core import run as menos_run
from menos.qiskit.convert import convert_circuit

spec = importlib.util.spec_from_file_location("d", "tools/dashboard/menos_dashboard.py")
d = importlib.util.module_from_spec(spec)
spec.loader.exec_module(d)

print(" n  gates  one_thread_ms (min-max)        all_threads_ms (min-max)      speedup")
for n in (14, 15, 16, 17, 18, 19):
    qc = d.build_circuit(n)
    circuit = convert_circuit(qc)
    row = {}
    for threads in (1, None):
        options = RunOptions()
        options.want_statevector = True
        options.seed = 1
        options.shots = 0
        options.max_threads = threads
        menos_run(circuit, options)  # warm-up
        times = []
        for _ in range(7):
            times.append(menos_run(circuit, options).execute_seconds * 1e3)
        row[threads] = times
    one, allt = row[1], row[None]
    print(f"{n:2d} {len(qc.data):5d}  {statistics.median(one):9.3f} ({min(one):.3f}-{max(one):.3f})"
          f"   {statistics.median(allt):9.3f} ({min(allt):.3f}-{max(allt):.3f})"
          f"   {statistics.median(one) / statistics.median(allt):6.2f}", flush=True)
