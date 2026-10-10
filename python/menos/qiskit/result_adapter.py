"""menos._core.Result -> qiskit.result.Result.

Matches the dict shape qiskit.providers.basic_provider.BasicSimulator
itself builds via qiskit.result.Result.from_dict (verified against
installed Qiskit source, docs/adr/0004) -- not a guessed schema.
"""

from __future__ import annotations

from qiskit.circuit import QuantumCircuit
from qiskit.result import Result as QiskitResult

from menos._core import Result as MenosResult


def build_qiskit_result(
    *,
    circuit: QuantumCircuit,
    menos_result: MenosResult,
    shots: int,
    seed_simulator: int,
    backend_name: str,
    backend_version: str,
    job_id: str,
    want_statevector: bool,
) -> QiskitResult:
    data: dict[str, object] = {}
    if menos_result.counts:
        # Already dict[str, int], hex-formatted in menos._core (Phase 3,
        # docs/adr/0005) -- BitString::to_hex_string() is byte-identical
        # to Python's own hex() on the same represented integer, and
        # Qiskit's own Counts/postprocess machinery does the binary-
        # string, register-aware display formatting from these hex keys;
        # this adapter never formats a binary string itself.
        data["counts"] = menos_result.counts
    if want_statevector:
        data["statevector"] = menos_result.statevector

    header = {
        "name": circuit.name,
        "n_qubits": circuit.num_qubits,
        "qreg_sizes": [[qreg.name, qreg.size] for qreg in circuit.qregs],
        "creg_sizes": [[creg.name, creg.size] for creg in circuit.cregs],
        "qubit_labels": [[qreg.name, j] for qreg in circuit.qregs for j in range(qreg.size)],
        "clbit_labels": [[creg.name, j] for creg in circuit.cregs for j in range(creg.size)],
        "memory_slots": circuit.num_clbits,
        "global_phase": circuit.global_phase,
        "metadata": circuit.metadata if circuit.metadata is not None else {},
    }

    total_time = menos_result.execute_seconds + menos_result.sample_seconds
    experiment_result = {
        "name": circuit.name,
        "seed_simulator": seed_simulator,
        "shots": shots,
        "data": data,
        "status": "DONE",
        "success": True,
        "header": header,
        "time_taken": total_time,
    }
    # Phase 17 (docs/adr/0021 section 3): a job run under time_budget_seconds
    # that abandoned an attempt reports it here, so a replan is never hidden.
    # Only present when there was one, so every other result is unchanged.
    # Reached as result.results[0].metadata["menos_replans"], the place Aer
    # puts its own per-experiment metadata.
    replans = menos_result.replans
    if replans:
        experiment_result["metadata"] = {"menos_replans": replans}

    return QiskitResult.from_dict(
        {
            "backend_name": backend_name,
            "backend_version": backend_version,
            "job_id": job_id,
            "results": [experiment_result],
            "status": "COMPLETED",
            "success": True,
            "time_taken": total_time,
        }
    )
