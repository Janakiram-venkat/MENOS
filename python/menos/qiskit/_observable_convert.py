"""SparsePauliOp -> menos._core.Observable conversion, the single source
of truth for menos.qiskit's Pauli-observable boundary (mirrors
_gate_table.py's role for named gates, docs/BUILD_PLAN.md Phase 5:
"through the same gate-table-style single source of truth used for
circuit conversion in Phase 2").
"""

from __future__ import annotations

from qiskit.quantum_info import SparsePauliOp

from menos._core import Observable, PauliString


def convert_observable(op: SparsePauliOp, num_qubits: int) -> Observable:
    """Converts a SparsePauliOp to a menos._core.Observable.

    op.paulis.x[i]/.z[i] are per-term boolean arrays, bit j = qubit j,
    with op.paulis.phase always 0 for any SparsePauliOp (any phase from
    Y = i*X*Z is already folded into op.coeffs) -- verified against
    installed Qiskit 2.5.2 this session (docs/adr/0008), and exactly
    MENOS's own PauliString convention (docs/adr/0006, docs/adr/0007).
    No Pauli label string is parsed or emitted, in either direction.
    """
    words = (num_qubits + 63) // 64
    x_bits = op.paulis.x
    z_bits = op.paulis.z
    coeffs = op.coeffs

    terms: list[tuple[PauliString, complex]] = []
    for i in range(len(op)):
        x_words = [0] * words
        z_words = [0] * words
        for q in range(num_qubits):
            if bool(x_bits[i][q]):
                x_words[q // 64] |= 1 << (q % 64)
            if bool(z_bits[i][q]):
                z_words[q // 64] |= 1 << (q % 64)

        pauli = PauliString()
        pauli.num_qubits = num_qubits
        pauli.x_words = x_words
        pauli.z_words = z_words
        terms.append((pauli, complex(coeffs[i])))

    # nanobind's STL-vector properties are copy-in/copy-out -- assigning
    # the full list once is required; observable.terms.append(...) would
    # mutate a throwaway copy, not the bound object.
    observable = Observable()
    observable.terms = terms
    return observable
