"""docs/adr/0024 gate criterion 6: noisy expval/var equal default.mixed's
within 1e-12; noisy counts agree with default.mixed's probabilities by
chi-square; noisy analytic state/probs are refused (section 3.6)."""

from __future__ import annotations

import pytest

qml = pytest.importorskip("pennylane")

import numpy as np  # noqa: E402
from scipy.stats import chi2  # noqa: E402

import menos  # noqa: E402
from _pennylane_fixtures import TOL  # noqa: E402
from menos.pennylane import MENOSDeviceUnsupportedError, MENOSQubitDevice  # noqa: E402

_KRAUS_RNG = np.random.default_rng(99)


def _random_two_qubit_channel():
    # A random CPTP map: Kraus operators from a random isometry.
    z = _KRAUS_RNG.normal(size=(12, 4)) + 1j * _KRAUS_RNG.normal(size=(12, 4))
    q, _ = np.linalg.qr(z)
    return [q[4 * i : 4 * i + 4, :] for i in range(3)]


NOISY_CIRCUITS = {
    "depolarizing": lambda: [
        qml.Hadamard(0), qml.DepolarizingChannel(0.2, 0), qml.CNOT([0, 1]),
        qml.DepolarizingChannel(0.05, 1),
    ],
    "amplitude damping": lambda: [
        qml.PauliX(0), qml.AmplitudeDamping(0.3, 0), qml.RY(0.7, 1), qml.CNOT([1, 0]),
    ],
    "generalized amplitude damping": lambda: [
        qml.RX(1.1, 0), qml.GeneralizedAmplitudeDamping(0.25, 0.4, 0), qml.CNOT([0, 1]),
    ],
    "phase damping": lambda: [qml.Hadamard(0), qml.PhaseDamping(0.35, 0), qml.CNOT([0, 1])],
    "bit and phase flip": lambda: [
        qml.Hadamard(1), qml.BitFlip(0.1, 0), qml.PhaseFlip(0.2, 1), qml.CZ([0, 1]),
    ],
    "pauli error": lambda: [
        qml.Hadamard(0), qml.CNOT([0, 1]), qml.PauliError("XY", 0.15, wires=[0, 1]),
    ],
    "thermal relaxation": lambda: [
        qml.Hadamard(0),
        qml.ThermalRelaxationError(0.1, 50e-6, 40e-6, 2e-6, wires=0),
        qml.CNOT([0, 1]),
    ],
    "reset error": lambda: [qml.PauliX(1), qml.ResetError(0.1, 0.2, wires=1), qml.CNOT([1, 0])],
    "two-qubit QubitChannel": lambda: [
        qml.Hadamard(0), qml.RY(0.5, 1), qml.QubitChannel(_random_two_qubit_channel(), wires=[1, 0]),
    ],
}

_OBSERVABLE = lambda: qml.X(0) @ qml.Z(1) + 0.5 * qml.Z(0) - 0.3 * qml.Y(1)  # noqa: E731


@pytest.mark.parametrize("name", sorted(NOISY_CIRCUITS))
def test_noisy_expval_and_var_match_default_mixed(name):
    ops = NOISY_CIRCUITS[name]()
    measurements = [qml.expval(_OBSERVABLE()), qml.var(_OBSERVABLE())]
    tape = qml.tape.QuantumScript(ops, measurements)
    expected = qml.execute([tape], qml.device("default.mixed", wires=2))[0]
    actual = qml.execute([tape], MENOSQubitDevice(wires=2))[0]
    for e, a in zip(expected, actual):
        assert abs(a - e) < TOL, (name, a, e)


@pytest.mark.parametrize("noise_method", ["exact", "auto"])
@pytest.mark.parametrize("name", sorted(NOISY_CIRCUITS))
def test_noisy_counts_chi_square_against_default_mixed(name, noise_method):
    ops = NOISY_CIRCUITS[name]()
    shots = 20_000
    probs = qml.execute(
        [qml.tape.QuantumScript(ops, [qml.probs(wires=[0, 1])])], qml.device("default.mixed", wires=2)
    )[0]
    dev = MENOSQubitDevice(wires=2, seed=7, noise_method=noise_method)
    counts = qml.execute([qml.tape.QuantumScript(ops, [qml.counts()], shots=shots)], dev)[0]
    observed = np.array([counts.get(format(b, "02b"), 0) for b in range(4)], float)
    expected = np.asarray(probs) * shots
    keep = expected > 0
    assert observed[~keep].sum() == 0
    stat = ((observed[keep] - expected[keep]) ** 2 / expected[keep]).sum()
    p = chi2.sf(stat, keep.sum() - 1)
    family = 2 * len(NOISY_CIRCUITS)
    assert p > 0.001 / family, (name, noise_method, p, dict(counts))


@pytest.mark.parametrize("measurement", [qml.state, lambda: qml.probs(wires=[0, 1])])
def test_noisy_analytic_state_and_probs_are_refused(measurement):
    ops = NOISY_CIRCUITS["depolarizing"]()
    tape = qml.tape.QuantumScript(ops, [measurement()])
    with pytest.raises(MENOSDeviceUnsupportedError, match="noisy circuit with shots=None"):
        qml.execute([tape], MENOSQubitDevice(wires=2))


def test_noisy_analytic_runs_on_the_exact_engine(monkeypatch):
    # noise_method="auto" applies to counts only: analytic results stay exact.
    import menos.pennylane.device as device_module

    seen = []
    real_run = device_module.menos_run

    def spy(circuit, options):
        seen.append(options.noise_method)
        return real_run(circuit, options)

    monkeypatch.setattr(device_module, "menos_run", spy)
    tape = qml.tape.QuantumScript(NOISY_CIRCUITS["depolarizing"](), [qml.expval(qml.Z(0))])
    qml.execute([tape], MENOSQubitDevice(wires=2, noise_method="auto"))
    assert seen == [menos.NoiseMethod.Exact]


def test_channel_outside_menos_cptp_tolerance_is_refused():
    # PennyLane accepts trace preservation to about 1e-8; MENOS checks it
    # within 1e-12 (src/ir/validate.cpp) and refuses, never renormalising.
    almost = [np.sqrt(1 + 1e-10) * np.eye(2)]
    tape = qml.tape.QuantumScript([qml.QubitChannel(almost, wires=0)], [qml.expval(qml.Z(0))])
    with pytest.raises(menos.InvalidCircuitError):
        qml.execute([tape], MENOSQubitDevice(wires=1))
