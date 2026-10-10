"""Criterion 13: each C++ exception class is surfaced as the matching
Python exception class (docs/BUILD_PLAN.md Phase 2, 2.8 item 13).

Exercises all four directly through menos._core (not just through the
Qiskit adapter), since that's the actual translation boundary
(src/python/bindings.cpp).
"""

from __future__ import annotations

import pytest

from menos._core import Circuit, RunOptions
from menos._core import run as menos_run
from menos.exceptions import (
    InternalError,
    InvalidCircuitError,
    MenosError,
    ResourceExceededError,
    UnsupportedFeatureError,
)


def test_invalid_circuit_error():
    circuit = Circuit(0, 0)  # zero qubits: InvalidCircuit in C++
    options = RunOptions()
    options.shots = 0
    options.want_statevector = True
    options.seed = 0
    with pytest.raises(InvalidCircuitError):
        menos_run(circuit, options)


def test_unsupported_feature_error():
    # Phase 3 removed the 64-classical-bit ceiling this test previously
    # exercised (BitString has no width limit, docs/adr/0005); a
    # non-terminal measurement is still genuinely unsupported.
    from menos._core import Opcode

    circuit = Circuit(1, 1)
    circuit.append_measure(0, 0)
    circuit.append(Opcode.H, [0])  # a gate after measuring this qubit: non-terminal
    options = RunOptions()
    options.shots = 0
    options.want_statevector = True
    options.seed = 0
    with pytest.raises(UnsupportedFeatureError):
        menos_run(circuit, options)


def test_wide_classical_bit_counts_succeed_no_longer_unsupported():
    # The specific >64-clbit case this test used to reject now succeeds
    # end-to-end (docs/BUILD_PLAN.md Phase 3, 3.8).
    from menos._core import Opcode

    circuit = Circuit(1, 65)
    circuit.append(Opcode.X, [0])
    circuit.append_measure(0, 64)
    options = RunOptions()
    options.shots = 5
    options.want_statevector = False
    options.seed = 0
    result = menos_run(circuit, options)
    assert result.counts  # succeeds; no exception


def test_resource_exceeded_error():
    from menos._core import Opcode

    circuit = Circuit(30, 0)
    for q in range(30):
        circuit.append(Opcode.H, [q])
    options = RunOptions()
    options.shots = 0
    options.want_statevector = True
    options.seed = 0
    options.memory_budget_bytes = 1024  # far too small
    with pytest.raises(ResourceExceededError):
        menos_run(circuit, options)


def test_all_four_derive_from_menos_error():
    assert issubclass(InvalidCircuitError, MenosError)
    assert issubclass(UnsupportedFeatureError, MenosError)
    assert issubclass(ResourceExceededError, MenosError)
    assert issubclass(InternalError, MenosError)


def test_menos_error_catches_any_subclass():
    circuit = Circuit(0, 0)
    options = RunOptions()
    options.shots = 0
    options.want_statevector = True
    options.seed = 0
    with pytest.raises(MenosError):
        menos_run(circuit, options)
