# Planner Explanation

Ask MENOS how it planned a job and read the answer.

<<< @/../examples/plan_explain.py

## Output

```text
MENOS EXECUTION REPORT
circuit    qubits=3 clbits=3 operations=6 depth=4 global_phase=0
light_cone qubits 3->3 operations 6->6
candidate 0strategy=direct_dense feasible=true winner=false peak_bytes=128 work_units=96
candidate 1strategy=direct_stabilizer feasible=true winner=true peak_bytes=56 work_units=33
candidate 2strategy=direct_mps feasible=true winner=false peak_bytes=256 work_units=51
strategy   direct_stabilizer (exact)
region 0   qubits=[0,1,2] engine=stabilizer backend=cpu
estimated  state_bytes=56 work_units=33
actual     state_bytes=56 execute_s=0.000008 sample_s=0.000000
```

Three candidates were feasible. The stabilizer engine won with 33 work units against 51 for MPS and 96 for dense. The `actual` line reports what was measured; its timing will differ on your machine.

## Reading it

Read [Plan Explanation](/planning/explain) for every line.
