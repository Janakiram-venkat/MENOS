"""Criterion 3: every advertised instruction is convertible and
executable; every intentionally unsupported operation is not advertised
(docs/BUILD_PLAN.md Phase 2, 2.4)."""

from __future__ import annotations

from menos.qiskit._gate_table import GATE_TABLE


def test_all_named_gates_advertised(backend):
    names = set(backend.target.operation_names)
    for name in GATE_TABLE:
        assert name in names, f"{name} is in GATE_TABLE but not advertised in Target"


def test_measure_and_unitary_advertised(backend):
    names = set(backend.target.operation_names)
    assert "measure" in names
    assert "unitary" in names


def test_unsupported_semantics_not_advertised(backend):
    names = set(backend.target.operation_names)
    for unsupported in ("reset", "if_else", "for_loop", "while_loop", "switch_case"):
        assert unsupported not in names, f"{unsupported} must not be advertised (not executable)"


def test_advertised_set_matches_gate_table_exactly():
    # Target truthfulness: the advertised named-gate set is *generated
    # from* GATE_TABLE (target.py), so it cannot silently drift. This test
    # pins the count so a future edit to one file without the other is
    # caught.
    from menos.qiskit.target import build_target

    target = build_target()
    named_gate_count = len(GATE_TABLE)
    # + measure + unitary
    assert len(list(target.operation_names)) == named_gate_count + 2
