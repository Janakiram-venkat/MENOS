# Job

`MENOSBackend.run` returns a `menos.qiskit.job.MENOSJob`, a Qiskit `JobV1`.

```text
class MENOSJob(JobV1)
```

Jobs are created by the backend; you do not construct them.

## Members

| Member | Behaviour |
|---|---|
| `job_id()` | A UUID string |
| `backend()` | The `MENOSBackend` that produced it |
| `result(timeout=None)` | Returns the Qiskit `Result`. `timeout` is accepted for interface compatibility and ignored |
| `status()` | Always `JobStatus.DONE` |
| `submit()` | A no-op: the work already happened in `run` |

## Why it is always finished

Execution is **synchronous**. The simulation has completed when `MENOSBackend.run` returns, so the job only holds the answer. No background thread exists. There is no cancellation, and no queued or running state.

If a job fails, `run` raises; you never obtain a job object for a failed run.

## Verification

```python
import inspect

from qiskit import QuantumCircuit
from qiskit.providers import JobStatus

from menos.qiskit import MENOSBackend
from menos.qiskit.job import MENOSJob

assert str(inspect.signature(MENOSJob.result)) == "(self, timeout=None) -> 'QiskitResult'"
assert str(inspect.signature(MENOSJob.status)) == "(self) -> 'JobStatus'"

qc = QuantumCircuit(1, 1)
qc.x(0)
qc.measure(0, 0)
job = MENOSBackend().run(qc, shots=3, seed_simulator=1)
assert isinstance(job, MENOSJob)
assert job.status() == JobStatus.DONE
assert job.result() is job.result()
```
