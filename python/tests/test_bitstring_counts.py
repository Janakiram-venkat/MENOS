"""Phase 3 required Python test: 100-classical-bit counts are correctly
formatted for Qiskit (docs/BUILD_PLAN.md Phase 3, 3.11 "Required Python
tests"), the specific width the spec names for BitString migration
testing (alongside 63/64/65, covered at the C++ level in
tests/common/bitstring_test.cpp and tests/ir/validate_test.cpp).

Also differentially checks menos._core's raw hex-keyed counts (before
Qiskit's own binary-string formatting) against Python's built-in hex(),
proving BitString::to_hex_string() is byte-identical to it (docs/adr/0005).
"""

from __future__ import annotations

from qiskit import QuantumCircuit


def test_one_hundred_classical_bits_formats_correctly_for_qiskit(backend):
    qc = QuantumCircuit(2, 100)
    qc.x(0)
    qc.x(1)
    qc.measure(0, 0)  # lowest clbit
    qc.measure(1, 99)  # highest clbit

    shots = 5
    result = backend.run(qc, shots=shots, seed_simulator=1).result()
    counts = result.get_counts()

    assert len(counts) == 1
    (bitstring, count), = counts.items()
    assert count == shots
    assert len(bitstring) == 100
    # Qiskit prints the highest clbit first: bitstring[0] is clbit 99,
    # bitstring[-1] is clbit 0.
    assert bitstring[0] == "1"  # clbit 99
    assert bitstring[-1] == "1"  # clbit 0
    assert bitstring[1:-1] == "0" * 98


def test_raw_hex_counts_match_pythons_own_hex(backend):
    # A value spanning past the first 64-bit word: bit 0 and bit 70 both
    # set, value = 2**70 + 1.
    qc = QuantumCircuit(2, 100)
    qc.x(0)
    qc.x(1)
    qc.measure(0, 0)
    qc.measure(1, 70)

    result = backend.run(qc, shots=3, seed_simulator=1).result()
    raw_counts = result.results[0].data.counts
    assert len(raw_counts) == 1
    (hex_key, count), = raw_counts.items()
    assert count == 3

    expected_value = (1 << 70) | 1
    assert hex_key == hex(expected_value)
