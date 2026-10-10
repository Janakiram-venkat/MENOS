"""Every script under examples/ must run to completion.

The examples are the code shown in the documentation site (docs/), so they are
tested as part of the ordinary pytest stage of scripts/verify.sh rather than by
a separate mechanism. Each script asserts its own results (mostly against
Qiskit's reference simulator), so a zero exit status is meaningful.
"""

from __future__ import annotations

import subprocess
import sys
from pathlib import Path

import pytest

EXAMPLES_DIR = Path(__file__).resolve().parents[2] / "examples"
EXAMPLES = sorted(EXAMPLES_DIR.glob("*.py"))


def test_the_examples_directory_is_not_empty():
    assert EXAMPLES, f"no examples found under {EXAMPLES_DIR}"


@pytest.mark.parametrize("script", EXAMPLES, ids=lambda path: path.stem)
def test_example_runs(script: Path):
    completed = subprocess.run(
        [sys.executable, str(script)],
        capture_output=True,
        text=True,
        timeout=180,
        check=False,
    )
    assert completed.returncode == 0, (
        f"{script.name} failed with exit status {completed.returncode}\n"
        f"--- stdout ---\n{completed.stdout}\n--- stderr ---\n{completed.stderr}"
    )
