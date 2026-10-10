"""Qiskit Aer noise models lowered to MENOS noise (Phase 12, docs/adr/0016).

MENOS's C++ core takes noise in two explicit forms: a channel operation (Kraus
operators) and a readout error on a measurement. This module turns a
``qiskit_aer.noise.NoiseModel`` into those forms, using only Aer's public API:

1. ``qiskit_aer.utils.insert_noise`` places every gate error, applying Aer's own
   rules for which error belongs to which instruction and qubits.
2. A channel that ``insert_noise`` placed directly after a ``measure`` on the
   same qubit is moved to directly before it. ``AerSimulator`` applies an error
   attached to ``measure`` BEFORE the measurement, but ``insert_noise`` writes it
   after; measured on 2026-10-01 to change the counts (docs/adr/0016 section 2,
   fact 4). MENOS matches the simulator.
3. Readout errors are read from the public ``NoiseModel.to_dict()``, a
   per-qubit error taking precedence over the all-qubit one, as in Aer. A
   multi-qubit (correlated) readout error is refused.

Only the verified noise types are accepted (docs/adr/0016 section 9.1); any
other channel raises ``UnsupportedFeatureError`` naming it. ``qiskit-aer`` is
imported only when a noise model or an Aer channel is actually present, so a
default install does not need it.
"""

from __future__ import annotations

import numpy as np
from qiskit.circuit import QuantumCircuit

from menos.exceptions import UnsupportedFeatureError

#: Entrywise tolerance on a channel's Pauli transfer matrix when deciding its
#: type: the same 1e-12 the C++ core uses for trace preservation and unitarity
#: (common::kDefaultAbsTol, docs/adr/0016 section 9, decision 2).
TYPE_TOLERANCE = 1e-12

#: The noise types accepted in this release, as named in error messages.
VERIFIED_TYPES = (
    "depolarizing on 1 or 2 qubits, amplitude damping (excited-state population 0), "
    "or a one-qubit composition of one depolarizing and one amplitude-damping error"
)

#: Name of the instruction Aer uses for an inserted or appended error.
CHANNEL_INSTRUCTION_NAME = "quantum_channel"


def _aer():
    try:
        import qiskit_aer.noise  # noqa: PLC0415 -- optional dependency, imported on use
        import qiskit_aer.utils  # noqa: PLC0415
    except ImportError as exc:
        raise ImportError(
            "Noise models need qiskit-aer: install it with pip install 'menos[noise]'"
        ) from exc
    return qiskit_aer


def apply_noise_model(
    qc: QuantumCircuit, noise_model
) -> tuple[QuantumCircuit, dict[int, list[float]]]:
    """Returns ``qc`` with the model's gate errors inserted as channel
    instructions, and its readout errors keyed by qubit index as
    ``[P(0|0), P(1|0), P(0|1), P(1|1)]``.
    """
    aer = _aer()
    if not isinstance(noise_model, aer.noise.NoiseModel):
        raise TypeError(
            f"noise_model must be a qiskit_aer.noise.NoiseModel, got {type(noise_model)!r}"
        )
    noisy = aer.utils.insert_noise(qc, noise_model, transpile=False)
    return _move_measure_errors_before_measurement(noisy), _readout_errors(noise_model, qc)


def _move_measure_errors_before_measurement(qc: QuantumCircuit) -> QuantumCircuit:
    reordered = qc.copy_empty_like()
    data = []
    for instruction in qc.data:
        previous = data[-1] if data else None
        if (
            instruction.operation.name == CHANNEL_INSTRUCTION_NAME
            and previous is not None
            and previous.operation.name == "measure"
            and tuple(previous.qubits) == tuple(instruction.qubits)
        ):
            data.insert(len(data) - 1, instruction)
        else:
            data.append(instruction)
    for instruction in data:
        reordered.append(instruction)
    return reordered


def _readout_errors(noise_model, qc: QuantumCircuit) -> dict[int, list[float]]:
    default: list[float] | None = None
    per_qubit: dict[int, list[float]] = {}
    for error in noise_model.to_dict()["errors"]:
        if error["type"] != "roerror":
            continue
        probabilities = [[float(p) for p in row] for row in error["probabilities"]]
        if len(probabilities) != 2:
            raise UnsupportedFeatureError(
                f"a {len(probabilities).bit_length() - 1}-qubit (correlated) readout error is not "
                "supported; only single-qubit readout errors are"
            )
        flat = probabilities[0] + probabilities[1]
        if "gate_qubits" in error:
            for qubits in error["gate_qubits"]:
                per_qubit[int(qubits[0])] = flat
        else:
            default = flat
    if default is None:
        return per_qubit
    readout = {q: default for q in range(qc.num_qubits)}
    readout.update(per_qubit)
    return readout


def channel_kraus(operation) -> list[np.ndarray]:
    """The Kraus operators of an Aer channel instruction, as 2^k x 2^k arrays
    in Qiskit's little-endian order (bit j of an index is the instruction's
    j-th qubit, the same rule as MENOS's operand order).
    """
    from qiskit.quantum_info import Kraus  # noqa: PLC0415

    _aer()
    return [np.asarray(k, dtype=complex) for k in Kraus(operation).data]


def verified_type(kraus: list[np.ndarray], num_qubits: int) -> str | None:
    """The verified noise type of a channel, or None if it is not one
    (docs/adr/0016 section 9.1), decided from its Pauli transfer matrix R:

    (a) depolarizing on 1 or 2 qubits: R = diag(1, f, ..., f);
    (b) one-qubit amplitude damping: R = [[1,0,0,0],[0,a,0,0],[0,0,a,0],[t,0,0,b]]
        with t = gamma, a = sqrt(1 - gamma), b = 1 - gamma;
    (c) one depolarizing (f) and one amplitude damping (gamma) composed on one
        qubit: the form of (b) with a = f sqrt(1 - gamma), b = f (1 - gamma) and
        t = gamma (damping applied last) or t = f gamma (depolarizing last).
    """
    from qiskit.quantum_info import PTM, Kraus  # noqa: PLC0415

    r = PTM(Kraus([np.asarray(k) for k in kraus])).data
    if np.max(np.abs(np.imag(r))) > TYPE_TOLERANCE:
        return None
    r = np.real(r)

    def close(x: float, y: float) -> bool:
        return abs(x - y) <= TYPE_TOLERANCE

    off_diagonal = r - np.diag(np.diag(r))
    if num_qubits in (1, 2) and np.max(np.abs(off_diagonal)) <= TYPE_TOLERANCE:
        diagonal = np.diag(r)
        f = diagonal[1]
        if close(diagonal[0], 1.0) and all(close(d, f) for d in diagonal[1:]):
            return "depolarizing"
    if num_qubits != 1:
        return None

    # The one-qubit damping form: everything but (0,0), (1,1), (2,2), (3,3) and
    # (3,0) is zero, and (1,1) == (2,2).
    mask = np.ones((4, 4), dtype=bool)
    for i, j in ((0, 0), (1, 1), (2, 2), (3, 3), (3, 0)):
        mask[i, j] = False
    if np.max(np.abs(r[mask])) > TYPE_TOLERANCE or not close(r[0, 0], 1.0):
        return None
    if not close(r[1, 1], r[2, 2]):
        return None
    a, b, t = r[1, 1], r[3, 3], r[3, 0]

    def is_damping_then(f: float, gamma: float) -> bool:
        in_range = -1.0 / 3.0 - TYPE_TOLERANCE <= f <= 1.0 + TYPE_TOLERANCE and (
            -TYPE_TOLERANCE <= gamma <= 1.0 + TYPE_TOLERANCE
        )
        root = np.sqrt(max(0.0, 1.0 - gamma))
        return in_range and close(a, f * root) and close(b, f * (1.0 - gamma))

    gamma = t
    if close(a, np.sqrt(max(0.0, 1.0 - gamma))) and close(b, 1.0 - gamma):
        return "amplitude damping"
    # Damping applied last: t = gamma, b = f (1 - gamma).
    if gamma < 1.0 - TYPE_TOLERANCE and is_damping_then(b / (1.0 - gamma), gamma):
        return "depolarizing then amplitude damping"
    # Depolarizing applied last: t = f gamma, b = f (1 - gamma), so f = t + b.
    f = t + b
    if abs(f) > TYPE_TOLERANCE and is_damping_then(f, t / f):
        return "amplitude damping then depolarizing"
    return None
