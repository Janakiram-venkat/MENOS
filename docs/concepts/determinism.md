# Determinism

MENOS is deterministic by design: **the same circuit, options and seed produce the same counts and the same plan.**

## What is guaranteed

| Quantity | Deterministic? |
|---|---|
| Counts, for a given seed | **Yes**, bit for bit |
| The chosen plan (strategy and engines) | **Yes**, because it depends only on the circuit and the request |
| Statevector and expectation value | **Yes** (exact computations) |
| Counts on CPU versus CUDA, same seed | **Identical**, by construction |
| Wall-clock fields (`execute_s`, `sample_s`) | No: they are measurements |
| Backend choice and time-budget feasibility | Depend on calibrated speed, so on the machine |

The one place a plan can differ between machines is the choice of CPU or CUDA, and the time-budget check, because both compare *measured* seconds. The choice of algorithm never depends on timing.

## Seeds

```python
from qiskit import QuantumCircuit

from menos.qiskit import MENOSBackend

qc = QuantumCircuit(2, 2)
qc.h(0)
qc.h(1)
qc.measure([0, 1], [0, 1])

backend = MENOSBackend()
first = backend.run(qc, shots=300, seed_simulator=2024).result().get_counts()
second = backend.run(qc, shots=300, seed_simulator=2024).result().get_counts()
different = backend.run(qc, shots=300, seed_simulator=2025).result().get_counts()
assert first == second
assert first != different
```

| Interface | Default seed |
|---|---|
| `MENOSBackend.run` | A fresh random seed per call, recorded in `results[0].seed_simulator` |
| Native `RunOptions.seed` | `0` |

If you want reproducibility through `MENOSBackend`, pass `seed_simulator` explicitly.

## How it is achieved

- One seeded pseudo-random generator is used by every engine, rather than the standard library's distributions, whose output can differ between implementations.
- Sub-streams for separate regions come from a fixed mixing of the job seed.
- Device kernels are built so that statevectors are bitwise identical to the CPU's, and counts are sampled on the host from that state.
- A replanned run is identical to running its final plan from a cold start.

## A scope statement

The guarantee is the design invariant; what has been **verified** is Linux on x86-64. Identical results on other platforms are intended and untested. See [Supported Platforms](/introduction/supported-platforms).

## Related

[Counts](/build/counts) · [CPU vs CUDA](/gpu/cpu-vs-cuda) · [Replanning](/runtime/replanning)
