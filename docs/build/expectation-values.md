# Expectation Values

An expectation value `⟨ψ|O|ψ⟩` of a Pauli observable `O` tells you the average outcome of measuring `O` on the circuit's output state. MENOS computes it **exactly**, without sampling, and for many circuits without ever building the state.

## From Qiskit

Use [`MENOSEstimator`](/build/estimator) with a `SparsePauliOp`:

```python
from qiskit import QuantumCircuit
from qiskit.quantum_info import SparsePauliOp, Statevector

from menos.qiskit import MENOSEstimator

qc = QuantumCircuit(3)
qc.h(0)
qc.cx(0, 1)
qc.ry(0.4, 2)
qc.cx(1, 2)

observable = SparsePauliOp.from_list([("ZZI", 1.0), ("IXX", 0.5), ("YIY", -0.25)])
value = float(MENOSEstimator().run([(qc, observable)]).result()[0].data.evs)
print(value)

assert abs(value - Statevector(qc).expectation_value(observable).real) < 1e-12
```

## From the native API

The native API takes a `menos._core.Observable`: a list of `(PauliString, coefficient)` terms.

```python
from menos import Circuit, Opcode, RunOptions, run
from menos._core import Observable, PauliString

circuit = Circuit(2, 0)
circuit.append(Opcode.H, [0])
circuit.append(Opcode.CX, [0, 1])

zz = PauliString()
zz.num_qubits = 2
zz.x_words = [0b00]      # no X component
zz.z_words = [0b11]      # Z on qubits 0 and 1

observable = Observable()
observable.terms = [(zz, 1.0 + 0.0j)]

options = RunOptions()
options.expectation = observable
result = run(circuit, options)

print(result.expectation_value)
assert abs(result.expectation_value - 1.0) < 1e-12
```

`PauliString` stores the operator as two bit masks: bit `j` of `x_words` set means an X component on qubit `j`, bit `j` of `z_words` set means a Z component. **Both set means Y**, following `Y = i·X·Z`. Masks are lists of 64-bit words, so observables on more than 64 qubits use several words. Assign whole lists (`observable.terms = [...]`); appending to `observable.terms` modifies a copy and has no effect.

## What makes it cheap

An expectation request lets the planner consider engines that cannot produce a statevector at all:

- the **Pauli propagation engine** pushes the observable backward through the circuit and never holds a state;
- the **stabilizer engine** evaluates Pauli expectations exactly on Clifford circuits;
- the **MPS** and **tensor-network** engines contract networks without a full state;
- **cut and recombine** splits a circuit into fragments and recombines their expectation values exactly.

And before any of that, [light-cone reduction](/observables/light-cone-reduction) removes every operation outside the observable's causal past. See [Simulation Engines](/engines/dense).

## Conventions

- **Bit order.** Qubit `j` is bit `j`. In a Qiskit label such as `"ZZI"`, the rightmost character is qubit 0.
- **Hermiticity is not checked.** A non-Hermitian observable has a complex expectation value; the Qiskit estimator returns its real part, exactly as Qiskit's reference `StatevectorEstimator` does.
- **Exact.** The value is computed analytically, with no shot noise.

## Limits

- Observables must be **Pauli sums**. Other operator forms are not accepted.
- Circuits must not carry unbound parameters when using the estimator.
- The native API returns a `complex`; the Qiskit estimator returns a real `float`.
