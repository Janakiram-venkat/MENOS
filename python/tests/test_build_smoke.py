"""Criterion 1: editable install imports menos, menos._core and
menos.qiskit."""

from __future__ import annotations


def test_import_menos():
    import menos

    assert hasattr(menos, "run")
    assert hasattr(menos, "Circuit")


def test_import_core():
    from menos import _core

    assert hasattr(_core, "run")
    assert hasattr(_core, "Circuit")
    assert hasattr(_core, "RunOptions")
    assert hasattr(_core, "Result")


def test_import_qiskit_adapter():
    from menos.qiskit import MENOSBackend

    assert MENOSBackend is not None
