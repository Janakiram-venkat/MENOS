"""PennyLane device ``menos.qubit`` (docs/adr/0024).

Install with ``pip install menos[pennylane]`` (CPython 3.11 or later; the
PennyLane release MENOS is tested with, 0.45.1, does not support 3.10).
"""

from __future__ import annotations

try:
    import pennylane  # noqa: F401
except ImportError as exc:  # pragma: no cover - exercised only without the extra
    raise ImportError(
        "menos.pennylane needs PennyLane: pip install menos[pennylane] "
        "(CPython 3.11 or later)"
    ) from exc

from menos.pennylane.device import MENOSDeviceUnsupportedError, MENOSQubitDevice

__all__ = ["MENOSQubitDevice", "MENOSDeviceUnsupportedError"]
