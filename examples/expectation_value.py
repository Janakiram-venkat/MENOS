"""An expectation value, computed without materialising the state."""

from qiskit import QuantumCircuit
from qiskit.quantum_info import SparsePauliOp, Statevector

from menos.qiskit import MENOSEstimator

qc = QuantumCircuit(2)
qc.h(0)
qc.cx(0, 1)

observable = SparsePauliOp.from_list([("ZZ", 1.0), ("XX", 0.5)])

estimator = MENOSEstimator()
job = estimator.run([(qc, observable)])
value = float(job.result()[0].data.evs)
print("<ZZ + 0.5 XX> =", value)

# Bell state: <ZZ> = 1 and <XX> = 1, so the value is 1.5.
assert abs(value - 1.5) < 1e-12
assert abs(value - Statevector(qc).expectation_value(observable).real) < 1e-12
