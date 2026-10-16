"""docs/adr/0024 gate criterion 4: finite-shot counts agree with
default.qubit's exact probabilities by chi-square at a family-wise 0.001
(Bonferroni, as in Phases 13-14); shot vectors keep default.qubit's result
structure; a fixed seed reproduces."""

from __future__ import annotations

import pytest

qml = pytest.importorskip("pennylane")

import numpy as np  # noqa: E402
from scipy.stats import chi2  # noqa: E402  (a PennyLane dependency)

from _pennylane_fixtures import fixture_operations, load_fixtures  # noqa: E402
from menos.pennylane import MENOSQubitDevice  # noqa: E402

FAMILY_WISE_ALPHA = 0.001
SHOTS = 20_000
# Small fixtures, so every outcome is in the test: circuits of 2-4 qubits.
CHI_FIXTURES = [e for e in load_fixtures() if e["num_qubits"] <= 4][:8]


def _chi_square_p(counts: dict, probs: np.ndarray, n: int, shots: int) -> float:
    observed = np.array([counts.get(format(b, f"0{n}b"), 0) for b in range(2**n)], float)
    keep = probs * shots >= 5  # the usual validity rule; the rest are pooled
    expected = probs * shots
    obs = np.append(observed[keep], observed[~keep].sum())
    exp = np.append(expected[keep], expected[~keep].sum())
    if exp[-1] == 0:
        assert obs[-1] == 0, "an outcome of probability 0 was sampled"
        obs, exp = obs[:-1], exp[:-1]
    stat = ((obs - exp) ** 2 / exp).sum()
    return float(chi2.sf(stat, len(exp) - 1))


@pytest.mark.parametrize("entry", CHI_FIXTURES, ids=lambda e: f"circuit{e['index']}")
def test_counts_chi_square_against_default_qubit(entry):
    n = entry["num_qubits"]
    ops = fixture_operations(entry)
    exact = qml.execute(
        [qml.tape.QuantumScript(ops, [qml.probs(wires=range(n))])],
        qml.device("default.qubit", wires=n),
    )[0]
    dev = MENOSQubitDevice(wires=n, seed=1000 + entry["index"])
    counts = qml.execute([qml.tape.QuantumScript(ops, [qml.counts()], shots=SHOTS)], dev)[0]
    assert sum(counts.values()) == SHOTS
    p = _chi_square_p(dict(counts), np.asarray(exact), n, SHOTS)
    assert p > FAMILY_WISE_ALPHA / len(CHI_FIXTURES), f"p={p}, counts={dict(counts)}"


def _structure(result):
    if isinstance(result, tuple):
        return tuple(_structure(r) for r in result)
    if isinstance(result, dict):
        return "dict"
    return np.shape(result)


def test_shot_vector_structure_matches_default_qubit():
    ops = [qml.Hadamard(0), qml.CNOT([0, 1]), qml.RY(0.4, 2)]
    measurements = [
        qml.expval(qml.X(0) @ qml.Z(1) + 0.3 * qml.Y(2)),
        qml.var(qml.Z(2)),
        qml.probs(wires=[2, 0]),
        qml.sample(wires=[1, 2]),
        qml.counts(wires=[0]),
        qml.sample(qml.Z(0) @ qml.Z(1)),
        qml.probs(op=qml.X(2)),
    ]
    shots = [100, (200, 2), 50]
    tape = qml.tape.QuantumScript(ops, measurements, shots=shots)
    ours = qml.execute([tape], MENOSQubitDevice(wires=3, seed=3))[0]
    ref = qml.execute([tape], qml.device("default.qubit", wires=3, seed=3))[0]
    assert _structure(ours) == _structure(ref)
    # Z0 Z1 after a Bell pair is +1 on every shot.
    for bin_result in ours:
        assert np.all(bin_result[5] == 1)


def test_finite_shot_expval_is_estimated_from_samples():
    # PennyLane's semantics (Q8): with shots the expectation is a sample mean,
    # so it is a multiple of 1/shots and not the exact value.
    tape = qml.tape.QuantumScript([qml.RY(0.9, 0)], [qml.expval(qml.Z(0))], shots=1000)
    value = qml.execute([tape], MENOSQubitDevice(wires=1, seed=11))[0]
    assert abs(value * 1000 - round(value * 1000)) < 1e-9
    assert value != pytest.approx(np.cos(0.9), abs=1e-6)
    assert abs(value - np.cos(0.9)) < 5 * np.sqrt((1 - np.cos(0.9) ** 2) / 1000)


def test_same_seed_gives_identical_results():
    ops = [qml.Hadamard(0), qml.CNOT([0, 1]), qml.RX(1.1, 2)]
    tape = qml.tape.QuantumScript(
        ops, [qml.sample(), qml.expval(qml.Z(2)), qml.counts()], shots=[300, 300]
    )
    a = qml.execute([tape, tape], MENOSQubitDevice(wires=3, seed=42))
    b = qml.execute([tape, tape], MENOSQubitDevice(wires=3, seed=42))
    for ta, tb in zip(a, b):
        for ba, bb in zip(ta, tb):
            assert np.array_equal(ba[0], bb[0])
            assert ba[1] == bb[1]
            assert dict(ba[2]) == dict(bb[2])
    # The two tapes and the two bins draw different seeds.
    assert not np.array_equal(a[0][0][0], a[0][1][0])
    assert not np.array_equal(a[0][0][0], a[1][0][0])


def test_samples_are_not_sorted():
    # Q8: rows are the counts in a seeded random order, not grouped by outcome.
    tape = qml.tape.QuantumScript([qml.Hadamard(0)], [qml.sample()], shots=200)
    rows = qml.execute([tape], MENOSQubitDevice(wires=1, seed=5))[0][:, 0]
    assert 0 < rows.sum() < 200
    assert not (np.all(np.diff(rows) >= 0) or np.all(np.diff(rows) <= 0))
