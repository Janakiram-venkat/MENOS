"""MENOSSampler(BaseSamplerV2): measurement samples through the MENOS C++
runtime, with optional readout-error mitigation (Phase 14, docs/adr/0018).

Execution path per pub: SamplerPub -> (noise model lowered by
``apply_noise_model``, as ``MENOSBackend`` does) -> ``convert_circuit`` ->
``menos._core.run`` for counts, or ``menos._core.run_readout_mitigated`` when
mitigation is on. Each pub's counts become one ``BitArray`` per classical
register, in the layout ``qiskit.primitives.StatevectorSampler`` produces (read
from installed Qiskit 2.5.2 this session).

MENOS returns counts, not a per-shot memory, so each ``BitArray``'s shots are
listed in ascending outcome order rather than in sampling order. Every
count-based reading (``get_counts``, ``get_bitstrings`` as a multiset,
expectation values) is unaffected; nothing reorders them at random, because
that would add randomness MENOS did not produce.

Mitigation is explicit opt-in (``readout_mitigation=True``). The standard
``DataBin`` stays the raw samples; the mitigated quasi-distribution, its
standard errors, gamma and the calibration go in
``PubResult.metadata["readout_mitigation"]`` (docs/adr/0018 section 9, Q3).
The calibration circuits are lowered through the same noise model as the
target, so the X preparations carry gate error, as on hardware (Q1).

No parameter binding, as in ``MENOSEstimator``.
"""

from __future__ import annotations

import secrets

from qiskit.circuit import QuantumCircuit
from qiskit.primitives import (
    BaseSamplerV2,
    BitArray,
    DataBin,
    PrimitiveJob,
    PrimitiveResult,
    SamplerPub,
    SamplerPubResult,
)

from menos._core import ReadoutMitigationOptions, RunOptions, derive_seeds, run_readout_mitigated
from menos._core import run as menos_run
from menos.exceptions import UnsupportedFeatureError
from menos.qiskit.convert import convert_circuit
from menos.qiskit.noise import apply_noise_model


class MENOSSampler(BaseSamplerV2):
    """A Qiskit BaseSamplerV2 that samples circuits through the MENOS C++
    runtime.
    """

    def __init__(
        self,
        *,
        default_shots: int = 1024,
        noise_model=None,
        seed: int | None = None,
        readout_mitigation: bool = False,
    ) -> None:
        # default_shots=1024 matches StatevectorSampler's own default and
        # MENOSBackend's (docs/adr/0004).
        self._default_shots = default_shots
        # A qiskit_aer.noise.NoiseModel, or None (docs/adr/0016). Its readout
        # errors apply to the samples, as in MENOSBackend.
        self._noise_model = noise_model
        # None draws a fresh seed for each run() call; the seed used is recorded
        # in every PubResult's metadata, as MENOSEstimator does.
        self._seed = seed
        # Explicit opt-in to readout mitigation (docs/adr/0018). Calibration
        # uses each pub's own shot count (Q2).
        self._readout_mitigation = readout_mitigation

    @property
    def default_shots(self) -> int:
        return self._default_shots

    def run(self, pubs, *, shots=None) -> PrimitiveJob:
        if shots is None:
            shots = self._default_shots
        coerced_pubs = [SamplerPub.coerce(pub, shots) for pub in pubs]
        seed = self._seed if self._seed is not None else secrets.randbits(63)
        job = PrimitiveJob(self._run, coerced_pubs, seed)
        job._submit()  # noqa: SLF001 -- matches StatevectorSampler.run's own call
        return job

    def _run(self, pubs: list[SamplerPub], seed: int) -> PrimitiveResult:
        # Pub j runs under the j-th SplitMix64 output of the job seed, the rule
        # run_batch uses for its circuits (docs/adr/0018 section 2).
        pub_seeds = derive_seeds(seed, len(pubs))
        return PrimitiveResult(
            [self._run_pub(pub, seed, pub_seed) for pub, pub_seed in zip(pubs, pub_seeds)],
            metadata={"version": 2},
        )

    def _lower(self, circuit: QuantumCircuit):
        if self._noise_model is None:
            return convert_circuit(circuit)
        noisy, readout_errors = apply_noise_model(circuit, self._noise_model)
        return convert_circuit(noisy, readout_errors)

    def _run_pub(self, pub: SamplerPub, seed: int, pub_seed: int) -> SamplerPubResult:
        circuit = pub.circuit
        if circuit.num_parameters > 0:
            raise UnsupportedFeatureError(
                f"MENOSSampler does not currently support parameterized circuits "
                f"({circuit.num_parameters} unbound parameter(s)); bind all parameters "
                "first (QuantumCircuit.assign_parameters)."
            )

        options = RunOptions()
        options.shots = pub.shots
        options.want_statevector = False
        options.seed = pub_seed

        metadata = {"shots": pub.shots, "circuit_metadata": circuit.metadata, "seed": seed}
        target = self._lower(circuit)
        if self._readout_mitigation:
            zero, one = _calibration_circuits(circuit)
            mitigation = ReadoutMitigationOptions()
            mitigation.calibration_shots = pub.shots
            mitigated = run_readout_mitigated(
                target, (self._lower(zero), self._lower(one)), options, mitigation
            )
            counts = mitigated.raw.counts
            metadata["readout_mitigation"] = _mitigation_metadata(mitigated, circuit.num_clbits)
        else:
            counts = menos_run(target, options).counts

        return SamplerPubResult(_data_bin(circuit, counts, pub.shape), metadata=metadata)


def _surviving_measurements(circuit: QuantumCircuit) -> dict[int, int]:
    """clbit index -> qubit index of the last measurement into each clbit, the
    rule MENOS applies (ir::compute_final_writes)."""
    pairs: dict[int, int] = {}
    for instruction in circuit.data:
        if instruction.operation.name == "measure":
            clbit = circuit.find_bit(instruction.clbits[0]).index
            pairs[clbit] = circuit.find_bit(instruction.qubits[0]).index
    return pairs


def _calibration_circuits(circuit: QuantumCircuit) -> tuple[QuantumCircuit, QuantumCircuit]:
    """The all-zero and all-one calibration circuits on circuit's registers,
    measuring its surviving (qubit, clbit) pairs (docs/adr/0018 section 3)."""
    pairs = sorted(_surviving_measurements(circuit).items())
    zero = circuit.copy_empty_like(name=f"{circuit.name}_cal_0")
    one = circuit.copy_empty_like(name=f"{circuit.name}_cal_1")
    for _, qubit in pairs:
        one.x(qubit)
    for calibration in (zero, one):
        for clbit, qubit in pairs:
            calibration.measure(qubit, clbit)
    return zero, one


def _binary_key(hex_key: str, num_clbits: int) -> str:
    return format(int(hex_key, 16), f"0{num_clbits}b") if num_clbits else ""


def _mitigation_metadata(mitigated, num_clbits: int) -> dict:
    return {
        "quasi_dists": {
            _binary_key(k, num_clbits): v for k, v in mitigated.quasi_probabilities.items()
        },
        "stds": {_binary_key(k, num_clbits): v for k, v in mitigated.standard_errors.items()},
        "gamma": mitigated.gamma,
        "calibration_shots": mitigated.calibration_shots,
        "calibration": [
            {"clbit": clbit, "p1_given_0": e0, "p0_given_1": e1}
            for clbit, e0, e1 in mitigated.calibration
        ],
        "exact": mitigated.exact,
    }


def _data_bin(circuit: QuantumCircuit, counts: dict[str, int], shape) -> DataBin:
    """One BitArray per classical register, as StatevectorSampler builds them.
    Bit j of a register's value is the register's j-th clbit."""
    outcomes = sorted((int(key, 16), count) for key, count in counts.items())
    arrays = {}
    for creg in circuit.cregs:
        indices = [circuit.find_bit(bit).index for bit in creg]
        samples = []
        for value, count in outcomes:
            register_value = sum(((value >> index) & 1) << j for j, index in enumerate(indices))
            samples.extend([register_value] * count)
        arrays[creg.name] = BitArray.from_samples(samples, num_bits=creg.size)
    return DataBin(**arrays, shape=shape)
