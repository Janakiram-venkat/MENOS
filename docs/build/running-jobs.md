# Running Jobs

A *job* is one call to `MENOSBackend.run`. This page covers the choices you make per call.

## Choosing the output

Each call asks for some combination of outputs. At least one is required.

| You want | Ask with |
|---|---|
| Measurement counts | a circuit with `measure` and `shots > 0` |
| The exact statevector | `want_statevector=True` (usually with `shots=0`) |
| Both | a measured circuit, `shots > 0` and `want_statevector=True` |
| An expectation value | use [`MENOSEstimator`](/build/estimator) instead |

```python
from qiskit import QuantumCircuit

from menos.qiskit import MENOSBackend

backend = MENOSBackend()
qc = QuantumCircuit(2, 2)
qc.h(0)
qc.cx(0, 1)
qc.measure([0, 1], [0, 1])

# Counts only.
counts_only = backend.run(qc, shots=100, seed_simulator=1).result()
assert "counts" in counts_only.data(0)

# Nothing requested: refused.
try:
    backend.run(qc, shots=0)
except Exception as error:
    print(type(error).__name__)
    assert type(error).__name__ == "InvalidCircuitError"
```

Asking for a statevector rules out some engines: only the [dense engine](/engines/dense) can produce one, so the circuit must be small enough for a dense state.

## Shots and seeds

`shots` is the number of samples. `seed_simulator` fixes the random stream:

```python
from qiskit import QuantumCircuit

from menos.qiskit import MENOSBackend

backend = MENOSBackend()
qc = QuantumCircuit(1, 1)
qc.h(0)
qc.measure(0, 0)

a = backend.run(qc, shots=200, seed_simulator=9).result().get_counts()
b = backend.run(qc, shots=200, seed_simulator=9).result().get_counts()
assert a == b
```

The same circuit, shots, options and seed always give the same counts. See [Determinism](/concepts/determinism). If you do not pass a seed, a fresh one is drawn and recorded in the result, so you can reproduce a run afterwards.

## The job object

`run` returns a `MENOSJob`. Because execution is synchronous it is already finished:

```python
from qiskit import QuantumCircuit

from menos.qiskit import MENOSBackend

qc = QuantumCircuit(1, 1)
qc.x(0)
qc.measure(0, 0)
job = MENOSBackend().run(qc, shots=5, seed_simulator=2)
print(job.status(), job.job_id())
assert str(job.status()) == "JobStatus.DONE"
assert job.result().get_counts() == {"1": 5}
```

`job.result(timeout=...)` accepts a timeout for interface compatibility and ignores it.

## Failures

A job that cannot run raises when you call `run`, not when you ask for the result, because the work happens inside `run`. The common cases:

| Situation | Exception |
|---|---|
| Nothing requested | `InvalidCircuitError` |
| `shots > 0` but no `measure` | `InvalidCircuitError` |
| Gate or feature MENOS cannot represent | `UnsupportedFeatureError` |
| No plan fits the memory budget | `ResourceExceededError` |

See [Common Errors](/troubleshooting/common-errors).

## Timing

`result.time_taken` is the time spent executing plus sampling, in seconds. It excludes planning and conversion.
