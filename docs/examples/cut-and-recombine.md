# Advanced Workload: Circuit Cutting

Two four-qubit clusters linked through one qubit. MENOS cuts the link, simulates the fragments, and recombines an exact expectation value.

<<< @/../examples/cut_and_recombine.py

## Output

```text
strategy   cut_and_recombine (exact)
cut        cuts=1 fragments=2 branches=8 distinct_evals=10 actual_peak_bytes=512
expectation: 0.37247461285251077
agrees with the dense statevector to 1.1102230246251565e-16
```

One wire was cut, giving two fragments. Evaluating the 8 branches needed only 10 distinct fragment runs, each small, and the whole thing used 512 bytes at peak. The dense statevector is used here only as an independent check: the two agree to about `1e-16`.

## See also

[Cut and Recombine](/engines/cut-and-recombine)
