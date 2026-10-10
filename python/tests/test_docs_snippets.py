"""Every runnable Python snippet in the documentation must execute.

Fenced ```python blocks under docs/ and in the root README.md are extracted and run, each in its own
interpreter, so a documented example cannot drift from the implementation. A
block that is deliberately not runnable (a fragment, or a command that needs
hardware this machine may not have) is marked ```python no-run and skipped.
Snippets that are pulled in from examples/ with the documentation site's file
include are covered by test_examples.py instead.
"""

from __future__ import annotations

import re
import subprocess
import sys
from pathlib import Path

import pytest

REPO_ROOT = Path(__file__).resolve().parents[2]
DOCS_DIR = REPO_ROOT / "docs"
README = REPO_ROOT / "README.md"
EXCLUDED_PARTS = {"node_modules", "adr", ".vitepress"}
FENCE = re.compile(r"^```python([^\n]*)\n(.*?)^```", re.DOTALL | re.MULTILINE)


def _collect():
    snippets = []
    if not DOCS_DIR.is_dir():
        return snippets
    pages = [(page, page.relative_to(DOCS_DIR)) for page in sorted(DOCS_DIR.rglob("*.md"))]
    pages.append((README, README.relative_to(REPO_ROOT)))  # the GitHub landing page too
    for page, relative in pages:
        if EXCLUDED_PARTS & set(relative.parts) or relative.name == "BUILD_PLAN.md":
            continue
        for index, match in enumerate(FENCE.finditer(page.read_text(encoding="utf-8"))):
            info, body = match.group(1).strip(), match.group(2)
            if "no-run" in info.split():
                continue
            snippets.append(pytest.param(body, id=f"{relative.as_posix()}#{index}"))
    return snippets


SNIPPETS = _collect()


def test_documentation_contains_runnable_snippets():
    assert SNIPPETS, f"no runnable python snippets found under {DOCS_DIR}"


@pytest.mark.parametrize("source", SNIPPETS)
def test_snippet_runs(source: str):
    completed = subprocess.run(
        [sys.executable, "-c", source],
        capture_output=True,
        text=True,
        timeout=180,
        check=False,
        cwd=DOCS_DIR.parent,  # snippets may read repo-relative paths
    )
    assert completed.returncode == 0, (
        f"documentation snippet failed with exit status {completed.returncode}\n"
        f"--- snippet ---\n{source}\n--- stderr ---\n{completed.stderr}"
    )
