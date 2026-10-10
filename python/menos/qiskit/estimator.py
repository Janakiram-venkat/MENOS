"""MENOSEstimator(BaseEstimatorV2): expectation values through the MENOS
C++ runtime (docs/BUILD_PLAN.md Phase 5, docs/adr/0008).

Execution path per pub, mirroring
qiskit.primitives.StatevectorEstimator's own reference implementation
(read in full from installed Qiskit 2.5.2 source this session):
    EstimatorPub -> convert.py's convert_circuit (reused, not
    duplicated) -> one menos._core.run() call per observable in the
    pub's ObservablesArray, expectation-only.

No parameter binding and no non-Pauli observables -- "Not in this phase"
per docs/BUILD_PLAN.md.

Under a noise model, a pub with precision > 0 may be sampled by trajectories
(Phase 13, docs/adr/0017): the precision becomes RunOptions.
expectation_precision, the planner picks trajectories or the exact density
matrix by cost, and stds holds the measured standard error of a sampled value
(0 for an exact one).
"""

from __future__ import annotations

import secrets

import numpy as np
from qiskit.primitives import (
    BaseEstimatorV2,
    DataBin,
    EstimatorPub,
    PrimitiveJob,
    PrimitiveResult,
    PubResult,
)
from qiskit.quantum_info import SparsePauliOp

from menos._core import RunOptions
from menos._core import run as menos_run
from menos.exceptions import UnsupportedFeatureError
from menos.qiskit._observable_convert import convert_observable
from menos.qiskit.convert import convert_circuit
from menos.qiskit.noise import apply_noise_model


class MENOSEstimator(BaseEstimatorV2):
    """A Qiskit BaseEstimatorV2 that computes expectation values through
    the MENOS C++ runtime.
    """

    def __init__(
        self, *, default_precision: float = 0.0, noise_model=None, seed: int | None = None
    ) -> None:
        # default_precision=0.0 matches StatevectorEstimator's own
        # default (installed Qiskit source, docs/adr/0008). Without a noise
        # model every engine is exact, so a precision is recorded in each
        # PubResult's metadata and the analytic value is returned.
        self._default_precision = default_precision
        # A qiskit_aer.noise.NoiseModel, or None (Phase 12, docs/adr/0016).
        # Readout errors change recorded measurement outcomes only and are not
        # applied to an expectation value, matching
        # qiskit_aer.primitives.EstimatorV2 (measured, docs/adr/0016). With a
        # noise model and precision > 0, a pub may be sampled by trajectories
        # (Phase 13, docs/adr/0017); with precision 0 it is computed exactly
        # by the density-matrix engine and stds stay 0.
        self._noise_model = noise_model
        # The seed for trajectory sampling (owner decision, 2026-10-02). None
        # draws a fresh seed for each run() call; either way the seed used is
        # recorded in every PubResult's metadata, so any run can be reproduced.
        self._seed = seed

    @property
    def default_precision(self) -> float:
        return self._default_precision

    def run(self, pubs, *, precision=None) -> PrimitiveJob:
        if precision is None:
            precision = self._default_precision
        coerced_pubs = [EstimatorPub.coerce(pub, precision) for pub in pubs]
        # A fresh, non-deterministic seed when none was given, as MENOSBackend
        # does for seed_simulator; secrets avoids the legacy global RNG state.
        seed = self._seed if self._seed is not None else secrets.randbits(63)

        job = PrimitiveJob(self._run, coerced_pubs, seed)
        job._submit()  # noqa: SLF001 -- matches StatevectorEstimator.run's own call
        return job

    def _run(self, pubs: list[EstimatorPub], seed: int) -> PrimitiveResult:
        return PrimitiveResult(
            [self._run_pub(pub, seed) for pub in pubs], metadata={"version": 2}
        )

    def _run_pub(self, pub: EstimatorPub, seed: int) -> PubResult:
        circuit = pub.circuit
        if circuit.num_parameters > 0:
            raise UnsupportedFeatureError(
                f"MENOSEstimator does not currently support parameterized circuits "
                f"({circuit.num_parameters} unbound parameter(s)); bind all parameters "
                "first (QuantumCircuit.assign_parameters)."
            )

        if self._noise_model is not None:
            noisy_circuit, _readout_errors = apply_noise_model(circuit, self._noise_model)
            converted_circuit = convert_circuit(noisy_circuit)
        else:
            converted_circuit = convert_circuit(circuit)
        precision = pub.precision
        shape = pub.observables.shape

        # Trajectories are an option only for a noisy pub that asked for a
        # precision; everything else is exact, with a standard error of 0.
        sample_with_precision = self._noise_model is not None and precision > 0
        evs = np.zeros(shape, dtype=np.float64)
        stds = np.zeros(shape, dtype=np.float64)
        for index in np.ndindex(*shape):
            # Each element is a dict[str, complex] mapping a Pauli label
            # to its coefficient (verified against installed Qiskit
            # source this session, docs/adr/0008) -- built into a
            # SparsePauliOp exactly like StatevectorEstimator._run_pub
            # does, so convert_observable's x/z-bit reading applies
            # identically to both paths.
            mapping = pub.observables[index]
            paulis, coeffs = zip(*mapping.items())
            sparse_pauli_op = SparsePauliOp(list(paulis), list(coeffs))
            observable = convert_observable(sparse_pauli_op, circuit.num_qubits)

            run_options = RunOptions()
            run_options.shots = 0
            run_options.want_statevector = False
            run_options.seed = seed
            run_options.expectation = observable
            if sample_with_precision:
                run_options.expectation_precision = precision

            menos_result = menos_run(converted_circuit, run_options)
            # Set only when the planner chose trajectories; an exact result
            # (including a noisy pub the density matrix ran more cheaply) has
            # no sampling error, so its std stays 0.
            if menos_result.expectation_standard_error is not None:
                stds[index] = menos_result.expectation_standard_error
            # StatevectorEstimator's own evs array is float64
            # (np.real_if_close(...) written into a real-dtype array) --
            # MENOS does not validate Hermiticity either (matching
            # SparsePauliOp, docs/BUILD_PLAN.md), so a non-Hermitian
            # observable's imaginary part is dropped here the same way
            # the reference implementation drops it, not a MENOS-specific
            # silent approximation (docs/adr/0008).
            evs[index] = menos_result.expectation_value.real

        data = DataBin(evs=evs, stds=stds, shape=shape)
        return PubResult(
            data,
            metadata={
                "target_precision": precision,
                "circuit_metadata": circuit.metadata,
                "seed": seed,
            },
        )
