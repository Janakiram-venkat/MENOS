# MPS

A 100-qubit circuit that no statevector could hold.

<<< @/../examples/mps.py

## Output

```text
strategy   direct_mps (exact)
state bytes actually used: 49792
candidate 0strategy=direct_dense feasible=false winner=false reason="checked_pow2: n = 100 exceeds the maximum of 62"
```

The dense engine is refused outright for 100 qubits. The circuit is a shallow brickwork of rotations and `CZ` gates, so entanglement stays low and the MPS needs only about 50 KB. The result is exact.

## Not always the right tool

MPS is chosen here because it is the only feasible engine. On circuits that entangle quickly its cost grows and another engine can win; see [MPS and Tensor Networks](/engines/mps).
