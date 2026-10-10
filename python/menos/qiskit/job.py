"""MENOSJob(JobV1): a synchronous job.

The result already exists when MENOSBackend.run() returns, matching
qiskit.providers.basic_provider.BasicProviderJob's own pattern (verified
against installed Qiskit source, docs/adr/0004). No background thread is
introduced.
"""

from __future__ import annotations

from qiskit.providers import JobStatus
from qiskit.providers.job import JobV1
from qiskit.result import Result as QiskitResult


class MENOSJob(JobV1):
    _async = False

    def __init__(self, backend, job_id: str, result: QiskitResult) -> None:
        super().__init__(backend, job_id)
        self._result = result

    def submit(self) -> None:
        # No-op: execution already happened in MENOSBackend.run().
        return

    def result(self, timeout=None) -> QiskitResult:
        # timeout is part of JobV1's contract; meaningless here since
        # execution is synchronous and the result already exists.
        del timeout
        return self._result

    def status(self) -> JobStatus:
        return JobStatus.DONE
