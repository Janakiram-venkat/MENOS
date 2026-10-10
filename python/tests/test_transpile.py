"""Criterion 11: transpile(qc, backend) followed by backend.run() produces
the same result as running the equivalent directly supported circuit
(docs/BUILD_PLAN.md Phase 2, 2.8 item 11)."""

from __future__ import annotations

import numpy as np
from qiskit import QuantumCircuit, transpile
from qiskit.quantum_info import Statevector

ABS_TOL = 1e-12


def test_transpile_then_run_matches_statevector_oracle(backend):
    qc = QuantumCircuit(2)
    qc.h(0)
    qc.cx(0, 1)

    transpiled = transpile(qc, backend)
    result = backend.run(transpiled, shots=0, want_statevector=True).result()
    actual = np.asarray(result.get_statevector(transpiled))
    expected = Statevector(qc).data  # transpilation must be semantics-preserving

    assert np.allclose(actual, expected, atol=ABS_TOL, rtol=0)


def test_transpile_of_an_unsupported_gate_lowers_to_supported_set(backend):
    # A gate outside MENOS's named set (e.g. the Toffoli's controlled-H
    # variant CH) that Qiskit's transpiler decomposes into the target's
    # basis gates.
    qc = QuantumCircuit(2)
    qc.ch(0, 1)

    transpiled = transpile(qc, backend)
    names = {instr.operation.name for instr in transpiled.data}
    from menos.qiskit._gate_table import GATE_TABLE

    supported = set(GATE_TABLE) | {"measure", "unitary", "barrier", "id", "delay"}
    assert names <= supported, f"transpile left unsupported instructions: {names - supported}"

    result = backend.run(transpiled, shots=0, want_statevector=True).result()
    actual = np.asarray(result.get_statevector(transpiled))
    expected = Statevector(qc).data
    assert np.allclose(actual, expected, atol=ABS_TOL, rtol=0)
