"""Criterion 15: build a wheel, install it into a clean virtual
environment, and import/run a minimal circuit (docs/BUILD_PLAN.md
Phase 2, 2.8 item 15).

Self-contained (doesn't use conftest.py's `backend` fixture) since
scripts/verify.sh invokes this one file with a separate interpreter --
the clean venv the wheel was installed into, not the editable-install dev
venv the rest of python/tests runs under.
"""

from __future__ import annotations


def test_wheel_install_smoke():
    import menos
    from menos.qiskit import MENOSBackend

    backend = MENOSBackend()
    from qiskit import QuantumCircuit

    qc = QuantumCircuit(2, 2)
    qc.h(0)
    qc.cx(0, 1)
    qc.measure([0, 1], [0, 1])

    result = backend.run(qc, shots=100, seed_simulator=1).result()
    counts = result.get_counts()

    assert sum(counts.values()) == 100
    assert set(counts) <= {"00", "11"}
    assert menos.__version__


def test_wheel_pennylane_entry_point():
    """docs/adr/0024 gate criterion 9: the installed wheel registers
    menos.qubit through the pennylane.plugins entry point."""
    import pytest

    qml = pytest.importorskip("pennylane")
    from menos.pennylane import MENOSQubitDevice

    dev = qml.device("menos.qubit", wires=2, seed=1)
    assert isinstance(dev, MENOSQubitDevice)

    @qml.set_shots(100)
    @qml.qnode(dev)
    def bell():
        qml.Hadamard(0)
        qml.CNOT([0, 1])
        return qml.counts()

    counts = bell()
    assert sum(counts.values()) == 100
    assert set(counts) <= {"00", "11"}
