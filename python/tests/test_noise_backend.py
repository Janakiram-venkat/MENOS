"""Phase 12: Qiskit Aer noise models simulated exactly (docs/adr/0016).

Every reference here is Qiskit Aer itself: its density-matrix method and its
EstimatorV2, which compute the same exact quantities MENOS does. Expectation
values are compared to 1e-12 (common::kDefaultAbsTol), and the Pauli
expectations of all qubits together determine the whole measured distribution.
Counts, which are sampled, are compared under a chi-square test.
"""

from __future__ import annotations

import itertools

import numpy as np
import pytest
from qiskit import ClassicalRegister, QuantumCircuit
from qiskit.quantum_info import DensityMatrix, Kraus, SparsePauliOp
from scipy.stats import chi2

from menos import Circuit, RunOptions, run
from menos.exceptions import UnsupportedFeatureError
from menos.qiskit import MENOSBackend
from menos.qiskit._observable_convert import convert_observable
from menos.qiskit.convert import convert_circuit
from menos.qiskit.estimator import MENOSEstimator

qiskit_aer = pytest.importorskip("qiskit_aer")
from qiskit_aer import AerSimulator  # noqa: E402
from qiskit_aer.noise import (  # noqa: E402
    NoiseModel,
    QuantumError,
    ReadoutError,
    amplitude_damping_error,
    depolarizing_error,
    pauli_error,
    phase_damping_error,
    reset_error,
    thermal_relaxation_error,
)
from qiskit_aer.primitives import EstimatorV2 as AerEstimator  # noqa: E402

TOLERANCE = 1e-12
CHI_SQUARE_SIGNIFICANCE = 0.001


def all_paulis(n: int) -> list[SparsePauliOp]:
    """Every n-qubit Pauli string except the all-identity one: an observable whose
    terms are all the identity raises InternalError, a documented limitation that
    predates Phase 12 (docs/reference/limitations.md). Its value is the trace, 1,
    for every engine."""
    labels = ("".join(p) for p in itertools.product("IXYZ", repeat=n))
    return [SparsePauliOp(label) for label in labels if set(label) != {"I"}]


def aer_expectations(qc: QuantumCircuit, noise_model, observables) -> np.ndarray:
    estimator = AerEstimator(
        options={"backend_options": {"noise_model": noise_model, "method": "density_matrix"}}
    )
    return np.asarray(estimator.run([(qc, observables)]).result()[0].data.evs)


def menos_expectations(qc: QuantumCircuit, noise_model, observables) -> np.ndarray:
    estimator = MENOSEstimator(noise_model=noise_model)
    return np.asarray(estimator.run([(qc, observables)]).result()[0].data.evs)


def layered_circuit(n: int) -> QuantumCircuit:
    qc = QuantumCircuit(n)
    for q in range(n):
        qc.h(q)
        qc.rz(0.3 + 0.2 * q, q)
    for q in range(n - 1):
        qc.cx(q, q + 1)
    for q in range(n):
        qc.sx(q)
        qc.x(q)
    if n > 1:
        qc.cx(n - 1, 0)
    return qc


def mixed_noise_model() -> NoiseModel:
    model = NoiseModel()
    model.add_all_qubit_quantum_error(depolarizing_error(0.02, 1), ["h", "sx"])
    model.add_all_qubit_quantum_error(amplitude_damping_error(0.07), ["x", "rz"])
    model.add_all_qubit_quantum_error(depolarizing_error(0.05, 2), ["cx"])
    # A local error that overrides the all-qubit one on qubit 1 (Aer's rule).
    model.add_quantum_error(amplitude_damping_error(0.2), ["h"], [1])
    return model


# --- Exact agreement with Aer. ---


@pytest.mark.parametrize("n", [1, 2, 3])
def test_expectations_match_aer_density_matrix_on_every_pauli(n):
    qc = layered_circuit(n)
    observables = all_paulis(n)
    model = mixed_noise_model()
    np.testing.assert_allclose(
        menos_expectations(qc, model, observables),
        aer_expectations(qc, model, observables),
        rtol=0,
        atol=TOLERANCE,
    )


@pytest.mark.parametrize("order", ["depolarizing first", "damping first"])
def test_composed_errors_match_aer_in_both_orders(order):
    first, second = depolarizing_error(0.1, 1), amplitude_damping_error(0.3)
    if order == "damping first":
        first, second = second, first
    model = NoiseModel()
    # Two errors added to one instruction: Aer composes them in this order.
    model.add_all_qubit_quantum_error(first, ["h"])
    model.add_all_qubit_quantum_error(second, ["h"])
    qc = QuantumCircuit(1)
    qc.h(0)
    qc.ry(0.4, 0)
    qc.h(0)
    observables = all_paulis(1)
    np.testing.assert_allclose(
        menos_expectations(qc, model, observables),
        aer_expectations(qc, model, observables),
        rtol=0,
        atol=TOLERANCE,
    )


def test_noise_on_id_is_applied_even_though_id_itself_is_dropped():
    model = NoiseModel(basis_gates=["id", "h"])
    model.add_all_qubit_quantum_error(amplitude_damping_error(0.25), ["id"])
    qc = QuantumCircuit(1)
    qc.x(0)
    qc.id(0)
    qc.id(0)
    observables = all_paulis(1)
    menos = menos_expectations(qc, model, observables)
    np.testing.assert_allclose(menos, aer_expectations(qc, model, observables), atol=TOLERANCE)
    # Two dampings of 0.25 leave population 0.75^2 in |1>: <Z> = 1 - 2 * 0.5625.
    assert menos[2] == pytest.approx(1 - 2 * 0.5625, abs=TOLERANCE)  # X, Y, Z


def test_the_estimator_ignores_readout_errors_as_aer_does():
    model = NoiseModel()
    model.add_all_qubit_readout_error(ReadoutError([[0.0, 1.0], [1.0, 0.0]]))
    model.add_all_qubit_quantum_error(depolarizing_error(0.3, 1), ["h"])
    qc = QuantumCircuit(1)
    qc.h(0)
    observables = [SparsePauliOp("X"), SparsePauliOp("Z")]
    menos = menos_expectations(qc, model, observables)
    np.testing.assert_allclose(menos, aer_expectations(qc, model, observables), atol=TOLERANCE)
    assert menos[0] == pytest.approx(0.7, abs=TOLERANCE)


def test_noisy_estimator_results_are_exact_with_zero_stds():
    model = NoiseModel()
    model.add_all_qubit_quantum_error(depolarizing_error(0.3, 1), ["h"])
    qc = QuantumCircuit(1)
    qc.h(0)
    result = MENOSEstimator(noise_model=model).run([(qc, SparsePauliOp("X"))]).result()[0]
    assert float(result.data.stds) == 0.0


# --- Counts. ---


def expected_recorded_distribution(qc_unmeasured, model, readout_rows) -> np.ndarray:
    """Aer's exact noisy probabilities for every qubit measured to its own
    clbit, pushed through each qubit's readout matrix P(recorded | true)."""
    reference = qc_unmeasured.copy()
    reference.save_density_matrix()
    simulator = AerSimulator(method="density_matrix", noise_model=model)
    rho = np.asarray(simulator.run(reference).result().data()["density_matrix"])
    true_probabilities = np.real(np.diag(rho))
    n = qc_unmeasured.num_qubits
    recorded = np.zeros(2**n)
    for x, p in enumerate(true_probabilities):
        for y in range(2**n):
            weight = p
            for q in range(n):
                truth, seen = (x >> q) & 1, (y >> q) & 1
                weight *= readout_rows[q][truth][seen]
            recorded[y] += weight
    return recorded


def test_counts_with_gate_and_readout_noise_pass_a_chi_square_test():
    n = 3
    model = mixed_noise_model()
    readout_rows = [
        [[0.97, 0.03], [0.05, 0.95]],
        [[0.90, 0.10], [0.20, 0.80]],
        [[0.97, 0.03], [0.05, 0.95]],
    ]
    model.add_all_qubit_readout_error(ReadoutError(readout_rows[0]))
    model.add_readout_error(ReadoutError(readout_rows[1]), [1])

    unmeasured = layered_circuit(n)
    measured = unmeasured.copy()
    measured.add_register(ClassicalRegister(n))
    measured.measure(range(n), range(n))

    shots = 200_000
    counts = (
        MENOSBackend()
        .run(measured, shots=shots, seed_simulator=2026, noise_model=model)
        .result()
        .get_counts()
    )
    expected = expected_recorded_distribution(unmeasured, model, readout_rows) * shots
    observed = np.array([counts.get(format(y, f"0{n}b"), 0) for y in range(2**n)])
    assert observed.sum() == shots
    assert expected.min() > 5, "every outcome must be likely enough for the chi-square test"
    statistic = float(np.sum((observed - expected) ** 2 / expected))
    critical = chi2.ppf(1 - CHI_SQUARE_SIGNIFICANCE, df=2**n - 1)
    assert statistic < critical, f"chi-square {statistic} >= {critical}; counts={counts}"


def test_a_measure_error_is_applied_before_the_measurement_as_aer_simulator_does():
    # Certain damping attached to "measure": |1> is damped to |0> BEFORE it is
    # measured, so every shot records 0. Writing the error after the measurement
    # (as qiskit_aer.utils.insert_noise does) would record 1.
    model = NoiseModel()
    model.add_all_qubit_quantum_error(amplitude_damping_error(1.0), ["measure"])
    qc = QuantumCircuit(1, 1)
    qc.x(0)
    qc.measure(0, 0)
    menos_counts = (
        MENOSBackend().run(qc, shots=100, seed_simulator=1, noise_model=model).result().get_counts()
    )
    aer_counts = (
        AerSimulator(method="density_matrix")
        .run(qc, noise_model=model, shots=100, seed_simulator=1)
        .result()
        .get_counts()
    )
    assert menos_counts == {"0": 100}
    assert aer_counts == {"0": 100}


def test_noisy_counts_are_deterministic_under_a_seed():
    model = mixed_noise_model()
    model.add_all_qubit_readout_error(ReadoutError([[0.9, 0.1], [0.2, 0.8]]))
    qc = layered_circuit(2)
    qc.measure_all()
    backend = MENOSBackend()
    first = backend.run(qc, shots=2000, seed_simulator=5, noise_model=model).result().get_counts()
    second = backend.run(qc, shots=2000, seed_simulator=5, noise_model=model).result().get_counts()
    assert first == second


def test_set_options_carries_the_noise_model():
    model = NoiseModel()
    model.add_all_qubit_readout_error(ReadoutError([[0.0, 1.0], [1.0, 0.0]]))
    qc = QuantumCircuit(1, 1)
    qc.measure(0, 0)
    backend = MENOSBackend()
    backend.set_options(noise_model=model)
    assert backend.run(qc, shots=50, seed_simulator=1).result().get_counts() == {"1": 50}


# --- The verified-type rule (docs/adr/0016 section 9.1). ---


@pytest.mark.parametrize(
    "error, gates",
    [
        (depolarizing_error(0.1, 1), ["h"]),
        (depolarizing_error(4 / 3, 1), ["h"]),
        (depolarizing_error(0.1, 2), ["cx"]),
        (amplitude_damping_error(0.3), ["h"]),
        (amplitude_damping_error(1.0), ["h"]),
        (depolarizing_error(0.1, 1).compose(amplitude_damping_error(0.3)), ["h"]),
        (amplitude_damping_error(0.3).compose(depolarizing_error(0.1, 1)), ["h"]),
    ],
)
def test_verified_noise_types_are_accepted(error, gates):
    model = NoiseModel()
    model.add_all_qubit_quantum_error(error, gates)
    qc = QuantumCircuit(2, 2)
    qc.h(0)
    qc.cx(0, 1)
    qc.measure([0, 1], [0, 1])
    result = MENOSBackend().run(qc, shots=10, seed_simulator=1, noise_model=model).result()
    assert sum(result.get_counts().values()) == 10


@pytest.mark.parametrize(
    "error, gates",
    [
        (phase_damping_error(0.2), ["h"]),
        (thermal_relaxation_error(50e3, 70e3, 100), ["h"]),
        (pauli_error([("X", 0.1), ("I", 0.9)]), ["h"]),
        (reset_error(0.1), ["h"]),
        (amplitude_damping_error(0.3, excited_state_population=0.2), ["h"]),
        (amplitude_damping_error(0.1).tensor(amplitude_damping_error(0.1)), ["cx"]),
    ],
)
def test_unverified_noise_types_are_rejected_with_a_reason(error, gates):
    model = NoiseModel()
    model.add_all_qubit_quantum_error(error, gates)
    qc = QuantumCircuit(2, 2)
    qc.h(0)
    qc.cx(0, 1)
    qc.measure([0, 1], [0, 1])
    with pytest.raises(UnsupportedFeatureError, match="verified noise types"):
        MENOSBackend().run(qc, shots=10, seed_simulator=1, noise_model=model)


def test_an_appended_error_is_converted_under_the_same_rule():
    qc = QuantumCircuit(1, 1)
    qc.h(0)
    qc.append(depolarizing_error(0.2, 1), [0])
    qc.measure(0, 0)
    assert sum(MENOSBackend().run(qc, shots=20, seed_simulator=1).result().get_counts().values()) == 20

    rejected = QuantumCircuit(1, 1)
    rejected.append(phase_damping_error(0.2), [0])
    rejected.measure(0, 0)
    with pytest.raises(UnsupportedFeatureError, match="verified noise types"):
        MENOSBackend().run(rejected, shots=20, seed_simulator=1)


def test_a_correlated_readout_error_is_rejected():
    model = NoiseModel()
    model.add_readout_error(
        ReadoutError([[0.9, 0.05, 0.05, 0.0], [0, 1, 0, 0], [0, 0, 1, 0], [0, 0, 0, 1]]), [0, 1]
    )
    qc = QuantumCircuit(2, 2)
    qc.measure([0, 1], [0, 1])
    with pytest.raises(UnsupportedFeatureError, match="readout"):
        MENOSBackend().run(qc, shots=10, seed_simulator=1, noise_model=model)


def test_a_noise_model_of_the_wrong_type_is_a_type_error():
    qc = QuantumCircuit(1, 1)
    qc.measure(0, 0)
    with pytest.raises(TypeError, match="NoiseModel"):
        MENOSBackend().run(qc, shots=10, seed_simulator=1, noise_model="depolarizing")


# --- The native API: any valid channel, and Result.exact. ---


def random_cptp(num_qubits: int, rank: int, rng: np.random.Generator) -> list[np.ndarray]:
    """A random channel from a random isometry V (Stinespring): stacking the
    Kraus operators gives V, so sum K^dagger K = V^dagger V = I."""
    d = 2**num_qubits
    g = rng.normal(size=(rank * d, d)) + 1j * rng.normal(size=(rank * d, d))
    v, _ = np.linalg.qr(g)
    return [v[i * d : (i + 1) * d, :] for i in range(rank)]


@pytest.mark.parametrize("num_qubits, rank", [(1, 3), (2, 2), (2, 5)])
def test_a_random_channel_through_the_native_api_matches_aer(num_qubits, rank):
    rng = np.random.default_rng(1000 * num_qubits + rank)
    kraus = random_cptp(num_qubits, rank, rng)
    gates = layered_circuit(2)

    circuit = convert_circuit(gates)
    circuit.append_channel(list(range(num_qubits)), [k.flatten(order="C").tolist() for k in kraus])

    reference = gates.copy()
    reference.append(QuantumError(Kraus(kraus)), list(range(num_qubits)))
    reference.save_density_matrix()
    rho = DensityMatrix(
        np.asarray(
            AerSimulator(method="density_matrix").run(reference).result().data()["density_matrix"]
        )
    )

    for observable in all_paulis(2):
        options = RunOptions()
        options.shots = 0
        options.seed = 0
        options.expectation = convert_observable(observable, 2)
        result = run(circuit, options)
        assert result.exact
        assert result.expectation_value == pytest.approx(
            complex(rho.expectation_value(observable)), abs=TOLERANCE
        ), observable


def test_native_readout_error_argument():
    circuit = Circuit(1, 1)
    circuit.append_measure(0, 0, readout_error=[0.0, 1.0, 1.0, 0.0])
    options = RunOptions()
    options.shots = 30
    options.seed = 1
    result = run(circuit, options)
    assert result.counts == {"0x1": 30}
    assert "readout_errors=1" in result.explain()
