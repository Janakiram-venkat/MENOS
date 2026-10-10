"""Phase 13: trajectory simulation through Python and Qiskit (docs/adr/0017).

The statistical agreement of trajectories with the exact density matrix is
tested in C++ (tests/api/trajectory_run_test.cpp). These tests cover the Python
surface: the new options and result field, the opt-in rules as a Python caller
sees them, MENOSEstimator's stds and seed, and MENOSBackend's noise_method.
"""

from __future__ import annotations

import numpy as np
import pytest
from qiskit import QuantumCircuit
from qiskit.quantum_info import SparsePauliOp

from menos import Circuit, NoiseMethod, Opcode, RunOptions, run
from menos.exceptions import InvalidCircuitError
from menos.qiskit import MENOSBackend
from menos.qiskit._observable_convert import convert_observable
from menos.qiskit.estimator import MENOSEstimator

qiskit_aer = pytest.importorskip("qiskit_aer")
from qiskit_aer.noise import NoiseModel, amplitude_damping_error, depolarizing_error  # noqa: E402
from qiskit_aer.primitives import EstimatorV2 as AerEstimator  # noqa: E402


def depolarizing_kraus(p: float) -> list[list[complex]]:
    a = np.sqrt(1 - 3 * p / 4)
    b = np.sqrt(p / 4)
    return [[a, 0, 0, a], [0, b, b, 0], [0, -1j * b, 1j * b, 0], [b, 0, 0, -b]]


def noisy_native_chain(n: int, num_clbits: int = 0) -> Circuit:
    """RY layer, one depolarizing channel, then a CX chain: the light cone of
    the last qubit keeps every qubit and the noise."""
    circuit = Circuit(n, num_clbits)
    for q in range(n):
        circuit.append(Opcode.RY, [q], [0.1 * (q + 1)])
    circuit.append_channel([0], depolarizing_kraus(0.1))
    for q in range(n - 1):
        circuit.append(Opcode.CX, [q, q + 1])
    return circuit


def noisy_qiskit_chain(n: int) -> tuple[QuantumCircuit, NoiseModel]:
    qc = QuantumCircuit(n)
    for q in range(n):
        qc.ry(0.1 * (q + 1), q)
    for q in range(n - 1):
        qc.cx(q, q + 1)
    model = NoiseModel()
    model.add_all_qubit_quantum_error(depolarizing_error(0.05, 1), ["ry"])
    model.add_all_qubit_quantum_error(amplitude_damping_error(0.05), ["ry"])
    return qc, model


# --- The native options and result field. ---


def test_the_new_options_default_to_phase_12_behavior():
    options = RunOptions()
    assert options.noise_method == NoiseMethod.Exact
    assert options.expectation_precision is None


def test_an_exact_result_has_no_standard_error():
    circuit = noisy_native_chain(2)
    options = RunOptions()
    options.shots = 0
    options.seed = 1
    options.expectation = convert_observable(SparsePauliOp("ZI"), 2)
    result = run(circuit, options)
    assert result.exact
    assert result.expectation_standard_error is None


def test_a_non_positive_precision_is_refused():
    circuit = noisy_native_chain(2)
    options = RunOptions()
    options.shots = 0
    options.seed = 1
    options.expectation = convert_observable(SparsePauliOp("ZI"), 2)
    options.expectation_precision = 0.0
    with pytest.raises(InvalidCircuitError):
        run(circuit, options)


def test_a_cheap_trajectory_plan_is_chosen_and_reported_as_statistical():
    # Eight qubits and a loose precision: N = 100 trajectories of 2^8
    # amplitudes cost far less than a 4^8-entry density matrix.
    n = 8
    circuit = noisy_native_chain(n)
    options = RunOptions()
    options.shots = 0
    options.seed = 7
    options.expectation = convert_observable(SparsePauliOp("Z" + "I" * (n - 1)), n)
    options.expectation_precision = 0.1
    result = run(circuit, options)
    assert "direct_trajectory" in result.explain()
    assert not result.exact
    assert result.expectation_standard_error is not None
    assert 0 < result.expectation_standard_error <= 0.1 * 1.5


def test_counts_with_auto_and_few_shots_run_on_trajectories():
    n = 8
    circuit = noisy_native_chain(n, num_clbits=n)
    for q in range(n):
        circuit.append_measure(q, q)
    options = RunOptions()
    options.shots = 50
    options.seed = 3
    options.noise_method = NoiseMethod.Auto
    result = run(circuit, options)
    assert "direct_trajectory" in result.explain()
    assert not result.exact
    assert sum(result.counts.values()) == 50


# --- MENOSEstimator. ---


def test_the_estimator_reports_genuine_stds_under_noise_and_precision():
    n = 8
    qc, model = noisy_qiskit_chain(n)
    observable = SparsePauliOp("Z" + "I" * (n - 1))  # qubit n - 1, the chain's end
    estimator = MENOSEstimator(noise_model=model, seed=11)
    result = estimator.run([(qc, observable)], precision=0.1).result()[0]
    std = float(result.data.stds)
    value = float(result.data.evs)
    assert std > 0
    assert result.metadata["seed"] == 11
    aer = AerEstimator(
        options={"backend_options": {"noise_model": model, "method": "density_matrix"}}
    )
    exact = float(aer.run([(qc, observable)]).result()[0].data.evs)
    # A single comparison: 5 standard errors is a false-alarm rate below 1e-6.
    assert abs(value - exact) <= 5 * std, f"{value} +/- {std} vs exact {exact}"


def test_an_explicit_seed_makes_the_estimator_reproducible():
    n = 8
    qc, model = noisy_qiskit_chain(n)
    observable = SparsePauliOp("Z" + "I" * (n - 1))
    first = MENOSEstimator(noise_model=model, seed=5).run([(qc, observable)], precision=0.1)
    second = MENOSEstimator(noise_model=model, seed=5).run([(qc, observable)], precision=0.1)
    a, b = first.result()[0], second.result()[0]
    assert float(a.data.stds) > 0, "the pub must actually be sampled by trajectories"
    assert float(a.data.evs) == float(b.data.evs)
    assert float(a.data.stds) == float(b.data.stds)


def test_without_a_seed_a_fresh_one_is_drawn_and_recorded():
    n = 8
    qc, model = noisy_qiskit_chain(n)
    observable = SparsePauliOp("Z" + "I" * (n - 1))
    result = MENOSEstimator(noise_model=model).run([(qc, observable)], precision=0.1).result()[0]
    seed = result.metadata["seed"]
    assert isinstance(seed, int)
    assert float(result.data.stds) > 0, "the pub must actually be sampled by trajectories"
    replay = MENOSEstimator(noise_model=model, seed=seed).run([(qc, observable)], precision=0.1)
    assert float(replay.result()[0].data.evs) == float(result.data.evs)


def test_precision_zero_under_noise_stays_exact_with_zero_stds():
    n = 3
    qc, model = noisy_qiskit_chain(n)
    result = MENOSEstimator(noise_model=model).run([(qc, SparsePauliOp("ZII"))]).result()[0]
    assert float(result.data.stds) == 0.0


# --- MENOSBackend. ---


def test_the_backend_accepts_noise_method_and_rejects_unknown_values():
    n = 3
    qc, model = noisy_qiskit_chain(n)
    qc.measure_all()
    backend = MENOSBackend()
    for method in ("exact", "auto"):
        counts = (
            backend.run(qc, shots=200, seed_simulator=4, noise_model=model, noise_method=method)
            .result()
            .get_counts()
        )
        assert sum(counts.values()) == 200
    with pytest.raises(ValueError, match="noise_method"):
        backend.run(qc, shots=10, seed_simulator=4, noise_model=model, noise_method="fast")


def test_auto_counts_are_deterministic_under_a_seed():
    n = 8
    qc, model = noisy_qiskit_chain(n)
    qc.measure_all()
    backend = MENOSBackend()
    runs = [
        backend.run(qc, shots=40, seed_simulator=9, noise_model=model, noise_method="auto")
        .result()
        .get_counts()
        for _ in range(2)
    ]
    assert runs[0] == runs[1]
