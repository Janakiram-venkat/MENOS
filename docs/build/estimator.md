# EstimatorV2

`menos.qiskit.MENOSEstimator` implements Qiskit's `BaseEstimatorV2`. Given circuits and Pauli observables it returns exact expectation values.

```text
MENOSEstimator(*, default_precision: float = 0.0, noise_model: NoiseModel | None = None)
MENOSEstimator.run(pubs, *, precision=None) -> PrimitiveJob
```

## A pub

A *pub* (primitive unified bloc) is `(circuit, observables)`. Observables can be a single `SparsePauliOp` or an array of them:

```python
from qiskit import QuantumCircuit
from qiskit.quantum_info import SparsePauliOp

from menos.qiskit import MENOSEstimator

qc = QuantumCircuit(2)
qc.h(0)
qc.cx(0, 1)

estimator = MENOSEstimator()
job = estimator.run([(qc, [SparsePauliOp("ZZ"), SparsePauliOp("XX"), SparsePauliOp("ZI")])])
result = job.result()[0]
print(result.data.evs)

assert list(result.data.evs) == [1.0, 1.0, 0.0]
assert float(result.data.stds.max()) == 0.0
```

Several pubs in one `run` are evaluated in order, and `job.result()` has one `PubResult` per pub.

## Precision

The engines are exact, so `stds` is always zero. A requested `precision` is recorded in each result's metadata and **never** used to add noise:

```python
from qiskit import QuantumCircuit
from qiskit.quantum_info import SparsePauliOp

from menos.qiskit import MENOSEstimator

qc = QuantumCircuit(1)
qc.h(0)
result = MENOSEstimator(default_precision=0.05).run([(qc, SparsePauliOp("X"))]).result()[0]
print(result.metadata)
assert result.metadata["target_precision"] == 0.05
assert abs(float(result.data.evs) - 1.0) < 1e-12
```

## Parameters must be bound first

`MENOSEstimator` rejects any circuit that still has parameters, even if you pass `parameter_values` in the pub. Bind them with `assign_parameters` beforehand, as the [EstimatorV2 example](/examples/estimator) does. The error is `UnsupportedFeatureError`:

```text
MENOSEstimator does not currently support parameterized circuits (1 unbound parameter(s)); bind all parameters first (QuantumCircuit.assign_parameters).
```

## How it runs

For each pub, MENOS converts the circuit once and then performs **one planned, expectation-only run per observable element**. Each run is planned independently, so different observables can be executed by different engines. Terms of one `SparsePauliOp` are evaluated together in a single run.

## Differences from Qiskit's reference estimator

| | `MENOSEstimator` | `StatevectorEstimator` |
|---|---|---|
| Result | exact | exact |
| Parameterized circuits | rejected | supported |
| Non-Hermitian observable | real part returned | real part returned |
| Scale | engine-dependent; can exceed statevector limits | statevector-limited |

## See also

- [Expectation Values](/build/expectation-values)
- [Estimator reference](/reference/estimator-api)
- [Supported Observables](/reference/supported-observables)
