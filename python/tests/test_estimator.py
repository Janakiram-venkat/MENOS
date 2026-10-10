"""docs/BUILD_PLAN.md Phase 5 acceptance criterion 7: MENOSEstimator
results match the C++ path, for one circuit per engine.
"""

from __future__ import annotations

import pytest
from qiskit.circuit import Parameter, QuantumCircuit
from qiskit.quantum_info import SparsePauliOp

from menos._core import RunOptions
from menos._core import run as menos_run
from menos.exceptions import UnsupportedFeatureError
from menos.qiskit import MENOSEstimator
from menos.qiskit.convert import convert_circuit
from menos.qiskit._observable_convert import convert_observable


def _direct_expectation(qc: QuantumCircuit, op: SparsePauliOp) -> complex:
    """The oracle for this criterion: the C++ path itself, not a
    hand-derived value (docs/adr/0008: "the C++ path is the oracle for
    this criterion, matching its literal wording")."""
    circuit = convert_circuit(qc)
    observable = convert_observable(op, qc.num_qubits)
    options = RunOptions()
    options.shots = 0
    options.want_statevector = False
    options.seed = 0
    options.expectation = observable
    return menos_run(circuit, options).expectation_value


def test_dense_engine_matches_direct_run():
    # ccx is neither Clifford nor RZ/RX/RY/P/U -- ineligible for both
    # DirectStabilizer and DirectPauli, forcing DirectDense.
    qc = QuantumCircuit(3)
    qc.h(0)
    qc.ccx(0, 1, 2)
    op = SparsePauliOp(["ZZZ", "XXX"], [1.0, 0.5])

    estimator = MENOSEstimator()
    result = estimator.run([(qc, op)]).result()
    expected = _direct_expectation(qc, op)

    assert result[0].data.evs == pytest.approx(expected.real, abs=1e-12)

    direct_circuit = convert_circuit(qc)
    direct_observable = convert_observable(op, qc.num_qubits)
    options = RunOptions()
    options.shots = 0
    options.want_statevector = False
    options.seed = 0
    options.expectation = direct_observable
    assert "direct_dense" in menos_run(direct_circuit, options).explain()


def test_stabilizer_engine_matches_direct_run():
    # All-Clifford: DirectPauli is ineligible for a fully Clifford region
    # (docs/adr/0007), so this must route to DirectStabilizer.
    qc = QuantumCircuit(2)
    qc.h(0)
    qc.cx(0, 1)
    op = SparsePauliOp(["ZZ"], [1.0])

    estimator = MENOSEstimator()
    result = estimator.run([(qc, op)]).result()
    expected = _direct_expectation(qc, op)

    assert result[0].data.evs == pytest.approx(expected.real, abs=1e-12)

    direct_circuit = convert_circuit(qc)
    direct_observable = convert_observable(op, qc.num_qubits)
    options = RunOptions()
    options.shots = 0
    options.want_statevector = False
    options.seed = 0
    options.expectation = direct_observable
    assert "direct_stabilizer" in menos_run(direct_circuit, options).explain()


def test_pauli_propagation_engine_matches_direct_run():
    # Hand-computable (tests/plan/planner_test.cpp's
    # NonCliffordWithinBoundRoutesToPauliWhenCheaper): 1 qubit, one RZ
    # gate, one-term Z observable -- Pauli propagation's work (2) is
    # strictly cheaper than dense's (8), and RZ is non-Clifford so
    # DirectStabilizer is ineligible, forcing DirectPauli.
    qc = QuantumCircuit(1)
    qc.rz(0.5, 0)
    op = SparsePauliOp(["Z"], [1.0])

    estimator = MENOSEstimator()
    result = estimator.run([(qc, op)]).result()
    expected = _direct_expectation(qc, op)

    assert result[0].data.evs == pytest.approx(expected.real, abs=1e-12)

    direct_circuit = convert_circuit(qc)
    direct_observable = convert_observable(op, qc.num_qubits)
    options = RunOptions()
    options.shots = 0
    options.want_statevector = False
    options.seed = 0
    options.expectation = direct_observable
    assert "direct_pauli" in menos_run(direct_circuit, options).explain()


def test_multiple_observables_in_one_pub():
    qc = QuantumCircuit(2)
    qc.h(0)
    qc.cx(0, 1)
    observables = [SparsePauliOp("ZZ"), SparsePauliOp("XX"), SparsePauliOp("YY")]

    estimator = MENOSEstimator()
    result = estimator.run([(qc, observables)]).result()

    assert result[0].data.evs.shape == (3,)
    for i, op in enumerate(observables):
        expected = _direct_expectation(qc, op)
        assert result[0].data.evs[i] == pytest.approx(expected.real, abs=1e-12)


def test_parameterized_circuit_with_no_bindings_is_rejected():
    # Qiskit's own EstimatorPub.validate() refuses this before
    # MENOSEstimator ever sees it (docs/adr/0008): "the number of values
    # (0) does not match the number of parameters (1)".
    qc = QuantumCircuit(1)
    qc.rz(Parameter("theta"), 0)
    estimator = MENOSEstimator()
    with pytest.raises(ValueError, match="does not match"):
        estimator.run([(qc, SparsePauliOp("Z"))]).result()


def test_parameterized_circuit_with_bindings_is_rejected_by_menos_estimator():
    # Parameter count matches (1 value for 1 parameter), so
    # EstimatorPub.validate() passes; MENOSEstimator's own check in
    # _run_pub is what rejects this one (docs/adr/0008).
    theta = Parameter("theta")
    qc = QuantumCircuit(1)
    qc.rz(theta, 0)
    estimator = MENOSEstimator()
    job = estimator.run([(qc, SparsePauliOp("Z"), [0.5])])
    with pytest.raises(UnsupportedFeatureError):
        job.result()


def test_default_precision_is_recorded_but_not_applied():
    qc = QuantumCircuit(1)
    qc.h(0)
    op = SparsePauliOp("Z")
    estimator = MENOSEstimator()
    result = estimator.run([(qc, op)], precision=0.1).result()
    assert result[0].metadata["target_precision"] == 0.1
    # H|0> gives <Z> = 0 exactly; a nonzero precision must never perturb
    # the exact value MENOS computes.
    assert result[0].data.evs == pytest.approx(0.0, abs=1e-12)
