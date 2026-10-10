# EstimatorV2

Several pubs in one call, with parameters bound up front.

<<< @/../examples/estimator_v2.py

## Output

```text
theta = 0.0000   <Z> = 1.000000
theta = 1.0472   <Z> = 0.500000
theta = 1.5708   <Z> = 0.000000
theta = 3.1416   <Z> = -1.000000
```

After `RY(θ)` the expectation of `Z` is `cos θ`, and the script asserts each value against that. `MENOSEstimator` does not accept a circuit that still has parameters, so each pub binds them with `assign_parameters` first.

## See also

[EstimatorV2](/build/estimator) · [Estimator reference](/reference/estimator-api)
