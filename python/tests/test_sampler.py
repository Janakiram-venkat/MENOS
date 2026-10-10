"""Phase 14: MENOSSampler, batch execution and readout mitigation through
Python (docs/adr/0018).

The statistical gate for mitigation is tested in C++
(tests/api/readout_mitigation_run_test.cpp). These tests cover the Python
surface: the SamplerV2 contract against StatevectorSampler, agreement with Aer's
SamplerV2 under noise, the opt-in mitigation metadata, and the bindings.
"""

from __future__ import annotations

import numpy as np
import pytest
from qiskit import ClassicalRegister, QuantumCircuit, QuantumRegister
from qiskit.circuit import Parameter
from qiskit.primitives import StatevectorSampler
from scipy.stats import chi2

from menos import (
    Circuit,
    Opcode,
    ReadoutMitigationOptions,
    RunOptions,
    run,
    run_batch,
    run_readout_mitigated,
)
from menos._core import derive_seeds
from menos.exceptions import InvalidCircuitError, UnsupportedFeatureError
from menos.qiskit import MENOSSampler

qiskit_aer = pytest.importorskip("qiskit_aer")
from qiskit_aer.noise import NoiseModel, ReadoutError, depolarizing_error  # noqa: E402
from qiskit_aer.primitives import SamplerV2 as AerSampler  # noqa: E402


def two_register_circuit() -> QuantumCircuit:
    """A deterministic circuit over two registers: alpha = 0b01, beta = 0b1."""
    qreg = QuantumRegister(3)
    alpha = ClassicalRegister(2, "alpha")
    beta = ClassicalRegister(1, "beta")
    qc = QuantumCircuit(qreg, alpha, beta)
    qc.x(0)
    qc.x(2)
    qc.measure([0, 1], alpha)
    qc.measure([2], beta)
    return qc


def bell() -> QuantumCircuit:
    qc = QuantumCircuit(2, 2)
    qc.h(0)
    qc.cx(0, 1)
    qc.measure([0, 1], [0, 1])
    return qc


def readout_noise_model(depolarizing: float = 0.0) -> NoiseModel:
    model = NoiseModel()
    model.add_all_qubit_readout_error(ReadoutError([[0.95, 0.05], [0.1, 0.9]]))
    if depolarizing:
        model.add_all_qubit_quantum_error(depolarizing_error(depolarizing, 1), ["h", "x"])
        model.add_all_qubit_quantum_error(depolarizing_error(depolarizing, 2), ["cx"])
    return model


# --- The SamplerV2 contract. ---


def test_the_data_bin_matches_statevector_sampler():
    qc = two_register_circuit()
    ours = MENOSSampler(seed=1).run([qc], shots=100).result()[0]
    reference = StatevectorSampler(seed=1).run([qc], shots=100).result()[0]
    assert sorted(ours.data.keys()) == sorted(reference.data.keys()) == ["alpha", "beta"]
    for name in ("alpha", "beta"):
        ours_bits = getattr(ours.data, name)
        reference_bits = getattr(reference.data, name)
        assert ours_bits.shape == reference_bits.shape == ()
        assert ours_bits.num_bits == reference_bits.num_bits
        assert ours_bits.num_shots == reference_bits.num_shots == 100
        assert ours_bits.get_counts() == reference_bits.get_counts()
    assert ours.data.alpha.get_counts() == {"01": 100}
    assert ours.metadata["shots"] == 100
    assert "readout_mitigation" not in ours.metadata


def test_bell_samples_only_correlated_outcomes():
    result = MENOSSampler(seed=3).run([bell()], shots=2000).result()[0]
    counts = result.data.c.get_counts()
    assert set(counts) == {"00", "11"}
    assert sum(counts.values()) == 2000


def test_default_shots_and_per_pub_shots():
    sampler = MENOSSampler(default_shots=64, seed=2)
    results = sampler.run([bell(), (bell(), None, 10)]).result()
    assert results[0].data.c.num_shots == 64
    assert results[1].data.c.num_shots == 10


def test_an_explicit_seed_is_reproducible_and_a_missing_one_is_recorded():
    a = MENOSSampler(seed=11).run([bell()], shots=500).result()[0]
    b = MENOSSampler(seed=11).run([bell()], shots=500).result()[0]
    assert a.data.c.get_counts() == b.data.c.get_counts()
    assert a.metadata["seed"] == 11
    fresh = MENOSSampler().run([bell()], shots=10).result()[0]
    assert isinstance(fresh.metadata["seed"], int)


def test_pubs_run_under_derived_seeds():
    pubs = MENOSSampler(seed=5).run([bell(), bell()], shots=1000).result()
    seeds = derive_seeds(5, 2)
    for pub, seed in zip(pubs, seeds):
        options = RunOptions()
        options.shots = 1000
        options.seed = seed
        from menos.qiskit.convert import convert_circuit

        expected = run(convert_circuit(bell()), options).counts
        assert sum(pub.data.c.get_counts().values()) == 1000
        assert {int(k, 2): v for k, v in pub.data.c.get_counts().items()} == {
            int(k, 16): v for k, v in expected.items()
        }


def test_parameterized_pubs_are_refused():
    qc = QuantumCircuit(1, 1)
    qc.rx(Parameter("t"), 0)
    qc.measure(0, 0)
    with pytest.raises(UnsupportedFeatureError, match="parameterized"):
        MENOSSampler(seed=1).run([(qc, [0.5])]).result()


# --- Noise: agreement with Aer's SamplerV2 (Phase 14 gate criterion 6). ---


def test_noisy_counts_agree_with_aer_sampler_by_chi_square():
    qc = QuantumCircuit(3, 3)
    qc.h(0)
    qc.cx(0, 1)
    qc.cx(1, 2)
    qc.measure([0, 1, 2], [0, 1, 2])
    model = readout_noise_model(depolarizing=0.05)
    shots = 20000
    ours = MENOSSampler(noise_model=model, seed=21).run([qc], shots=shots).result()[0]
    aer = AerSampler(options={"backend_options": {"noise_model": model, "seed_simulator": 22}})
    theirs = aer.run([qc], shots=shots).result()[0]
    a = ours.data.c.get_counts()
    b = theirs.data.c.get_counts()
    # Two-sample chi-square test of homogeneity, equal sample sizes.
    keys = sorted(set(a) | set(b))
    statistic = sum(
        (a.get(k, 0) - b.get(k, 0)) ** 2 / (a.get(k, 0) + b.get(k, 0)) for k in keys
    )
    critical = chi2.ppf(0.999, len(keys) - 1)
    assert statistic < critical, f"chi2={statistic:.2f} critical={critical:.2f} {a} {b}"


# --- Readout mitigation (opt-in). ---


def tvd(p: dict[str, float], q: dict[str, float]) -> float:
    return 0.5 * sum(abs(p.get(k, 0.0) - q.get(k, 0.0)) for k in set(p) | set(q))


def test_mitigation_metadata_appears_only_on_opt_in_and_moves_toward_the_ideal():
    model = readout_noise_model()
    shots = 20000
    plain = MENOSSampler(noise_model=model, seed=8).run([bell()], shots=shots).result()[0]
    assert "readout_mitigation" not in plain.metadata

    sampler = MENOSSampler(noise_model=model, seed=8, readout_mitigation=True)
    result = sampler.run([bell()], shots=shots).result()[0]
    mitigation = result.metadata["readout_mitigation"]
    assert mitigation["exact"] is False
    assert mitigation["calibration_shots"] == shots
    assert set(mitigation["quasi_dists"]) == {"00", "01", "10", "11"}
    assert set(mitigation["stds"]) == set(mitigation["quasi_dists"])
    assert all(s > 0 for s in mitigation["stds"].values())
    assert mitigation["gamma"] > 1.0
    assert sum(mitigation["quasi_dists"].values()) == pytest.approx(1.0, abs=1e-12)
    assert [c["clbit"] for c in mitigation["calibration"]] == [0, 1]
    assert mitigation["calibration"][0]["p1_given_0"] == pytest.approx(0.05, abs=0.01)
    assert mitigation["calibration"][0]["p0_given_1"] == pytest.approx(0.10, abs=0.01)

    ideal = {"00": 0.5, "11": 0.5}
    raw_counts = result.data.c.get_counts()
    raw = {k: v / shots for k, v in raw_counts.items()}
    assert tvd(mitigation["quasi_dists"], ideal) < tvd(raw, ideal)


def test_a_circuit_without_measurements_cannot_be_mitigated():
    qc = QuantumCircuit(1, 1)
    qc.h(0)
    with pytest.raises(InvalidCircuitError):
        MENOSSampler(seed=1, readout_mitigation=True).run([qc], shots=10).result()


# --- The native bindings. ---


def native_bell(readout=None) -> Circuit:
    circuit = Circuit(2, 2)
    circuit.append(Opcode.H, [0])
    circuit.append(Opcode.CX, [0, 1])
    circuit.append_measure(0, 0, readout)
    circuit.append_measure(1, 1, readout)
    return circuit


def test_run_batch_runs_each_circuit_under_its_derived_seed():
    options = RunOptions()
    options.shots = 500
    options.seed = 42
    results = run_batch([native_bell(), native_bell()], options)
    for result, seed in zip(results, derive_seeds(42, 2)):
        single = RunOptions()
        single.shots = 500
        single.seed = seed
        assert result.counts == run(native_bell(), single).counts


def test_run_readout_mitigated_through_the_binding():
    options = RunOptions()
    options.shots = 5000
    options.seed = 3
    mitigation = ReadoutMitigationOptions()
    mitigation.calibration_shots = 5000
    result = run_readout_mitigated(native_bell([0.9, 0.1, 0.2, 0.8]), None, options, mitigation)
    assert result.exact is False
    assert set(result.quasi_probabilities) == {"0x0", "0x1", "0x2", "0x3"}
    assert result.measured_clbits == [0, 1]
    assert len(result.calibration_results) == 2
    assert sum(result.raw.counts.values()) == 5000
    assert "mitigation" in result.explain()
    assert np.isclose(sum(result.quasi_probabilities.values()), 1.0, atol=1e-12)


def test_readout_calibration_circuits_are_the_default_pair():
    from menos import readout_calibration_circuits

    target = native_bell([0.9, 0.1, 0.2, 0.8])
    zero, one = readout_calibration_circuits(target)
    assert (zero.num_qubits(), zero.num_clbits()) == (2, 2)
    options = RunOptions()
    options.shots = 1000
    options.seed = 9
    mitigation = ReadoutMitigationOptions()
    mitigation.calibration_shots = 1000
    supplied = run_readout_mitigated(target, (zero, one), options, mitigation)
    default = run_readout_mitigated(target, None, options, mitigation)
    assert supplied.quasi_probabilities == default.quasi_probabilities
