"""RunOptions.max_threads through the Python API (Phase 15, docs/adr/0019
section 3.3): a cap on CPU threads for gate application. None means every
hardware thread, 0 is refused, and the thread count never changes a result.

These tests go through menos._core directly; the Qiskit adapters do not set
the field.
"""

from __future__ import annotations

import numpy as np
import pytest

from menos._core import Circuit, Opcode, RunOptions
from menos._core import run as menos_run
from menos.exceptions import InvalidCircuitError


def _circuit(num_qubits: int) -> Circuit:
    circuit = Circuit(num_qubits, num_qubits)
    for layer in range(4):
        for q in range(num_qubits):
            circuit.append(Opcode.RY, [q], [0.3 + 0.1 * q + layer])
        for q in range(layer % 2, num_qubits - 1, 2):
            circuit.append(Opcode.CX, [q, q + 1])
    for q in range(num_qubits):
        circuit.append_measure(q, q)
    return circuit


def _options(max_threads: int | None) -> RunOptions:
    options = RunOptions()
    options.shots = 500
    options.seed = 5
    options.want_statevector = True
    options.max_threads = max_threads
    return options


def test_max_threads_round_trips():
    options = RunOptions()
    assert options.max_threads is None
    options.max_threads = 4
    assert options.max_threads == 4
    options.max_threads = None
    assert options.max_threads is None


def test_zero_threads_is_refused():
    with pytest.raises(InvalidCircuitError, match="max_threads"):
        menos_run(_circuit(2), _options(0))


def test_results_do_not_depend_on_the_thread_count():
    circuit = _circuit(16)
    reference = menos_run(circuit, _options(1))
    for threads in (2, 3, None, 1_000_000):
        result = menos_run(circuit, _options(threads))
        assert result.counts == reference.counts, threads
        # Bit for bit, not within a tolerance.
        assert np.array_equal(
            np.asarray(result.statevector).view(np.uint64),
            np.asarray(reference.statevector).view(np.uint64),
        ), threads
