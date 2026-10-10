# Pauli Observables

MENOS computes expectation values of **Pauli observables**: real or complex weighted sums of Pauli strings.

```text
O  =  c₁ · P₁  +  c₂ · P₂  +  …        each Pᵢ a tensor product of I, X, Y, Z
```

## Representation

A Pauli string on `n` qubits is stored as two bit masks:

| Mask | Bit `j` set means |
|---|---|
| `x_words` | an X component on qubit `j` |
| `z_words` | a Z component on qubit `j` |

**Both set means Y**, following `Y = i·X·Z`. Neither set means identity on that qubit. Masks are lists of 64-bit words, so observables on more than 64 qubits use several words. This is exactly the convention Qiskit's `SparsePauliOp` uses internally, which is why conversion is exact, with no string parsing.

| Pauli on qubit `j` | `x` bit | `z` bit |
|---|---|---|
| I | 0 | 0 |
| X | 1 | 0 |
| Z | 0 | 1 |
| Y | 1 | 1 |

## Building one natively

```python
from menos._core import Observable, PauliString

def pauli(n, x_qubits=(), z_qubits=()):
    words = (n + 63) // 64
    x = [0] * words
    z = [0] * words
    for q in x_qubits:
        x[q // 64] |= 1 << (q % 64)
    for q in z_qubits:
        z[q // 64] |= 1 << (q % 64)
    p = PauliString()
    p.num_qubits = n
    p.x_words = x        # assign the whole list
    p.z_words = z
    return p

# Z0 Z1 + 0.5 * X2 on three qubits, and Y on qubit 1 (x and z both set).
observable = Observable()
observable.terms = [
    (pauli(3, z_qubits=[0, 1]), 1.0 + 0j),
    (pauli(3, x_qubits=[2]), 0.5 + 0j),
    (pauli(3, x_qubits=[1], z_qubits=[1]), 1.0 + 0j),
]
assert len(observable.terms) == 3
```

::: warning Assign whole lists
`PauliString.x_words`, `PauliString.z_words` and `Observable.terms` are copied across the Python and C++ boundary each time you read them. Modifying an element in place, for example `p.z_words[0] |= 1` or `observable.terms.append(...)`, changes a temporary copy and **silently does nothing**. Build the list first and assign it, as above.
:::

```python
from menos._core import PauliString

p = PauliString()
p.num_qubits = 2
p.x_words = [0]
p.z_words = [0]
p.z_words[0] |= 1          # modifies a temporary copy
assert p.z_words == [0]    # the assignment did not stick
```

## Supported observables

Only Pauli sums. See [Supported Observables](/reference/supported-observables).

## An all-identity observable is not supported

An observable in which **every** term is the identity is a constant, `⟨I⟩ = 1`, but MENOS does not currently handle it: planning reduces the circuit to zero qubits and the run raises `InternalError` (`Tableau: num_qubits must be at least 1`). An identity term **alongside** any other term works and is exact.

If you need a constant offset, add it yourself after the run:

```python
from qiskit import QuantumCircuit
from qiskit.quantum_info import SparsePauliOp

from menos.qiskit import MENOSEstimator

qc = QuantumCircuit(2)
qc.h(0)
qc.cx(0, 1)

constant = 2.5                                  # the coefficient of the identity term
value = constant + float(
    MENOSEstimator().run([(qc, SparsePauliOp("ZZ", coeffs=[0.5]))]).result()[0].data.evs
)
assert abs(value - 3.0) < 1e-12
```

## Hermiticity

MENOS does not check that an observable is Hermitian. A non-Hermitian sum has a complex expectation value; the native API returns it as a `complex`, and the Qiskit estimator returns the real part, as Qiskit's reference estimator does.

## Related

[SparsePauliOp](/observables/sparse-pauli-op) · [Expectation Values](/build/expectation-values) · [Conventions](/concepts/conventions)
