# Statevectors

The statevector is the complete quantum state: `2ⁿ` complex amplitudes.

## Requesting one

```python
import numpy as np
from qiskit import QuantumCircuit

from menos.qiskit import MENOSBackend

qc = QuantumCircuit(2)
qc.h(0)
qc.cx(0, 1)

result = MENOSBackend().run(qc, shots=0, want_statevector=True).result()
state = np.asarray(result.get_statevector())
print(state)
assert state.shape == (4,)
assert np.allclose(state, [2**-0.5, 0, 0, 2**-0.5])
```

A circuit with no measurements and `shots=0` is the usual form. A statevector can also be requested alongside counts.

## Conventions

- **Index order.** Amplitude index `i` has qubit `j` equal to bit `j` of `i` (little-endian). Index 1 is qubit 0 set; index 2 is qubit 1 set.
- **Global phase.** Included, exactly as Qiskit's `Statevector` does. The circuit's `global_phase` multiplies the whole state.
- **Exactness.** Computed in double precision without approximation.

See [Conventions](/concepts/conventions).

## Agreement with Qiskit

The [statevector example](/examples/statevector) checks the result against Qiskit's own `Statevector` to about `1e-16`.

## Which engine produces it

Only the [dense engine](/engines/dense) can produce a statevector, on CPU or on a GPU. The other engines do not materialize the state, by design. Consequently:

- a statevector request is refused for circuits too large for a dense state, with an error stating the bytes required and the budget;
- for circuits that split into independent groups, MENOS simulates each group densely and forms the full state as an exact tensor product.

## Size

```text
qubits   bytes
   20    16 MiB
   25    512 MiB
   30    16 GiB
```

The default memory budget is 50% of physical memory. See [Memory Constraints](/planning/memory-constraints).

```python
import numpy as np
from qiskit import QuantumCircuit

from menos.qiskit import MENOSBackend

# 16 qubits: 1 MiB of amplitudes.
qc = QuantumCircuit(16)
for q in range(16):
    qc.ry(0.1 * (q + 1), q)
state = np.asarray(MENOSBackend().run(qc, shots=0, want_statevector=True).result().get_statevector())
assert state.size == 2**16
assert abs(np.linalg.norm(state) - 1.0) < 1e-12
```
