"""Diagnostic for the Phase 18 Windows CI wheel crash (temporary; docs/adr/0022).

Runs the first simulation of python/tests/test_backend_selection.py (a seeded
Bell circuit) in a child process, once as is, once with RunOptions.max_threads
= 1 and once with OPENBLAS_NUM_THREADS=1, and reports each exit code, so a
crash in MENOS's thread pool can be told from one in OpenBLAS's threads.
Usage: python scripts/windows-crash-repro.py
"""

import os
import subprocess
import sys

if len(sys.argv) == 1:
    variants = [
        ("as is", {}, 0),
        ("RunOptions.max_threads = 1", {}, 1),
        ("OPENBLAS_NUM_THREADS=1", {"OPENBLAS_NUM_THREADS": "1"}, 0),
    ]
    for name, extra_env, max_threads in variants:
        result = subprocess.run(
            [sys.executable, "-X", "faulthandler", __file__, "child", str(max_threads)],
            env={**os.environ, **extra_env}, capture_output=True, text=True)
        print(f"{name}: exit {result.returncode & 0xFFFFFFFF:#x}")
        print(result.stdout.strip())
        print(result.stderr.strip()[-3000:])
    sys.exit(0)

from menos._core import Circuit, Opcode, RunOptions, run  # noqa: E402

circuit = Circuit(2, 2)
circuit.append(Opcode.H, [0])
circuit.append(Opcode.CX, [0, 1])
circuit.append_measure(0, 0)
circuit.append_measure(1, 1)
options = RunOptions()
options.shots = 100
options.seed = 1
if int(sys.argv[2]):
    options.max_threads = int(sys.argv[2])
print("  running", flush=True)
result = run(circuit, options)
print("  counts:", dict(result.counts), flush=True)
print(result.explain(), flush=True)
