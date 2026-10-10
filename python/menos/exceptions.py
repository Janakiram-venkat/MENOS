"""MENOS Python exception hierarchy.

These are not redefined in Python: nanobind creates and registers them
directly against the C++ exception hierarchy (src/python/bindings.cpp),
so a C++ exception thrown by the runtime surfaces as exactly one of these
types with no separate translation step. This module exists only to give
them a stable, documented import path (``menos.exceptions.MenosError``).
"""

from menos._core import (
    InternalError,
    InvalidCircuitError,
    MenosError,
    ResourceExceededError,
    UnsupportedFeatureError,
)

__all__ = [
    "MenosError",
    "InvalidCircuitError",
    "UnsupportedFeatureError",
    "ResourceExceededError",
    "InternalError",
]
