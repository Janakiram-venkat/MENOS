"""Phase 15 step 5 measurement (docs/adr/0019 section 7): how the planner's
time estimate for the dense CPU plan compares with the measured execution time
once gate application is threaded. A huge time budget makes explain() print
est_seconds without constraining anything. Run with the main clone's .venv."""

import importlib.util
import re
import statistics
import sys

from menos._core import RunOptions
from menos._core import run as menos_run
from menos.qiskit.convert import convert_circuit

spec = importlib.util.spec_from_file_location("d", "tools/dashboard/menos_dashboard.py")
d = importlib.util.module_from_spec(spec)
spec.loader.exec_module(d)

print("threads  n   est_s        actual_s(median of 3)  est/actual")
for threads in (None, 1):
    for n in (12, 14, 16, 18, 20, 22, 24):
        circuit = convert_circuit(d.build_circuit(n))
        options = RunOptions()
        options.want_statevector = True
        options.seed = 1
        options.shots = 0
        options.time_budget_seconds = 1.0e6
        options.max_threads = threads
        actual = []
        est = None
        for _ in range(3):
            result = menos_run(circuit, options)
            actual.append(result.execute_seconds)
            winner = [line for line in result.explain().splitlines() if "winner=true" in line]
            match = re.search(r"est_seconds=([0-9.eE+-]+)", winner[0]) if winner else None
            est = float(match.group(1)) if match else None
        median = statistics.median(actual)
        label = "default" if threads is None else str(threads)
        ratio = f"{est / median:8.2f}" if est else "     n/a"
        print(f"{label:>7} {n:2d}  {est!s:12.12} {median:12.6f}            {ratio}")
        sys.stdout.flush()
