# Circuits and Gates

MENOS takes circuits from Qiskit's `QuantumCircuit`, or from the small native `menos.Circuit` ([Native API](/build/native-api)). This page explains what a circuit may contain.

## Gates

The gates MENOS runs natively are listed in [Supported Gates](/reference/supported-gates): the Pauli gates, Hadamard, the S and T families, the single-qubit rotations `rx`, `ry`, `rz`, `p` and `u`, and `cx`, `cz`, `swap`, `cp` and `ccx`.

A gate that is **not** in that list still runs if Qiskit can give it a matrix and it acts on at most **five qubits**. It is lowered to a dense unitary. For example Qiskit's `cswap` is not native but runs:

```python
from qiskit import QuantumCircuit

from menos.qiskit import MENOSBackend

qc = QuantumCircuit(3, 3)
qc.x(0)
qc.x(1)
qc.cswap(0, 1, 2)   # swaps qubits 1 and 2 because qubit 0 is 1
qc.measure([0, 1, 2], [0, 1, 2])

counts = MENOSBackend().run(qc, shots=10, seed_simulator=1).result().get_counts()
print(counts)
assert counts == {"101": 10}
```

A gate that is neither native nor expressible as a matrix on five or fewer qubits is rejected. Use `transpile(qc, backend)` to lower it into the native set; see [Qiskit Integration](/get-started/qiskit-integration).

::: warning Why the five-qubit limit matters
A unitary is a `2ᵏ × 2ᵏ` matrix and the limit keeps that bounded. The same limit (`kMaxUnitaryQubits = 5`) applies to the native API and to the CUDA kernel. Native gates have no such limit beyond their own arity.
:::

## Measurements

Measurements must be **terminal**: once a qubit is measured, no later operation may act on it. A circuit that measures a qubit and then applies a gate to it is rejected with `UnsupportedFeatureError`.

```text
operation 1: qubit 0 has a non-terminal measurement (an operation follows a measurement of this qubit)
```

Measuring several qubits into one classical bit is allowed; the last measurement written to a classical bit wins. A classical register can be any width: counts are keyed by an arbitrary-width bitstring, with no 64-bit ceiling.

## Classical bits and counts

A counts key is as wide as the circuit's classical register: one character per classical bit, highest bit on the left. A classical bit that no measurement writes stays `0`.

```python
from qiskit import QuantumCircuit

from menos.qiskit import MENOSBackend

qc = QuantumCircuit(3, 3)
qc.x(1)
qc.measure(1, 2)   # qubit 1 -> classical bit 2; bits 0 and 1 are never written

counts = MENOSBackend().run(qc, shots=5, seed_simulator=1).result().get_counts()
print(counts)
assert counts == {"100": 5}
```

To get short keys, declare a short classical register (`QuantumCircuit(3, 1)` gives one-character keys).

## Global phase

A circuit's `global_phase` is honoured and part of the statevector MENOS returns. It cannot change counts or expectation values, so it only matters for [statevectors](/build/statevectors). See [Conventions](/concepts/conventions).

## Dropped instructions

`barrier`, `id` and `delay` are dropped during conversion because they cannot change any output. They do not raise. A noise error attached to `id` is still applied: it is a separate instruction (see [Noise Models](/build/noise-models)).

## Not supported

| Not supported | What happens |
|---|---|
| `reset` | `UnsupportedFeatureError` |
| Classical control (`if_test`, conditional gates) | `UnsupportedFeatureError` |
| Mid-circuit measurement | `UnsupportedFeatureError` |
| Noise models | Supported for verified noise types; see [Noise Models](/build/noise-models) |
| Unbound parameters | `InvalidCircuitError`; call `assign_parameters` first |

## Size limits

MENOS has no fixed qubit limit. What limits a job is whether some engine can run it inside the [memory budget](/planning/memory-constraints). A dense statevector needs `16 · 2ⁿ` bytes; other engines need far less for the right circuits. See [Simulation Engines](/engines/dense).
