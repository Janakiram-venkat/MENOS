"""Phase 15 gate criteria 1-4 and 7 from the JSON data (docs/BUILD_PLAN.md,
Phase 15 gate). The baseline at each point is the FASTER of the two baseline
runs (runs C/D for the dashboard, E/F for the dense benchmark), which is the
stricter comparison. Both gate runs (A, B) must meet each numeric criterion.
Run from the repository root."""

import json
import statistics
import sys

BASE = "docs/benchmarks/phase-15-baseline"
GATE = "docs/benchmarks/phase-15-gate"


def dash(path):
    return {p["n"]: p for p in json.load(open(path))["menos_vs_qiskit"]}


def dense(path):
    out = {}
    for b in json.load(open(path))["benchmarks"]:
        if b.get("run_type") == "iteration":
            out.setdefault(b["run_name"].replace("/repeats:10", ""), []).append(b["real_time"])
    return {k: statistics.median(v) for k, v in out.items()}


base_c, base_d = dash(f"{BASE}/run-c/results.json"), dash(f"{BASE}/run-d/results.json")
gate = {r: dash(f"{GATE}/run-{r}/results.json") for r in ("a", "b")}
ob = dash(f"{GATE}/run-c/results.json")
base_dense = [dense(f"{BASE}/dense-e/dense.json"), dense(f"{BASE}/dense-f/dense.json")]
gate_dense = {r: dense(f"{GATE}/run-{r}/dense.json") for r in ("a", "b")}

ok_all = True


def verdict(ok):
    global ok_all
    ok_all &= ok
    return "PASS" if ok else "FAIL"


def base_dash(n):
    return min(base_c[n]["menos_wall_seconds"], base_d[n]["menos_wall_seconds"])


print("Criterion 1: dashboard end-to-end speed-up >= 4x at n = 22, 24 (both runs)")
for n in (22, 24):
    for r in ("a", "b"):
        s = base_dash(n) / gate[r][n]["menos_wall_seconds"]
        print(f"  n={n} run {r.upper()}: baseline {base_dash(n):.4f} s / {gate[r][n]['menos_wall_seconds']:.4f} s = {s:.2f}x  {verdict(s >= 4)}")

print("Criterion 2: dense 1q/2q gate speed-up >= 5x at n = 22, 24, 26 (default threads, both runs)")
for name in ("BM_DenseGateApplication", "BM_DenseGateApplication1Q"):
    for n in (22, 24, 26):
        key = f"{name}/{n}"
        b = min(d[key] for d in base_dense)
        for r in ("a", "b"):
            s = b / gate_dense[r][key]
            print(f"  {key} run {r.upper()}: {b:.3f} / {gate_dense[r][key]:.3f} ms = {s:.2f}x  {verdict(s >= 5)}")

print("Criterion 3: no dashboard point n = 4..18 more than 10% slower than baseline (both runs)")
for n in range(4, 19, 2):
    for r in ("a", "b"):
        g = gate[r][n]["menos_wall_seconds"]
        change = g / base_dash(n) - 1
        print(f"  n={n:2d} run {r.upper()}: baseline {base_dash(n) * 1e3:.4f} ms, now {g * 1e3:.4f} ms ({change * 100:+.1f}%)  {verdict(change <= 0.10)}")

print("Criterion 4: one thread, dense 1q/2q speed-up >= 2x at n = 22, 24, 26 (both runs)")
for name, base_name in (("BM_DenseGateApplicationOneThread", "BM_DenseGateApplication"),
                        ("BM_DenseGateApplication1QOneThread", "BM_DenseGateApplication1Q")):
    for n in (22, 24, 26):
        b = min(d[f"{base_name}/{n}"] for d in base_dense)
        for r in ("a", "b"):
            g = gate_dense[r][f"{name}/{n}"]
            s = b / g
            print(f"  {name}/{n} run {r.upper()}: {b:.3f} / {g:.3f} ms = {s:.2f}x  {verdict(s >= 2)}")

print("Criterion 7 (recorded, no threshold): dashboard MENOS with OPENBLAS_NUM_THREADS=1 (run C) vs runs A/B")
for n in (14, 16, 18, 20, 22, 24):
    a, b, c = (gate["a"][n]["menos_wall_seconds"], gate["b"][n]["menos_wall_seconds"], ob[n]["menos_wall_seconds"])
    print(f"  n={n}: A {a:.4f} s, B {b:.4f} s, OPENBLAS=1 {c:.4f} s; Qiskit A {gate['a'][n]['qiskit_wall_seconds']:.3f} s vs OPENBLAS=1 {ob[n]['qiskit_wall_seconds']:.3f} s")

agree = all(p["agrees"] for r in ("a", "b") for p in gate[r].values()) and all(p["agrees"] for p in ob.values())
print(f"Agreement with Qiskit (every point, runs A, B, C): {verdict(agree)}")
print("ALL NUMERIC CRITERIA:", "PASS" if ok_all else "FAIL")
sys.exit(0 if ok_all else 1)
