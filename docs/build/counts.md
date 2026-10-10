# Counts

Counts are the measurement histogram: how many shots produced each bitstring.

## Reading counts

```python
from qiskit import QuantumCircuit

from menos.qiskit import MENOSBackend

qc = QuantumCircuit(3, 3)
qc.x(0)
qc.x(2)
qc.measure([0, 1, 2], [0, 1, 2])

counts = MENOSBackend().run(qc, shots=50, seed_simulator=1).result().get_counts()
print(counts)
assert counts == {"101": 50}
```

The key is written with classical bit 2 leftmost and classical bit 0 rightmost, which is Qiskit's convention. Here qubits 0 and 2 are set, so the key is `101`.

## Seeds and reproducibility

The same circuit, shots and `seed_simulator` give identical counts on every run. Different seeds give different samples from the same distribution. See the [counts example](/examples/counts) and [Determinism](/concepts/determinism).

## Partial measurement

Keys are as wide as the circuit's classical register, and a classical bit that is never written stays `0` (see [Circuits and Gates](/build/circuits-and-gates)). If the register has one classical bit, the keys are one character wide:

```python
from qiskit import QuantumCircuit

from menos.qiskit import MENOSBackend

qc = QuantumCircuit(3, 1)
qc.x(2)
qc.measure(2, 0)
counts = MENOSBackend().run(qc, shots=100, seed_simulator=1).result().get_counts()
assert counts == {"1": 100}
```

Other qubits still take part in the simulation if they influence the measured one. If they cannot, [light-cone reduction](/observables/light-cone-reduction) removes them before planning.

## Native counts

The native API returns counts as a dictionary keyed by **hexadecimal** strings, because classical registers may be arbitrarily wide:

```python
from menos import Circuit, Opcode, RunOptions, run

circuit = Circuit(3, 3)
circuit.append(Opcode.X, [0])
circuit.append(Opcode.X, [2])
for q in range(3):
    circuit.append_measure(q, q)

options = RunOptions()
options.shots = 10
options.seed = 1
counts = run(circuit, options).counts
print(counts)
assert counts == {"0x5": 10}   # 0b101
```

`MENOSBackend` converts these to Qiskit's binary keys for you.

## How counts are sampled

Counts are drawn from the exact outcome distribution. The dense engine draws from the final statevector; the stabilizer engine draws from the affine space of reachable outcomes; the MPS engine draws one shot at a time by walking its tensor chain. On a GPU, sampling still happens on the host from the copied-back state, which is why counts are identical between CPU and CUDA for a given seed.

## Cost

Sampling is part of a run's cost. For the MPS engine it can be the larger part: in one measured case sampling took 69% of the run's wall time. See [Monitoring](/runtime/monitoring).
