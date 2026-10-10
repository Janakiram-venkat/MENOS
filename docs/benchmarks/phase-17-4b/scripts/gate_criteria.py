"""Phase 17 gate criterion 4 (docs/BUILD_PLAN.md, Phase 17): per-region
CPU/CUDA for Factorized plans. From factorized_backend.json of 4b gate runs a
and b, each figure a same-run ratio: the all-CPU assignment's median divided
by the planner's own assignment's median. Both runs must pass: at least 1.2x
on at least three of the five measured circuits (owner reset, 2026-10-09,
docs/adr/0021 section 10.3), and no circuit more than 10% slower than all-CPU.
Run from the repository root; exits 1 if any check fails."""

import json
import sys

GATE = "docs/benchmarks/phase-17-4b"
RUNS = ("a", "b")
EXPECTED = [[12, 22], [16, 20], [20, 22], [10, 16, 22], [14, 18, 22]]

ok_all = True


def verdict(ok):
    global ok_all
    ok_all &= ok
    return "PASS" if ok else "FAIL"


print("Criterion 4: planner's assignment >= 1.2x all-CPU on >= 3 of 5 circuits, and none > 10% slower"
      " (both runs)")
for run in RUNS:
    meeting = 0
    rows = json.load(open(f"{GATE}/run-{run}/factorized_backend.json"))["factorized"]
    measured = {tuple(row["blocks"]): row for row in rows if row.get("factorized")}
    for blocks in EXPECTED:
        row = measured.get(tuple(blocks))
        if row is None:
            print(f"  {'+'.join(map(str, blocks))} run {run}: not measured  {verdict(False)}")
            continue
        ratio = row["planner_over_all_cpu"]
        meeting += ratio >= 1.2
        print(
            f"  {'+'.join(map(str, blocks))} run {run}: planner {row['planner_assignment']} "
            f"= {ratio:.2f}x  >=1.2x {'yes' if ratio >= 1.2 else 'no'}  "
            f"not slower {verdict(ratio >= 1 / 1.1)}"
        )
    print(f"  run {run}: {meeting} of {len(EXPECTED)} circuits at >= 1.2x  {verdict(meeting >= 3)}")

print("ALL PASS" if ok_all else "SOME CRITERIA FAIL")
sys.exit(0 if ok_all else 1)
