# Stabilizer

The stabilizer engine simulates **Clifford circuits** in time and memory that grow polynomially with the number of qubits. It can handle hundreds of qubits where a dense state could never exist.

## Capabilities

| Output | Supported |
|---|---|
| Counts | Yes |
| Expectation value | Yes (exact `-1`, `0` or `+1` for a Pauli term) |
| Statevector | **No** |

It does not track global phase, so it cannot produce a statevector, and a statevector request makes it infeasible with the reason `the stabilizer engine does not track global phase -- only the dense engine can produce a statevector`.

## Which circuits

Only circuits whose every operation is a named Clifford gate:

```text
X  Y  Z  H  S  Sdg  SX  SXdg  CX  CZ  SWAP
```

A single non-Clifford gate (`T`, `RX(0.3)`, `CP`, `CCX`, or any unitary) makes the engine infeasible, and the reason names the first offending operation:

```text
operation 8 (t) is not a Clifford gate
```

## How it works

The engine keeps a bit-packed table of `n` stabilizer generators, each a Pauli string with a sign. A Clifford gate maps each generator to another Pauli string, which is a handful of bit operations. To sample, it reduces the final table once by Gaussian elimination over GF(2) to a base outcome plus a basis for the affine space of possible outcomes; each shot is the base XOR a random subset of the basis.

## Cost

```text
work = n                          row initialization
     + gates · n                  each gate touches each row
     + 2 · n² · words + n · words   (counts) elimination plus one sampling pass
words = ceil(n / 64)
```

Memory is `n · 2 · words · 8 + words · 8` bytes, about 13 KB for 200 qubits. Nothing in it is exponential.

## It does not always win

Being Clifford-only makes the engine *eligible*, not *chosen*. The planner compares work units, and for some Clifford circuits another engine is cheaper:

| Circuit | Planned onto |
|---|---|
| Bell pair, counts | `direct_stabilizer` (16 work units against 32 dense, 34 MPS) |
| 60-qubit random Clifford, counts | `direct_stabilizer` (dense and MPS are infeasible) |
| 200-qubit GHZ, counts | `direct_mps` (3,400 against 361,000) |

See the [stabilizer example](/examples/stabilizer).

## Routing rule for expectation values

A *fully Clifford* region is never given to Pauli propagation, even if that would estimate cheaper: the stabilizer engine handles a Clifford region's expectation value. This is a hard routing rule, not a cost preference.

## Related

[Pauli Propagation](/engines/pauli) · [Supported Gates](/reference/supported-gates)
