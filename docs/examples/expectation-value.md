# Expectation Value

An exact expectation value of a two-term observable.

<<< @/../examples/expectation_value.py

## Output

```text
<ZZ + 0.5 XX> = 1.5
```

For a Bell state both `⟨ZZ⟩` and `⟨XX⟩` equal 1, so the value is `1 + 0.5 = 1.5`. The script also checks it against Qiskit's `Statevector.expectation_value` to `1e-12`.

## See also

[Expectation Values](/build/expectation-values) · [EstimatorV2](/build/estimator)
