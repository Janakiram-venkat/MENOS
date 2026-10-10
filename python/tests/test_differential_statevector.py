"""Criterion 4: fixed-seed/randomized circuits from 1 to 12 qubits over the
complete supported KIR gate set compare against
qiskit.quantum_info.Statevector within the single tolerance policy
(docs/BUILD_PLAN.md Phase 2, 2.8 item 4)."""

from __future__ import annotations

import numpy as np
import pytest
from qiskit import QuantumCircuit
from qiskit.quantum_info import Statevector

from menos.qiskit._gate_table import GATE_TABLE

# Same absolute tolerance as the C++ suite's src/common/tolerance.h
# (kDefaultAbsTol); one tolerance policy across the whole project.
ABS_TOL = 1e-12

SEED = 20260921  # fixed, reproducible; print()s the circuit on failure below


def _random_circuit(rng: np.random.Generator, num_qubits: int) -> QuantumCircuit:
    qc = QuantumCircuit(num_qubits)
    qc.global_phase = float(rng.uniform(0, 2 * np.pi))
    num_ops = int(rng.integers(5, 15))
    names = list(GATE_TABLE.keys())
    for _ in range(num_ops):
        name = str(rng.choice(names))
        spec = GATE_TABLE[name]
        if spec.arity > num_qubits:
            continue
        qubits = [int(q) for q in rng.choice(num_qubits, size=spec.arity, replace=False)]
        params = [float(rng.uniform(-2 * np.pi, 2 * np.pi)) for _ in range(spec.num_params)]
        getattr(qc, name)(*params, *qubits)
    return qc


@pytest.mark.parametrize("num_qubits", [1, 2, 3, 5, 8, 12])
def test_differential_statevector(backend, num_qubits):
    rng = np.random.default_rng(SEED + num_qubits)
    qc = _random_circuit(rng, num_qubits)

    result = backend.run(qc, shots=0, want_statevector=True).result()
    actual = np.asarray(result.get_statevector(qc))
    expected = Statevector(qc).data

    if not np.allclose(actual, expected, atol=ABS_TOL, rtol=0):
        raise AssertionError(
            f"statevector mismatch for seed={SEED + num_qubits}, num_qubits={num_qubits}\n"
            f"circuit:\n{qc}\nmax abs diff: {np.max(np.abs(actual - expected))}"
        )
