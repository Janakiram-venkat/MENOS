import json, statistics
B = "build/phase15-baseline"
def load(r, f): return json.load(open(f"{B}/run-{r}/{f}"))
c, d = load("c", "results.json"), load("d", "results.json")
print("| n | gates | MENOS C median (min-max) | MENOS D median (min-max) | D vs C | Qiskit C | Qiskit D | ratio C | ratio D |")
print("|---|---|---|---|---|---|---|---|---|")
for pc, pd in zip(c["menos_vs_qiskit"], d["menos_vs_qiskit"]):
    mc, md = pc["menos_wall_spread"], pd["menos_wall_spread"]
    qc, qd = pc["qiskit_wall_spread"], pd["qiskit_wall_spread"]
    f = lambda s: f"{s['median']:.4g} ({s['min']:.4g}-{s['max']:.4g})"
    print(f"| {pc['n']} | {pc['gates']} | {f(mc)} | {f(md)} | {100*(md['median']/mc['median']-1):+.1f}% | {qc['median']:.4g} | {qd['median']:.4g} | {qc['median']/mc['median']:.2f} | {qd['median']/md['median']:.2f} |")
print()
def dense(r):
    out = {}
    for b in load(r, "dense.json")["benchmarks"]:
        if b.get("run_type") == "iteration":
            out.setdefault(b["run_name"], []).append(b["real_time"])
    return out
dc, dd = dense("c"), dense("d")
print("| benchmark | C median ms (min-max) | D median ms (min-max) | D vs C | C cv | D cv |")
print("|---|---|---|---|---|---|")
for k in dc:
    a, b = dc[k], dd[k]
    cv = lambda x: 100*statistics.stdev(x)/statistics.mean(x)
    name = k.replace("/repeats:10", "")
    print(f"| {name} | {statistics.median(a):.3g} ({min(a):.3g}-{max(a):.3g}) | {statistics.median(b):.3g} ({min(b):.3g}-{max(b):.3g}) | {100*(statistics.median(b)/statistics.median(a)-1):+.1f}% | {cv(a):.1f}% | {cv(b):.1f}% |")
for r in ("c", "d"):
    x = load(r, "crossover.json")
    sv = {row["n"]: row for row in x["statevector"]}
    print(r, "crossover sv n=22 cpu", sv[22]["cpu_seconds"], "cuda", sv[22]["cuda_seconds"], "n=24", sv[24]["cpu_seconds"], sv[24]["cuda_seconds"])
