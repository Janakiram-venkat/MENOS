# Statevector

An exact statevector, compared with Qiskit's own reference simulator.

<<< @/../examples/statevector.py

## Output

```text
amplitudes: [0.664237+0.j       0.      +0.j       0.      +0.j
 0.469686+0.469686j 0.      -0.242465j 0.      +0.j
 0.      +0.j       0.22468 -0.09115j ]
max |difference| from Qiskit's Statevector: 5.551115123125783e-17
```

The two results differ by about `6e-17`, which is double-precision rounding. The script asserts the difference is below `1e-12`.

## See also

[Statevectors](/build/statevectors) · [Conventions](/concepts/conventions)
