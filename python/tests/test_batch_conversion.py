"""Circuit.append_batch (preliminary Phase 3 step, docs/adr/0004): proves
convert_circuit's batched-flush accumulation produces correct results when
interleaved with instructions that force a flush mid-circuit (a custom
unitary, which is not in GATE_TABLE) and with parameterized named gates,
not just the pure named-gate-only representative benchmark circuit.
"""

from __future__ import annotations

import numpy as np
from qiskit import QuantumCircuit
from qiskit.quantum_info import Statevector

ABS_TOL = 1e-12


def test_batch_flush_interleaved_with_unitary_matches_oracle(backend):
    # Named gates (accumulated into a batch) -> a custom Unitary (forces a
    # flush) -> more named gates with params (a second batch) -> named
    # gates on a different qubit -> measure. Exercises multiple
    # append_batch flushes within one circuit, not just one at the end.
    matrix = np.array([[0, 1], [1, 0]], dtype=complex)  # X, as a non-named-gate Unitary
    qc = QuantumCircuit(3, 3)
    qc.h(0)
    qc.cx(0, 1)
    qc.unitary(matrix, [2], label="custom_x")
    qc.rx(0.3, 0)
    qc.ry(1.1, 1)
    qc.cx(1, 2)
    qc.measure([0, 1, 2], [0, 1, 2])

    result = backend.run(qc, shots=0, want_statevector=True).result()
    actual = np.asarray(result.get_statevector(qc))
    expected = Statevector(qc.remove_final_measurements(inplace=False)).data
    assert np.allclose(actual, expected, atol=ABS_TOL, rtol=0)


def test_batch_flush_preserves_operation_order_for_depth(backend):
    # A circuit whose correctness depends on operation ORDER within a
    # batch and across flushes (RX(pi) then RX(-pi) on the same qubit
    # must cancel; if append_batch's CSR offsets mis-ordered operations,
    # this would not return to |0>).
    qc = QuantumCircuit(1)
    qc.rx(np.pi, 0)
    qc.rx(-np.pi, 0)

    result = backend.run(qc, shots=0, want_statevector=True).result()
    actual = np.asarray(result.get_statevector(qc))
    expected = Statevector(qc).data
    assert np.allclose(actual, expected, atol=ABS_TOL, rtol=0)
