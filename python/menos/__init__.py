"""MENOS: a quantum circuit simulation runtime.

This top-level package exposes the thin C++ core surface
(``menos._core``) directly. See ``menos.qiskit`` for the Qiskit
``BackendV2`` adapter.
"""

from menos._core import (
    Backend,
    Circuit,
    MitigatedResult,
    NoiseMethod,
    Opcode,
    ReadoutMitigationOptions,
    Result,
    RunOptions,
    readout_calibration_circuits,
    run,
    run_batch,
    run_readout_mitigated,
)
from menos.exceptions import (
    InternalError,
    InvalidCircuitError,
    MenosError,
    ResourceExceededError,
    UnsupportedFeatureError,
)

__all__ = [
    "Backend",
    "Circuit",
    "NoiseMethod",
    "Opcode",
    "Result",
    "RunOptions",
    "run",
    "run_batch",
    "ReadoutMitigationOptions",
    "MitigatedResult",
    "readout_calibration_circuits",
    "run_readout_mitigated",
    "MenosError",
    "InvalidCircuitError",
    "UnsupportedFeatureError",
    "ResourceExceededError",
    "InternalError",
]

__version__ = "1.1.0"
