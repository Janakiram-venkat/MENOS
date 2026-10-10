"""Shared pytest fixtures for the menos.qiskit test suite."""

from __future__ import annotations

import pytest

from menos.qiskit import MENOSBackend


@pytest.fixture
def backend() -> MENOSBackend:
    return MENOSBackend()
