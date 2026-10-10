# Bell State

The smallest entangled state, run through the Qiskit backend.

<<< @/../examples/bell_state.py

## Output

```text
{'00': 507, '11': 493}
```

With `seed_simulator=7` this is the same every run. Only `00` and `11` ever appear, because the two qubits are perfectly correlated, and the counts add up to the 1000 shots requested.

## What to try

- Change `seed_simulator` and watch the split change while staying close to 50/50.
- Remove the seed: each run then draws its own.

## See also

[Your First Circuit](/get-started/first-circuit) · [Counts](/build/counts)
