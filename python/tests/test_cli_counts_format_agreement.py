"""CLAUDE.md: "Human-readable string formatting (highest classical bit
first) exists only in the Qiskit adapter and the CLI (Phase 7); each
implements it independently and a test checks they agree." The Qiskit
adapter itself never formats a binary string (docs/adr/0004: it hands
Qiskit hex-keyed data and Qiskit's own Counts/postprocess machinery does
the binary-string display formatting); the CLI (cli/cli_impl.cpp's
format_counts_key) is the second, independent implementation this test
checks against the first.

Runs the built `menos` CLI binary as a subprocess against the same
circuit, converted to OpenQASM 2 text, that is also run directly
through MENOSBackend -- since both paths ultimately call the same
menos::run() on an equivalent Circuit with the same seed, MENOS's own
determinism guarantee means the counts dictionaries should agree
exactly (keys and values), not just their formatting convention.
"""

from __future__ import annotations

import subprocess
from pathlib import Path

import pytest
from qiskit import QuantumCircuit, qasm2

REPO_ROOT = Path(__file__).resolve().parents[2]


def _find_cli_binary() -> Path | None:
    for preset in ("dev", "release"):
        candidate = REPO_ROOT / "build" / preset / "cli" / "menos"
        if candidate.is_file():
            return candidate
    return None


def _run_cli_counts(binary: Path, qasm_path: Path, shots: int, seed: int) -> dict[str, int]:
    result = subprocess.run(
        [str(binary), "run", str(qasm_path), "--shots", str(shots), "--seed", str(seed)],
        capture_output=True,
        text=True,
        check=True,
    )
    counts: dict[str, int] = {}
    for line in result.stdout.splitlines():
        # Count lines are the only ones shaped "<binary key>: <count>" --
        # every other line (report header, labeled report fields) has a
        # non-binary-digit label before the colon.
        if ":" not in line:
            continue
        key, _, value = line.partition(":")
        if key and all(c in "01" for c in key):
            counts[key] = int(value.strip())
    return counts


def test_cli_and_qiskit_adapter_counts_agree(tmp_path, backend):
    cli_binary = _find_cli_binary()
    if cli_binary is None:
        pytest.skip("build/{dev,release}/cli/menos not built -- run the CMake dev or release "
                    "preset before this test (scripts/verify.sh already does)")

    qc = QuantumCircuit(3, 3)
    qc.h(0)
    qc.cx(0, 1)
    qc.t(2)
    qc.h(2)
    qc.measure([0, 1, 2], [0, 1, 2])

    shots = 200
    seed = 12345

    qiskit_counts = backend.run(qc, shots=shots, seed_simulator=seed).result().get_counts()

    qasm_path = tmp_path / "circuit.qasm"
    qasm_path.write_text(qasm2.dumps(qc))
    cli_counts = _run_cli_counts(cli_binary, qasm_path, shots, seed)

    assert cli_counts, "expected at least one counts line from the CLI"
    assert cli_counts == qiskit_counts
