# SparsePauliOp

`qiskit.quantum_info.SparsePauliOp` is how you give `MENOSEstimator` an observable. This page covers how labels map to qubits and how terms are evaluated.

## Labels

A label is read **right to left**: the rightmost character acts on qubit 0.

```python
from qiskit import QuantumCircuit
from qiskit.quantum_info import SparsePauliOp

from menos.qiskit import MENOSEstimator

qc = QuantumCircuit(3)
qc.x(0)                      # qubit 0 is |1>, qubits 1 and 2 are |0>

estimator = MENOSEstimator()
z_on_qubit_0 = SparsePauliOp("IIZ")   # rightmost character = qubit 0
z_on_qubit_2 = SparsePauliOp("ZII")

on_0 = float(estimator.run([(qc, z_on_qubit_0)]).result()[0].data.evs)
on_2 = float(estimator.run([(qc, z_on_qubit_2)]).result()[0].data.evs)
print(on_0, on_2)
assert on_0 == -1.0      # qubit 0 is |1>
assert on_2 == +1.0      # qubit 2 is |0>
```

## Many terms

A `SparsePauliOp` is a weighted sum, and all its terms are evaluated in one run:

```python
from qiskit import QuantumCircuit
from qiskit.quantum_info import SparsePauliOp, Statevector

from menos.qiskit import MENOSEstimator

qc = QuantumCircuit(3)
qc.h(0)
qc.cx(0, 1)
qc.cx(1, 2)

hamiltonian = SparsePauliOp.from_list([("ZZI", 1.0), ("IZZ", 1.0), ("XXX", 0.5), ("IIZ", -0.25)])
value = float(MENOSEstimator().run([(qc, hamiltonian)]).result()[0].data.evs)
assert abs(value - Statevector(qc).expectation_value(hamiltonian).real) < 1e-12
```

## Coefficients

Coefficients may be complex. The estimator returns the real part of the sum, as Qiskit's `StatevectorEstimator` does. `Y` terms carry their `i` phase inside the coefficient, which is what MENOS's bit convention expects; no action is needed from you.

## Arrays of observables

An array of observables is evaluated element by element, and the result has the same shape:

```python
import numpy as np
from qiskit import QuantumCircuit
from qiskit.quantum_info import SparsePauliOp

from menos.qiskit import MENOSEstimator

qc = QuantumCircuit(2)
qc.h(0)
qc.cx(0, 1)

# Use nested Python lists: np.array() would try to iterate each SparsePauliOp.
observables = [[SparsePauliOp("ZZ"), SparsePauliOp("XX")], [SparsePauliOp("YY"), SparsePauliOp("ZI")]]
evs = MENOSEstimator().run([(qc, observables)]).result()[0].data.evs
print(evs)
assert evs.shape == (2, 2)
assert np.allclose(evs, [[1.0, 1.0], [-1.0, 0.0]])
```

Each element is a separate planned run, so different elements can use different engines.

## Not supported

- Observables that are not Pauli sums.
- An observable whose terms are all the identity ([details](/observables/pauli-observables#an-all-identity-observable-is-not-supported)).
- An empty observable (Qiskit itself rejects a zero-coefficient `SparsePauliOp` before MENOS sees it).

## Related

[Pauli Observables](/observables/pauli-observables) · [EstimatorV2](/build/estimator) · [sparse_pauli_op example](/examples/sparse-pauli-op)
