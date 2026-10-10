"""An expectation value by Pauli propagation: no statevector is ever built."""

from qiskit import QuantumCircuit
from qiskit.quantum_info import SparsePauliOp, Statevector

from menos import Circuit, Opcode, RunOptions, run
from menos._core import Observable, PauliString

n = 10
native = Circuit(n, 0)
reference = QuantumCircuit(n)
for layer in range(3):
    for q in range(n):
        native.append(Opcode.H, [q])
        reference.h(q)
    native.append(Opcode.RZ, [0], [0.37])
    reference.rz(0.37, 0)
    for q in range(layer % 2, n - 1, 2):
        native.append(Opcode.CX, [q, q + 1])
        reference.cx(q, q + 1)

z0 = PauliString()
z0.num_qubits = n
z0.x_words = [0]
z0.z_words = [1]  # Z on qubit 0
observable = Observable()
observable.terms = [(z0, 1.0 + 0.0j)]

options = RunOptions()
options.expectation = observable
result = run(native, options)

report = result.explain()
print("\n".join(line for line in report.splitlines() if line.startswith(("candidate", "strategy"))))
print("expectation:", result.expectation_value.real)
print("bytes used:", result.actual_state_bytes)

expected = Statevector(reference).expectation_value(SparsePauliOp("I" * (n - 1) + "Z")).real
assert "strategy   direct_pauli" in report
assert abs(result.expectation_value.real - expected) < 1e-12
