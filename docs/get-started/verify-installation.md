# Verify Installation

This page runs a real circuit through the whole stack: the Qiskit converter, the planner, an engine, and the result adapter. If it runs, installation worked.

## Check the package and its version

```python
import menos
import qiskit

print("menos", menos.__version__)
print("qiskit", qiskit.__version__)
assert isinstance(menos.__version__, str) and menos.__version__
```

MENOS pins Qiskit 2.5.2; a different version usually still imports but is untested.

## Run a circuit

```python
from qiskit import QuantumCircuit

from menos.qiskit import MENOSBackend

qc = QuantumCircuit(2, 2)
qc.h(0)
qc.cx(0, 1)
qc.measure([0, 1], [0, 1])

result = MENOSBackend().run(qc, shots=1000, seed_simulator=7).result()
counts = result.get_counts()
print(counts)

assert result.success
assert set(counts) <= {"00", "11"}
assert sum(counts.values()) == 1000
```

Expected output is two keys, `00` and `11`, with counts that add up to 1000. With `seed_simulator=7` the split is the same every time: `{'00': 507, '11': 493}`.

## Check the native layer

The Python package wraps a compiled extension, `menos._core`. If it failed to build or load, `import menos` raises `ImportError`. You can confirm it directly:

```python
from menos import Circuit, Opcode, RunOptions, run

circuit = Circuit(1, 1)
circuit.append(Opcode.X, [0])
circuit.append_measure(0, 0)

options = RunOptions()
options.shots = 10
options.seed = 1
result = run(circuit, options)

print(result.counts)
assert result.counts == {"0x1": 10}
```

Notice the key: the native API returns counts keyed by **hexadecimal strings** (`0x1` means classical bit 0 is set), while `MENOSBackend` converts to Qiskit's binary strings. See [Results](/build/results).

## Run the project's own checks

From the repository root:

```bash
.venv/bin/python -m pytest python/tests --deselect python/tests/test_wheel_smoke.py
```

This runs the Python suite, which includes every script under `examples/`.

## Did it use a GPU?

A default install never does. To check a CUDA-enabled build, see [CPU vs CUDA](/gpu/cpu-vs-cuda).

## If something failed

| Symptom | Go to |
|---|---|
| `ModuleNotFoundError: No module named 'menos'` | [Installation Problems](/troubleshooting/installation) |
| `ImportError` mentioning `_core` | [Installation Problems](/troubleshooting/installation) |
| `ModuleNotFoundError: No module named 'qiskit'` | Install the `qiskit` extra, see [Installation](/get-started/installation) |
