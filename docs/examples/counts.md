# Counts

Seeds, reproducibility, and what a partial measurement returns.

<<< @/../examples/counts.py

## Output

```text
seed 42: {'000': 64, '001': 58, '010': 65, '011': 58, '100': 68, '101': 64, '110': 68, '111': 55}
seed 43: {'000': 75, '001': 68, '010': 76, '011': 50, '100': 65, '101': 60, '110': 58, '111': 48}
one measured qubit: {'1': 100}
```

The same seed always gives the same counts (the script asserts it), and a different seed gives a different sample of the same uniform distribution. With a one-bit classical register the keys are one character wide.

## See also

[Counts](/build/counts) · [Determinism](/concepts/determinism)
