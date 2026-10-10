"""MENOSBackend(BackendV2): the Qiskit entry point.

Execution path (docs/BUILD_PLAN.md Phase 2, 2.2):
    QuantumCircuit -> convert.py -> menos._core.Circuit
    -> menos._core.run() (Phase 1 C++ planner/runtime)
    -> menos._core.Result -> result_adapter.py -> qiskit.result.Result

No simulation algorithm is reimplemented here.
"""

from __future__ import annotations

import importlib.metadata
import secrets
import uuid

from qiskit.circuit import QuantumCircuit
from qiskit.providers import BackendV2, Options

from menos._core import Backend, NoiseMethod, RunOptions
from menos._core import run as menos_run
from menos.qiskit.convert import convert_circuit
from menos.qiskit.job import MENOSJob
from menos.qiskit.noise import apply_noise_model
from menos.qiskit.result_adapter import build_qiskit_result
from menos.qiskit.target import build_target

_BACKEND_VERSION = importlib.metadata.version("menos")

# The run option's spellings (docs/adr/0017 section 9, Q1).
_NOISE_METHODS = {"exact": NoiseMethod.Exact, "auto": NoiseMethod.Auto}
# Phase 17 (docs/adr/0021 section 2, owner Q2): the backend run option.
_BACKENDS = {"cpu": Backend.Cpu, "cuda": Backend.Cuda}


class MENOSBackend(BackendV2):
    """A Qiskit BackendV2 that executes circuits through the MENOS C++
    runtime.
    """

    def __init__(self, provider=None, **fields) -> None:
        super().__init__(
            provider=provider,
            name="menos",
            description="MENOS dense-engine simulator",
            backend_version=_BACKEND_VERSION,
            **fields,
        )
        self._target = build_target()

    @property
    def target(self):
        return self._target

    @property
    def max_circuits(self):
        # Phase 2 rejects multi-circuit submissions explicitly (below)
        # rather than silently running only the first (docs/BUILD_PLAN.md
        # Phase 2, 2.2).
        return 1

    @classmethod
    def _default_options(cls) -> Options:
        # shots=1024 matches qiskit.providers.basic_provider.BasicSimulator's
        # own default (verified against installed source, docs/adr/0004),
        # not invented. seed_simulator=None: run() generates a fresh seed
        # when unset. want_statevector=False: the explicit statevector-
        # output request this phase's spec calls for (docs/adr/0004);
        # shots=0 with want_statevector=False is rejected by the C++ layer
        # (nothing would be produced), not silently corrected here.
        # noise_model=None: a qiskit_aer.noise.NoiseModel, as AerSimulator
        # accepts it, simulated exactly by the density-matrix engine (Phase 12,
        # docs/adr/0016). It attaches to the instructions of the circuit as
        # submitted, as Aer's does.
        # noise_method="exact": a noisy circuit runs on the exact density-matrix
        # engine, as in Phase 12. "auto" also lets the planner sample it with
        # trajectories when that is cheaper or the only plan that fits (Phase
        # 13, docs/adr/0017). Trajectory counts are independent samples of the
        # same noisy distribution. No effect without a noise model.
        # time_budget_seconds=None: no budget and no run-time monitor, exactly
        # as before Phase 17. A number is RunOptions.time_budget_seconds (docs/
        # adr/0021 section 3): it bounds ONE execution attempt, a run that
        # overruns is replanned, and with no plan left run() raises
        # ResourceExceededError. Abandoned attempts appear in the experiment's
        # metadata under "menos_replans".
        # backend=None: the planner chooses CPU or CUDA, as before. "cpu" or
        # "cuda" forces one (RunOptions.backend); "cuda" raises
        # UnsupportedFeatureError when it cannot be honoured, never falling
        # back to the CPU.
        return Options(
            shots=1024,
            seed_simulator=None,
            want_statevector=False,
            noise_model=None,
            noise_method="exact",
            time_budget_seconds=None,
            backend=None,
        )

    def run(self, run_input, **options) -> MENOSJob:
        if isinstance(run_input, (list, tuple)):
            if len(run_input) != 1:
                raise ValueError(
                    "MENOSBackend.run() does not support batching: pass exactly one "
                    f"QuantumCircuit, not a list of {len(run_input)}."
                )
            run_input = run_input[0]
        if not isinstance(run_input, QuantumCircuit):
            raise TypeError(f"MENOSBackend.run() expects a QuantumCircuit, got {type(run_input)!r}")

        merged = dict(self.options.items())
        for key, value in options.items():
            if key not in merged:
                raise AttributeError(f"Option {key!r} is not valid for this backend")
            merged[key] = value

        shots = merged["shots"]
        seed_simulator = merged["seed_simulator"]
        if seed_simulator is None:
            # A fresh, non-deterministic seed for a caller who didn't ask
            # for reproducibility -- this doesn't weaken MENOS's own
            # determinism invariant (same seed -> same result), which is
            # unaffected; secrets.randbits avoids relying on legacy global
            # RNG state (docs/adr/0004).
            seed_simulator = secrets.randbits(63)

        readout_errors = None
        noise_model = merged["noise_model"]
        if noise_model is not None:
            noisy_input, readout_errors = apply_noise_model(run_input, noise_model)
            circuit = convert_circuit(noisy_input, readout_errors)
        else:
            circuit = convert_circuit(run_input)

        noise_method = merged["noise_method"]
        if noise_method not in _NOISE_METHODS:
            raise ValueError(
                f"noise_method must be one of {sorted(_NOISE_METHODS)}, got {noise_method!r}"
            )

        run_options = RunOptions()
        run_options.shots = shots
        run_options.want_statevector = bool(merged["want_statevector"])
        run_options.seed = seed_simulator
        run_options.noise_method = _NOISE_METHODS[noise_method]
        backend = merged["backend"]
        if backend is not None:
            if backend not in _BACKENDS:
                raise ValueError(
                    f"backend must be None or one of {sorted(_BACKENDS)}, got {backend!r}"
                )
            run_options.backend = _BACKENDS[backend]
        if merged["time_budget_seconds"] is not None:
            # Validated (finite, > 0) by the C++ layer, as for RunOptions.
            run_options.time_budget_seconds = float(merged["time_budget_seconds"])

        menos_result = menos_run(circuit, run_options)

        job_id = str(uuid.uuid4())
        qiskit_result = build_qiskit_result(
            circuit=run_input,
            menos_result=menos_result,
            shots=shots,
            seed_simulator=seed_simulator,
            backend_name=self.name,
            backend_version=self.backend_version,
            job_id=job_id,
            want_statevector=run_options.want_statevector,
        )
        return MENOSJob(self, job_id, qiskit_result)
