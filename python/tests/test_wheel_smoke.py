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
