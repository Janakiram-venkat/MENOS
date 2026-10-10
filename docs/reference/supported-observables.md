# Supported Observables

## What is supported

**Pauli sums**: weighted sums of tensor products of `I`, `X`, `Y` and `Z`, with complex coefficients.

| Form | Supported | Notes |
|---|---|---|
| Single Pauli string | Yes | `SparsePauliOp("ZZI")` |
| Weighted sum of Pauli strings | Yes | All terms evaluated in one run |
| Complex coefficients | Yes | The Qiskit estimator returns the real part |
| Array of observables | Yes | Evaluated element by element |
| Observable on more than 64 qubits | Yes | Masks span several 64-bit words |
| Identity term alongside other terms | Yes | Exact |
| **Only** identity terms | **No** | Raises `InternalError`; add the constant yourself ([details](/observables/pauli-observables#an-all-identity-observable-is-not-supported)) |
| Non-Pauli operators (general matrices, projectors) | **No** | Not representable |
| Hermiticity | Not checked | A non-Hermitian sum gives a complex value natively, its real part through Qiskit |

## Which engines can evaluate one

| Engine | Condition |
|---|---|
| Dense | Always |
| Stabilizer | The circuit is entirely Clifford |
| Pauli propagation | Clifford gates and `rz rx ry p u` only, and the circuit is not entirely Clifford |
| MPS | Always (exact mode) within the memory budget |
| Tensor network | Always, within the memory budget |
| Cut and recombine | A cut exists (at most two cuts) |

Which one runs is the planner's choice; see [Plan](/execution/plan).

## Estimator-specific limits

`MENOSEstimator` additionally requires that the circuit carry no unbound parameters, and that the observable be a `SparsePauliOp` (or a mapping of Pauli labels to coefficients, as the Qiskit pub format produces).

## Related

[Pauli Observables](/observables/pauli-observables) · [SparsePauliOp](/observables/sparse-pauli-op) · [EstimatorV2](/build/estimator)
