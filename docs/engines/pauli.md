# Pauli Propagation

Pauli propagation computes an **expectation value** without ever building a state. Instead of evolving the state forward, it evolves the *observable* backward through the circuit (the Heisenberg picture) and evaluates the result against `|0…0⟩`.

## Capabilities

| Output | Supported |
|---|---|
| Expectation value | **Yes** (only) |
| Counts | No |
| Statevector | No |

It is enumerated only for expectation requests.

## Which circuits

The operations must be Clifford gates or one of a few rotations:

| Gate | Effect on one Pauli term |
|---|---|
| `X Y Z H S Sdg SX SXdg CX CZ SWAP` | One term becomes one term (with a sign) |
| `RZ RX RY P` | One term splits into **at most 2** weighted terms |
| `U` | One term splits into **at most 3** weighted terms |

`T`, `Tdg`, `CP`, `CCX` and unitary operations are **not supported** and make the engine infeasible, naming the operation:

```text
operation 20 (t) is not supported by Pauli propagation (Clifford gates and RZ/RX/RY/P/U only)
```

A region that is entirely Clifford is also refused, by the [routing rule](/engines/stabilizer#routing-rule-for-expectation-values): the stabilizer engine handles those.

## Cost and its limit

A Clifford gate does not change the number of terms. Each rotation can multiply it by up to 2 (by up to 3 for `U`), so in the worst case the count grows exponentially with the number of rotations that reach the observable. MENOS computes a worst-case bound *before* propagating anything and refuses a job whose bound cannot be represented, rather than truncating terms. The engine is always exact.

It shines when the observable is local and few rotations reach it.

## Example

An RZ-ladder circuit on 10 qubits with a single-qubit observable, run natively. The planner considers six strategies and picks Pauli propagation:

<<< @/../examples/pauli_propagation.py

```text
candidate strategy=direct_dense feasible=true winner=false peak_bytes=256 work_units=672
candidate strategy=direct_stabilizer feasible=false …
candidate strategy=direct_pauli feasible=true winner=true peak_bytes=256 work_units=128
candidate strategy=direct_mps feasible=true winner=false peak_bytes=384 work_units=148
candidate strategy=direct_tensor_network feasible=true winner=false peak_bytes=1024 work_units=2890
candidate strategy=cut_and_recombine feasible=true winner=false peak_bytes=256 work_units=840
```

Pauli propagation has 128 work units, against 672 for dense, and the value agrees with Qiskit's `Statevector` to about `1e-16`. The report above is abridged; the real lines have no space after the label (see [Plan Explanation](/planning/explain)).

## Related

[Observables](/observables/pauli-observables) · [Light-Cone Reduction](/observables/light-cone-reduction) · [Expectation Values](/build/expectation-values)
