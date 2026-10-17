"""docs/adr/0024 gate criterion 8: PennyLane's own device test suite
(pennylane.devices.tests) passes for the supported features.

The suite is run in a subprocess, with shots=None and with finite shots,
exactly as `pl-device-test --device menos.qubit --skip-ops` runs it. Its
tests of constructs the device refuses by design (docs/adr/0024 section 2.2)
fail with MENOSDeviceUnsupportedError naming the construct; each such
failure must name one of REFUSED_BY_DESIGN, and any other failure or error
fails this test. The outcome counts are printed for the gate evidence.
"""

from __future__ import annotations

import os
import subprocess
import sys
import xml.etree.ElementTree as ET

import pytest

pytest.importorskip("pennylane")
pytest.importorskip("pytest_mock")  # the suite's own requirements
pytest.importorskip("flaky")

from pennylane.devices.tests import get_device_tests  # noqa: E402

# Observables without a Pauli representation, Snapshot, the suite's own
# custom measurement-process class, and backprop (the suite's diff_method
# probe): each refused by name (section 2.2).
REFUSED_BY_DESIGN = (
    "MENOSDeviceUnsupportedError: observable Hermitian (no Pauli representation)",
    "MENOSDeviceUnsupportedError: observable Projector (no Pauli representation)",
    "MENOSDeviceUnsupportedError: observable Hadamard (no Pauli representation)",
    "MENOSDeviceUnsupportedError: observable SparseHamiltonian (no Pauli representation)",
    "MENOSDeviceUnsupportedError: Snapshot is not supported by menos.qubit",
    "MENOSDeviceUnsupportedError: measurement MyMeasurement ",
    'MENOSDeviceUnsupportedError: diff_method="backprop"',
)


def _run_suite(shots: str, tmp_path) -> dict:
    report = tmp_path / f"suite-{shots}.xml"
    env = dict(os.environ)
    command = [
        sys.executable,
        "-m",
        "pytest",
        get_device_tests(),
        "--device=menos.qubit",
        f"--shots={shots}",
        "--skip-ops",
        "-q",
        "-p",
        "no:cacheprovider",
        # autograd 1.8.0's source triggers a SyntaxWarning on CPython 3.14.
        "-W",
        "ignore::SyntaxWarning",
        f"--junitxml={report}",
    ]
    completed = subprocess.run(command, env=env, capture_output=True, text=True, timeout=1800)
    assert report.exists(), completed.stdout[-4000:] + completed.stderr[-4000:]
    root = ET.parse(report).getroot()
    # The suite's `flaky` plugin reruns failing tests, and the report lists
    # every attempt: keep each test's last outcome.
    final = {}
    for case in root.iter("testcase"):
        final[f"{case.get('classname')}::{case.get('name')}"] = case
    outcomes = {"passed": 0, "skipped": 0, "refused": 0, "unexpected": []}
    for name, case in final.items():
        failure = case.find("failure")
        if failure is None:
            failure = case.find("error")
        if failure is not None:
            message = failure.get("message", "")
            if any(m in message for m in REFUSED_BY_DESIGN):
                outcomes["refused"] += 1
            else:
                outcomes["unexpected"].append(f"{name}: {message[:300]}")
        elif case.find("skipped") is not None:  # skips and expected failures
            outcomes["skipped"] += 1
        else:
            outcomes["passed"] += 1
    suite = next(root.iter("testsuite"))
    assert len(final) == int(suite.get("tests")), "report and per-test outcomes disagree"
    return outcomes


@pytest.mark.parametrize("shots", ["None", "10000"])
def test_pennylane_device_suite(shots, tmp_path):
    outcomes = _run_suite(shots, tmp_path)
    print(
        f"\npennylane.devices.tests, menos.qubit, shots={shots}: "
        f"{outcomes['passed']} passed, {outcomes['skipped']} skipped or xfailed, "
        f"{outcomes['refused']} refused by design"
    )
    assert not outcomes["unexpected"], "\n".join(outcomes["unexpected"])
    assert outcomes["passed"] >= 300
