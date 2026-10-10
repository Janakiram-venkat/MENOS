# SparsePauliOp

A multi-term observable, and an array of observables evaluated together.

<<< @/../examples/sparse_pauli_op.py

## Output

```text
values: [0.825336 1.       0.825336 1.119002]
```

The four values are `⟨ZZI⟩`, `⟨IZZ⟩`, `⟨XXX⟩` and the four-term Hamiltonian, each checked against Qiskit. The Hamiltonian includes a `YIY` term, which shows that `Y` is handled correctly.

## See also

[SparsePauliOp](/observables/sparse-pauli-op) · [Pauli Observables](/observables/pauli-observables)
