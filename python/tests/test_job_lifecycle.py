"""Criterion 12: result() executes once, repeated result() calls are
stable, and failure propagation preserves the original error category
(docs/BUILD_PLAN.md Phase 2, 2.8 item 12)."""

from __future__ import annotations

from qiskit import QuantumCircuit
from qiskit.providers import JobStatus

from menos.qiskit.job import MENOSJob


def test_status_is_always_done(backend):
    qc = QuantumCircuit(1, 1)
    qc.measure(0, 0)
    job = backend.run(qc, shots=1)
    assert isinstance(job, MENOSJob)
    assert job.status() == JobStatus.DONE
    assert job.done()


def test_repeated_result_calls_are_stable(backend):
    qc = QuantumCircuit(1, 1)
    qc.h(0)
    qc.measure(0, 0)
    job = backend.run(qc, shots=1000, seed_simulator=7)
    result_a = job.result()
    result_b = job.result()
    assert result_a.get_counts() == result_b.get_counts()


def test_submit_is_a_noop_execution_already_happened(backend):
    qc = QuantumCircuit(1, 1)
    qc.measure(0, 0)
    job = backend.run(qc, shots=1)
    # Execution already happened inside backend.run(); submit() must not
    # re-run or raise.
    job.submit()
    job.submit()
    assert job.result() is not None


def test_job_id_is_present_and_backend_is_this_backend(backend):
    qc = QuantumCircuit(1, 1)
    qc.measure(0, 0)
    job = backend.run(qc, shots=1)
    assert isinstance(job.job_id(), str) and job.job_id()
    assert job.backend() is backend


def test_failure_propagates_the_original_exception_category(backend):
    qc = QuantumCircuit(1, 1)
    qc.reset(0)
    # The failure happens inside run() itself (synchronous execution), so
    # it propagates from backend.run(), not from a later job.result() call
    # -- there is no job to query since execution never completed.
    from menos.exceptions import UnsupportedFeatureError

    try:
        backend.run(qc, shots=1)
    except UnsupportedFeatureError:
        pass
    else:
        raise AssertionError("expected UnsupportedFeatureError to propagate from run()")


def test_cancel_reports_unsupported(backend):
    qc = QuantumCircuit(1, 1)
    qc.measure(0, 0)
    job = backend.run(qc, shots=1)
    # JobV1.cancel() raises NotImplementedError by default (verified
    # against installed Qiskit source, docs/adr/0004); MENOSJob does not
    # override it, since cancellation of an already-completed synchronous
    # job is meaningless.
    try:
        job.cancel()
    except NotImplementedError:
        pass
    else:
        raise AssertionError("expected NotImplementedError from cancel()")
