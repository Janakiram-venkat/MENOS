"""Builds the Target MENOSBackend advertises."""

from __future__ import annotations

from qiskit.circuit import Measure
from qiskit.circuit.library import UnitaryGate
from qiskit.circuit.library.standard_gates import get_standard_gate_name_mapping
from qiskit.transpiler import Target

from menos.qiskit._gate_table import GATE_TABLE


def build_target() -> Target:
    """A Target advertising exactly the instructions convert.py can
    execute -- no more, no less (docs/BUILD_PLAN.md Phase 2, 2.4's "Target
    truthfulness" rule). ``num_qubits=None`` matches
    ``qiskit.providers.basic_provider.BasicSimulator``'s own convention for
    a simulator with no fixed device size (verified against installed
    source, docs/adr/0004) -- MENOS has no fixed qubit count either, only a
    memory budget.
    """
    target = Target(description="MENOS dense-engine simulator", num_qubits=None)

    standard_gates = get_standard_gate_name_mapping()
    for name in GATE_TABLE:
        target.add_instruction(standard_gates[name], properties=None, name=name)

    target.add_instruction(Measure(), properties=None, name="measure")
    # A class (not instance) registration, matching BasicSimulator's own
    # pattern: signals the transpiler not to decompose unitaries, without
    # claiming a fixed qubit count for the gate.
    target.add_instruction(UnitaryGate, name="unitary")

    return target
