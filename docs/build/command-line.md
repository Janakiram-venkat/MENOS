# Command Line

The repository builds a small command-line tool, `menos`, that reads an **OpenQASM 2** file and either runs it or shows how it would be planned. It is an inspection tool, linked only to the public API.

::: info Not installed by pip
The command-line tool is a CMake target and is **not** part of the Python package. Build it with the CMake presets; the binary is `build/<preset>/cli/menos`.
:::

```bash
cmake --preset release
cmake --build --preset release --target menos_cli
build/release/cli/menos explain examples/bell.qasm
```

## Commands

```text
menos explain <file.qasm> [--shots N] [--seed N] [--time-budget SECONDS] [--backend cpu|cuda]
menos run     <file.qasm> --shots N --seed N [--statevector] [--time-budget SECONDS] [--backend cpu|cuda]
```

### `explain`

Plans the job and prints the report **without executing it**. `--shots` defaults to 1 (it only selects the counts output for planning).

```text
$ menos explain examples/bell.qasm
MENOS EXECUTION REPORT
circuit    qubits=2 clbits=2 operations=4 depth=3 global_phase=0
light_cone qubits 2->2 operations 4->4
candidate 0strategy=direct_dense feasible=true winner=false peak_bytes=64 work_units=32
candidate 1strategy=direct_stabilizer feasible=true winner=true peak_bytes=40 work_units=16
candidate 2strategy=direct_mps feasible=true winner=false peak_bytes=128 work_units=34
strategy   direct_stabilizer (exact)
region 0   qubits=[0,1] engine=stabilizer backend=cpu
estimated  state_bytes=40 work_units=16
```

### `run`

Runs the job, prints the full report including the `actual` line, then the counts (highest classical bit left):

```text
$ menos run examples/bell.qasm --shots 100 --seed 5
…
actual     state_bytes=40 execute_s=0.000006 sample_s=0.000000
00: 49
11: 51
```

`--shots` and `--seed` are both **required** for `run`. `--statevector` also prints every amplitude, and is limited to circuits of **10 qubits or fewer**.

### `--time-budget SECONDS`

Both commands accept a [time budget](/runtime/time-budgets), the CLI form of `RunOptions.time_budget_seconds` (since Phase 17). It must be a finite number greater than 0; anything else is a usage error that names the flag. With `explain` it only decides which plans fit, since nothing runs. With `run`, an attempt that overruns is abandoned and replanned, and the report says so; if no plan fits, the tool exits with `1` and an error naming the budget. With a budget, the report also prints machine-dependent time estimates.

### `--backend cpu|cuda`

Forces the backend instead of letting the planner choose, the CLI form of `RunOptions.backend` (since Phase 17). The report then contains a `backend    cpu (requested)` (or `cuda`) line. A CUDA request that cannot be honoured exits with `1` and an error, and never runs on the CPU. See [Forcing a backend](/planning/backend-selection#forcing-a-backend). Any value other than `cpu` or `cuda` is a usage error.

## Exit status

`0` on success. On any error the tool writes `error: …` to standard error and exits with `1`:

```text
error: --shots is required for 'run'
error: usage: menos <explain|run> <file.qasm> [--shots N] [--seed N] [--statevector] [--time-budget SECONDS] [--backend cpu|cuda]
```

## OpenQASM support

| Supported | Not supported |
|---|---|
| OpenQASM 2.0, `qelib1.inc` gates that map to MENOS gates | The register-wide form `measure q -> c;` |
| `measure q[i] -> c[j];` one bit at a time | OpenQASM 3 |

Write measurements bit by bit. The register-wide form is rejected:

```text
error: qasm2: expected '[' after a register name at line 7
```

## Limits

The tool has no option for a memory budget; use the [native API](/build/native-api) for that. A time budget is `--time-budget`.
