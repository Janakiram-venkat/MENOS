"""SparsePauliOp observables: several terms, and arrays of observables."""

import numpy as np
from qiskit import QuantumCircuit
from qiskit.quantum_info import SparsePauliOp, Statevector

from menos.qiskit import MENOSEstimator

qc = QuantumCircuit(3)
qc.h(0)
qc.cx(0, 1)
qc.cx(1, 2)
qc.ry(0.6, 2)

# One observable with several terms; "YIY" shows that Y terms are supported.
hamiltonian = SparsePauliOp.from_list([("ZZI", 0.5), ("IZZ", 0.5), ("XXX", 0.25), ("YIY", -0.3)])

# An array of observables is evaluated element by element.
observables = [SparsePauliOp("ZZI"), SparsePauliOp("IZZ"), SparsePauliOp("XXX"), hamiltonian]

estimator = MENOSEstimator()
values = np.asarray(estimator.run([(qc, observables)]).result()[0].data.evs)
print("values:", np.round(values, 6))

reference = Statevector(qc)
for value, observable in zip(values, observables):
    assert abs(value - reference.expectation_value(observable).real) < 1e-12
