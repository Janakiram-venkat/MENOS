# Stabilizer

A 60-qubit random Clifford circuit, far beyond any dense statevector.

<<< @/../examples/stabilizer.py

## Output

```text
strategy   direct_stabilizer (exact)
distinct outcomes in 16 shots: 16
```

Sixty qubits would need `16 · 2⁶⁰` bytes as a dense state. The stabilizer engine needs kilobytes, and the dense and MPS engines are both infeasible here, so it is the only candidate. A random Clifford circuit this deep spreads its outcomes widely, so 16 shots give 16 different results.

## See also

[Stabilizer](/engines/stabilizer)
