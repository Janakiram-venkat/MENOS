"""Phase 16 gate criteria 1, 2 and 4 from the kernel benchmark's JSON
(docs/BUILD_PLAN.md, Phase 16 gate). Every speed-up is a same-run ratio:
generic median / specialised median, both measured in the same run of
menos_cuda_kernel_benchmark. Both gate runs (a, b) must meet each criterion.
Criterion 3's bitwise flag is printed beside criterion 1; the tests are its
evidence. Run from the repository root; exits 1 if any criterion fails."""

import json
import sys

GATE = "docs/benchmarks/phase-16-gate"
RUNS = ("a", "b")
SMALL = (4, 6, 8, 10, 12, 14)
LARGE = (22, 24, 26)

ok_all = True


def verdict(ok):
    global ok_all
    ok_all &= ok
    return "PASS" if ok else "FAIL"


def load(run):
    document = json.load(open(f"{GATE}/run-{run}/kernel.json"))
    assert document["schema_version"] == 2, "kernel.json is not from the Phase 16 benchmark"
    paths = {row["n"]: row for row in document["probe"] if row["measured"]}
    gates = {(row["n"], row["k"]): row for row in document["breakdown"]}
    return paths, gates


data = {run: load(run) for run in RUNS}

print("Criterion 1: end-to-end speed-up >= 1.4x at n = 4-14 and >= 1.15x at n = 22, 24, 26"
      " (both runs)")
for n, target in [(n, 1.4) for n in SMALL] + [(n, 1.15) for n in LARGE]:
    for run in RUNS:
        row = data[run][0][n]
        g = row["generic_end_to_end_seconds"]["median"]
        s = row["specialised_end_to_end_seconds"]["median"]
        bitwise = row["specialised_bitwise_identical"]
        print(f"  n={n} run {run}: {g:.4f} s / {s:.4f} s = {g / s:.2f}x  {verdict(g / s >= target)}"
              f"  bitwise {verdict(bitwise)}")

print("Criterion 2: per-gate speed-up >= 1.3x (k = 1), >= 0.95x (k = 2) at n = 22, 24, 26 (both runs)")
for k, target in ((1, 1.3), (2, 0.95)):
    for n in LARGE:
        for run in RUNS:
            row = data[run][1][(n, k)]
            g = row["generic_wall_seconds"]["median"]
            s = row["specialised_wall_seconds"]["median"]
            print(f"  k={k} n={n} run {run}: {g * 1e3:.3f} ms / {s * 1e3:.3f} ms = {g / s:.2f}x"
                  f"  {verdict(g / s >= target)}")

print("Criterion 4: no end-to-end point at n = 4-18 more than 10% slower than generic (both runs)")
for run in RUNS:
    for n in sorted(data[run][0]):
        if n > 18:
            continue
        row = data[run][0][n]
        g = row["generic_end_to_end_seconds"]["median"]
        s = row["specialised_end_to_end_seconds"]["median"]
        print(f"  n={n} run {run}: specialised/generic = {s / g:.3f}  {verdict(s <= 1.10 * g)}")

print("ALL PASS" if ok_all else "SOME CRITERIA FAIL")
sys.exit(0 if ok_all else 1)
