# Conventions

Every quantum software stack has to choose bit orders and phase conventions. MENOS chooses the ones Qiskit uses, so results agree with Qiskit's own tools. Each claim below is checked by an executable snippet against Qiskit's reference simulator.

## Qubit order is little-endian

Qubit 0 is the **least significant bit** of a statevector index. Index `i` has qubit `j` equal to bit `j` of `i`.

```python
import numpy as np
from qiskit import QuantumCircuit
from qiskit.quantum_info import Statevector

from menos.qiskit import MENOSBackend

qc = QuantumCircuit(3)
qc.x(0)                      # only qubit 0 is set

state = np.asarray(MENOSBackend().run(qc, shots=0, want_statevector=True).result().get_statevector())
assert int(np.argmax(np.abs(state))) == 1           # index 1 = 0b001
assert np.allclose(state, Statevector(qc).data)
```

## Classical bits and counts keys

A Qiskit counts key is written with the highest classical bit on the **left**:

```python
from qiskit import QuantumCircuit

from menos.qiskit import MENOSBackend

qc = QuantumCircuit(3, 3)
qc.x(0)
qc.measure([0, 1, 2], [0, 1, 2])
counts = MENOSBackend().run(qc, shots=10, seed_simulator=1).result().get_counts()
assert counts == {"001": 10}    # classical bit 0 is the rightmost character
```

The native API keys counts by **hexadecimal** string instead (`"0x1"`), so classical registers can be arbitrarily wide.

## Operand order

For controlled gates the control qubits come first and the target last: `cx(control, target)`, `ccx(control1, control2, target)`, `cp(theta, control, target)`. This matches Qiskit.

## Pauli strings and `Y = i·X·Z`

An observable's Pauli string is two bit masks, `x` and `z`, with bit `j` for qubit `j`. **Y is both bits set**, and its factor of `i` lives in the coefficient, following `Y = i·X·Z`. A Qiskit label is read right to left, so the rightmost character is qubit 0. See [Pauli Observables](/observables/pauli-observables).

## Global phase

A circuit's `global_phase` is part of the state. It multiplies the whole statevector by `e^{i·phase}`, and cannot change counts or expectation values.

`RZ(θ)` and `P(θ)` differ by exactly a global phase, `RZ(θ) = e^{-iθ/2} · P(θ)`, and MENOS preserves that difference in statevectors, as Qiskit does:

```python
import numpy as np
from qiskit import QuantumCircuit
from qiskit.quantum_info import Statevector

from menos.qiskit import MENOSBackend

theta = 0.9
def state(gate):
    qc = QuantumCircuit(1)
    qc.h(0)
    getattr(qc, gate)(theta, 0)
    sv = MENOSBackend().run(qc, shots=0, want_statevector=True).result().get_statevector()
    return np.asarray(sv), Statevector(qc).data

rz, rz_reference = state("rz")
p, p_reference = state("p")
assert np.allclose(rz, rz_reference) and np.allclose(p, p_reference)
assert np.allclose(rz / p, np.exp(-1j * theta / 2))     # they differ by a global phase
```

## Rotation conventions

`rx`, `ry`, `rz`, `p`, `u` and `cp` take Qiskit's parameters and mean what they mean in Qiskit. The differential tests compare every gate against Qiskit's matrices.

## Measurement

Measurements are projective and terminal, in the computational basis. Where several measurements write one classical bit, the **last** write wins.

## Related

[Statevectors](/build/statevectors) · [Counts](/build/counts) · [Supported Gates](/reference/supported-gates)
