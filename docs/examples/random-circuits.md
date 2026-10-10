# Random Circuits

Random circuits from Qiskit, lowered to MENOS's gate set by the transpiler and checked against Qiskit's simulator.

<<< @/../examples/random_circuits.py

## Output

```text
seed 0: max |difference| = 1.24e-16
seed 1: max |difference| = 2.48e-16
seed 2: max |difference| = 1.63e-16
seed 3: max |difference| = 2.30e-16
seed 4: max |difference| = 1.57e-16
```

Qiskit's `random_circuit` can use gates outside MENOS's native set. `transpile(qc, backend)` uses the backend's `Target` to lower exactly what is needed, and every result agrees with Qiskit to about `1e-16`.

## See also

[Qiskit Integration](/get-started/qiskit-integration) · [Supported Gates](/reference/supported-gates)
