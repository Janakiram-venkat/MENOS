"""MENOSQubitDevice: the PennyLane device ``menos.qubit`` (docs/adr/0024).

Execution path:
    PennyLane tape -> preprocess (PennyLane's own transforms)
    -> _convert.tape_to_circuit -> menos._core.run() (plan_job, one job per
    required run) -> menos._core.Result -> PennyLane result shapes.

No simulation algorithm is reimplemented here, and the device never chooses
an engine: every run goes through the MENOS planner on its own.
"""

from __future__ import annotations

import numpy as np
import pennylane as qml
from pennylane.devices import Device, ExecutionConfig
from pennylane.devices.modifiers import simulator_tracking, single_tape_support
from pennylane.devices.preprocess import decompose, validate_device_wires
from pennylane.exceptions import DeviceError
from pennylane.measurements import (
    CountsMP,
    ExpectationMP,
    MidMeasureMP,
    ProbabilityMP,
    SampleMP,
    StateMP,
    VarianceMP,
)
from pennylane.ops import Conditional, MidMeasure
from pennylane.transforms import broadcast_expand, diagonalize_measurements, split_non_commuting
from pennylane.transforms.core import CompilePipeline

from menos._core import Backend, NoiseMethod, RunOptions
from menos._core import run as menos_run
from menos.exceptions import InternalError, UnsupportedFeatureError
from menos.pennylane._convert import (
    counts_to_samples,
    is_noisy,
    is_supported_operation,
    pauli_sentence_to_observable,
    statevector_to_big_endian,
    tape_to_circuit,
)

_NAME = "menos.qubit"
# The same spellings as MENOSBackend's options (docs/adr/0017, docs/adr/0021).
_NOISE_METHODS = {"exact": NoiseMethod.Exact, "auto": NoiseMethod.Auto}
_BACKENDS = {"cpu": Backend.Cpu, "cuda": Backend.Cuda}


class MENOSDeviceUnsupportedError(UnsupportedFeatureError, DeviceError):
    """A construct ``menos.qubit`` cannot run exactly. It is both MENOS's
    ``UnsupportedFeatureError`` and PennyLane's ``DeviceError``
    (docs/adr/0024 section 9, Q9), so either can catch it."""


def _refuse(what: str):
    raise MENOSDeviceUnsupportedError(f"{what} is not supported by {_NAME}")


def _non_pauli_factor(obs):
    """The first factor of ``obs`` with no Pauli representation, so a
    refusal names it rather than the product or sum around it."""
    for child in getattr(obs, "operands", None) or ([obs.base] if hasattr(obs, "base") else []):
        if child.pauli_rep is None:
            return _non_pauli_factor(child)
    return obs


@qml.transform
def _refuse_unsupported(tape):
    """Refuse, by name, what the device does not run (docs/adr/0024 section 2.2)."""
    for op in tape.operations:
        if isinstance(op, (MidMeasure, Conditional)):
            _refuse(f"{op.name} (mid-circuit measurement or classical control; Phase 24)")
        if op.name == "Snapshot":
            _refuse("Snapshot")
    for mp in tape.measurements:
        if isinstance(mp, MidMeasureMP) or mp.mv is not None:
            _refuse(f"{type(mp).__name__} of a mid-circuit measurement (Phase 24)")
        if tape.shots:
            allowed = (ExpectationMP, VarianceMP, ProbabilityMP, SampleMP, CountsMP)
            ok = type(mp) in allowed
        else:
            ok = type(mp) in (ExpectationMP, VarianceMP, ProbabilityMP, StateMP)
        if not ok and tape.shots:
            # PennyLane's own wording for this case (its device test suite).
            raise MENOSDeviceUnsupportedError(
                f"measurement {type(mp).__name__} is not accepted with finite shots by {_NAME}"
            )
        if not ok:
            _refuse(f"measurement {type(mp).__name__} with shots=None")
        if mp.obs is not None and mp.obs.pauli_rep is None:
            factor = _non_pauli_factor(mp.obs)
            _refuse(f"observable {factor.name} (no Pauli representation) in {mp.obs!r}")
    return (tape,), lambda results: results[0]


@qml.transform
def _diagonalize_for_shots(tape):
    """With finite shots, rewrite every measurement into one diagonal basis
    with PennyLane's own transforms, so all of them come from one counts run
    per shot bin (docs/adr/0024 section 3.5)."""
    if not tape.shots:
        return (tape,), lambda results: results[0]
    split_tapes, split_fn = split_non_commuting(tape)
    tapes = []
    fns = []
    for t in split_tapes:
        diag_tapes, diag_fn = diagonalize_measurements(t)
        tapes.extend(diag_tapes)
        fns.append((len(diag_tapes), diag_fn))

    def postprocess(results):
        out = []
        start = 0
        for count, fn in fns:
            out.append(fn(results[start : start + count]))
            start += count
        return split_fn(out)

    return tapes, postprocess


@simulator_tracking
@single_tape_support
class MENOSQubitDevice(Device):
    """A PennyLane device that runs circuits through the MENOS planner.

    Args:
        wires: number of wires or an iterable of wire labels; ``None`` lets
            each circuit use its own wires.
        shots: deprecated by PennyLane on devices (use ``qml.set_shots``);
            accepted only for PennyLane's device test suite.
        seed: ``int`` or ``None``. One NumPy generator per device draws the
            seed of every run with shots, in tape order then shot-bin order;
            ``None`` seeds it from OS entropy (docs/adr/0024 section 3.7).
        time_budget_seconds: ``RunOptions.time_budget_seconds`` for every run.
        backend: ``None``, ``"cpu"`` or ``"cuda"``, as ``MENOSBackend``'s.
        noise_method: ``"exact"`` (density matrix) or ``"auto"`` (the planner
            may also sample noisy counts by trajectories). Analytic results
            are always exact and ignore it.
    """

    name = _NAME

    def __init__(
        self,
        wires=None,
        shots=None,
        seed=None,
        time_budget_seconds=None,
        backend=None,
        noise_method="exact",
    ):
        super().__init__(wires=wires, shots=shots)
        if seed is not None and not isinstance(seed, (int, np.integer)):
            raise TypeError(f"seed must be an int or None, got {type(seed).__name__}")
        if backend is not None and backend not in _BACKENDS:
            raise ValueError(f"backend must be None or one of {sorted(_BACKENDS)}, got {backend!r}")
        if noise_method not in _NOISE_METHODS:
            raise ValueError(
                f"noise_method must be one of {sorted(_NOISE_METHODS)}, got {noise_method!r}"
            )
        self._rng = np.random.default_rng(seed)
        self._time_budget_seconds = time_budget_seconds
        self._backend = backend
        self._noise_method = noise_method

    def preprocess_transforms(self, execution_config: ExecutionConfig | None = None):
        program = CompilePipeline()
        program.add_transform(validate_device_wires, wires=self.wires, name=_NAME)
        program.add_transform(_refuse_unsupported)
        program.add_transform(
            decompose,
            stopping_condition=is_supported_operation,
            skip_initial_state_prep=False,
            name=_NAME,
            error=MENOSDeviceUnsupportedError,
        )
        program.add_transform(_diagonalize_for_shots)
        program.add_transform(broadcast_expand)
        return program

    def execute(self, circuits, execution_config: ExecutionConfig | None = None):
        return tuple(self._execute_tape(tape) for tape in circuits)

    # -- one tape ---------------------------------------------------------

    def _options(self) -> RunOptions:
        options = RunOptions()
        if self._backend is not None:
            options.backend = _BACKENDS[self._backend]
        if self._time_budget_seconds is not None:
            # Validated (finite, > 0) by the C++ layer, as for RunOptions.
            options.time_budget_seconds = float(self._time_budget_seconds)
        return options

    def _wire_order(self, tape):
        return self.wires if self.wires is not None else tape.wires

    def _execute_tape(self, tape):
        if tape.shots:
            return self._execute_shots(tape)
        return self._execute_analytic(tape)

    def _execute_analytic(self, tape):
        wire_order = self._wire_order(tape)
        noisy = is_noisy(tape.operations)
        circuit = tape_to_circuit(tape.operations, wire_order, measure=False)
        state = None
        results = []
        for mp in tape.measurements:
            if isinstance(mp, (StateMP, ProbabilityMP)) and noisy:
                _refuse(f"{type(mp).__name__} of a noisy circuit with shots=None")
            if isinstance(mp, ProbabilityMP) and mp.obs is not None:
                # Probabilities in the observable's eigenbasis: its own
                # (exact) diagonalizing gates, then the computational-basis
                # probabilities of its wires, as default.qubit computes them.
                rotated = tape_to_circuit(
                    [*tape.operations, *mp.obs.diagonalizing_gates()], wire_order, measure=False
                )
                options = self._options()
                options.want_statevector = True
                rotated_state = statevector_to_big_endian(
                    self._run_exact(rotated, options).statevector
                )
                results.append(qml.probs(wires=mp.wires).process_state(rotated_state, wire_order))
            elif isinstance(mp, (StateMP, ProbabilityMP)):
                if state is None:
                    options = self._options()
                    options.want_statevector = True
                    state = statevector_to_big_endian(self._run_exact(circuit, options).statevector)
                results.append(mp.process_state(state, wire_order))
            elif isinstance(mp, ExpectationMP):
                results.append(self._expectation(circuit, mp.obs.pauli_rep, wire_order))
            else:  # VarianceMP
                sentence = mp.obs.pauli_rep
                mean = self._expectation(circuit, sentence, wire_order)
                square = self._expectation(circuit, sentence @ sentence, wire_order)
                # <O^2> - <O>^2, O^2 by exact PauliSentence algebra (Q6).
                results.append(np.float64(square - mean * mean))
        return results[0] if len(results) == 1 else tuple(results)

    def _expectation(self, circuit, sentence, wire_order) -> np.float64:
        observable, identity = pauli_sentence_to_observable(sentence, wire_order)
        value = identity
        if observable is not None:
            options = self._options()
            options.expectation = observable
            value += self._run_exact(circuit, options).expectation_value
        return np.float64(value.real)

    def _run_exact(self, circuit, options):
        # shots=None never requests trajectories (NoiseMethod.Exact, no
        # expectation_precision), so a non-exact result here would be a
        # MENOS defect, never a value to return as analytic (Q3).
        options.noise_method = NoiseMethod.Exact
        result = menos_run(circuit, options)
        if not result.exact or result.expectation_standard_error is not None:
            raise InternalError(
                f"{_NAME}: MENOS returned a non-exact result for an analytic "
                "(shots=None) execution; refusing to return it"
            )
        return result

    def _execute_shots(self, tape):
        wire_order = self._wire_order(tape)
        n = len(wire_order)
        circuit = tape_to_circuit(tape.operations, wire_order, measure=True)
        bins = []
        for lower, upper in tape.shots.bins():
            seed = int(self._rng.integers(0, 2**63))
            options = self._options()
            options.shots = upper - lower
            options.seed = seed
            options.noise_method = _NOISE_METHODS[self._noise_method]
            counts = menos_run(circuit, options).counts
            samples = counts_to_samples(counts, n, np.random.default_rng(seed))
            values = tuple(mp.process_samples(samples, wire_order) for mp in tape.measurements)
            bins.append(values[0] if len(values) == 1 else values)
        return tuple(bins) if tape.shots.has_partitioned_shots else bins[0]
