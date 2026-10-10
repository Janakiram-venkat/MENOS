# GHZ State

A five-qubit GHZ state, measured for counts and computed as an exact statevector.

<<< @/../examples/ghz_state.py

## Output

```text
{'00000': 1029, '11111': 971}
statevector matches the analytic GHZ state
```

The counts contain only the all-zeros and all-ones strings. The statevector has exactly two non-zero amplitudes, `1/√2` at the first and last index, and the script asserts that to `1e-12`.

## Two requests, two plans

The measured circuit is planned for counts; the unmeasured one is planned for a statevector, which only the dense engine can produce. You can see the difference in the [plan report](/planning/explain).

## See also

[Statevectors](/build/statevectors) · [Counts](/build/counts)
