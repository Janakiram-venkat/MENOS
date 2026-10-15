# ADR 0023: Phase 19 — OpenQASM 3 import

## Status

**Accepted for implementation (2026-10-14); gate not yet passed.** The owner started Phase 19 on 2026-10-14, in parallel with Phase 18, whose gate is still open (`docs/adr/0022`, on branch `phase-18-windows-native`). The owner chose OpenQASM 3 import as the first phase of the interoperability roadmap answered section 9 the same day and approved the implementation plan. Branch `phase-19-openqasm3-import`, started from `main` at `ff74397` and rebased on 2026-10-14, at the owner's decision, onto `phase-18-windows-native` at `7fa38ce`, so that it carries Phase 18's fixes (`menos.__version__`, the `noise` and `test` extras) and builds natively on Windows too. ADR number 0022 belongs to Phase 18.

Labels follow `docs/adr/0014`–`0022`: **[MEASURED]**, **[DERIVED]**, **[MENOS]** (a proposed design decision) and **[OPEN]** (an owner decision, section 9).

---

## 1. Context

MENOS reads OpenQASM 2 through `menos::qasm2::parse` (`include/menos/qasm2.h`, `src/qasm2/parser.cpp`, 566 lines, Phase 7). What exists on `ff74397`:

| Item | Where | State |
|---|---|---|
| Importer | `src/qasm2/parser.cpp` | Hand-rolled lexer and recursive-descent parser. One `qreg`, at most one `creg`, gates looked up in the Phase 1 gate table, `measure q[i] -> c[j]`, constant angle expressions (`pi`, `+ - * /`, parentheses). `barrier` and `id` produce no operation. |
| Errors | same | `InvalidCircuit` or `UnsupportedFeature`, with `at line N` in the message |
| Callers | `cli/cli_impl.cpp:147,173` | The CLI is the only caller. Every input file goes to `qasm2::parse`; there is no format selection. |
| Python | — | No QASM entry point (`docs/reference/compatibility.md`) |
| IR | `include/menos/circuit.h` | Opcodes X Y Z H S Sdg T Tdg SX SXdg RX RY RZ P U CX CZ SWAP CP CCX, `UnitaryOp` up to `kMaxUnitaryQubits` = 5, `MeasureOp`, `ChannelOp`, `global_phase`. **No reset.** |
| Gate table | `src/ir/gate_table.cpp` | 20 names (above, lower case, Qiskit conventions) |

OpenQASM 3 is the exchange format of Qiskit (`qiskit.qasm3.dumps`), Braket, tket and others. Its static-circuit subset adds `qubit`/`bit` declarations, several registers, `stdgates.inc`, user `gate` definitions, gate modifiers, `gphase`, richer constant expressions and `input` parameters.

**[MEASURED] from the source, not yet run: the OpenQASM 2 importer reorders measurements.** It collects every `measure` and appends them after all gates (`src/qasm2/parser.cpp:232`). A program such as `measure q[0] -> c[0]; x q[0];` is accepted, and the `x` runs before the measurement. `ir::validate` refuses an operation on an already-measured qubit, so with source order kept this program would be refused. Section 9, Q5.

## 2. Scope

### 2.1 Accepted — [MENOS], within the owner's answers

- Header `OPENQASM 3;` or `OPENQASM 3.0;`, optional as in the specification. Any other version is refused.
- `include "stdgates.inc";`. Any other include is refused.
- Declarations: `qubit[n] q;`, `qubit q;`, `bit[n] c;`, `bit c;`, and the older `qreg q[n];` and `creg c[n];`. Several registers are allowed and flattened in declaration order (section 4).
- Gates: the built-ins `U` and `gphase`; every gate in `stdgates.inc`; user `gate` definitions, which are expanded where they are called.
- Modifiers: `inv @`, `pow(k) @` with an integer constant `k` (negative `k` is `inv` of `pow(-k)`), `ctrl @`, `ctrl(n) @`, `negctrl @` and `negctrl(n) @`.
- Constant expressions for angles: integer and real literals, `pi`/`π`, `tau`/`τ`, `euler`/`ℯ`; `+ - * / **` and unary minus; parentheses; `sin cos tan arcsin arccos arctan exp ln sqrt`; `const` declarations of `int`, `float` and `angle` with constant initialisers.
- `input float[64] name;`, `input float name;` and `input angle name;`, bound to numbers at submission (Q3).
- `measure` in three forms: `c[i] = measure q[j];`, `measure q[j] -> c[i];`, and register-wide `c = measure q;` with equal sizes.
- `barrier`, which produces no operation, as in OpenQASM 2.

### 2.2 Refused with `UnsupportedFeature`, naming the construct, line and column

`reset` (Q2, deferred to Phase 24), `if`/`else`, `for`, `while`, `switch`, `break`/`continue`, `def` subroutines, `extern`, `delay`, `box`, `duration`/`stretch` and timing, `defcal`/`cal`/`defcalgrammar`, classical assignments and arithmetic on `bit`/`int` at run time, `measure` without a target, a measurement used as an expression, `pow` with a non-integer exponent, arrays, `#pragma` and annotations, and any include other than `stdgates.inc`. Also refused, because the subset cannot represent them exactly or they need semantics it does not define: applying a gate to a whole register (broadcast), register slices, physical qubits (`$0`), `float` widths other than 64 and sized `angle[n]` (both round), integer division (`1 / 2`), `input` of a type other than `float` or `angle`, and functions other than the nine listed in section 2.1. Malformed programs throw `InvalidCircuit` with the same location.

## 3. Design — [MENOS]

- **Library.** New `menos_qasm3` (`src/qasm3/`), public header `include/menos/qasm3.h`:
  ```cpp
  using InputBindings = std::map<std::string, double, std::less<>>;
  [[nodiscard]] Circuit parse(std::string_view text, const InputBindings& inputs = {});
  ```
  Like `qasm2::parse`, it does not call `ir::validate`; the caller does.
- **Parser.** Hand-rolled (Q1): a lexer to tokens with line and column, then recursive descent. It shares nothing with `src/qasm2` in this phase. Shared code waits for the second implementation (BUILD_PLAN invariants), and the two grammars differ in most productions.
- **`stdgates.inc` (Q8, revised).** The 32 names are built-in gates, not definitions read from the file: section 4.1 measured that the file's bodies, taken literally, do not denote the gates their names stand for. Each name's matrix equals Qiskit's gate of that name, global phase included. A name with a gate-table entry is emitted as that entry, so stabilizer detection and the specialised kernels still see `cx`, `h`, `s` and so on. `phase`, `u1`, `cphase` and `CX` map to `p`, `p`, `cp` and `cx`; `id` produces no operation. The others (`cy`, `ch`, `crx`, `cry`, `crz`, `cswap`, `cu`, `u2`, `u3`) are emitted as exact decompositions into gate-table gates, each checked entrywise against Qiskit's matrix, global phase included. `include "stdgates.inc";` makes the names available; the file itself is not shipped.
- **Modifiers (Q4).** Applied exactly:
  - `inv`: the body in reverse order with each gate inverted. Every gate-table entry has an exact inverse in the table (`s`↔`sdg`, `rx(θ)`→`rx(−θ)`, `u(θ,φ,λ)`→`u(−θ,−λ,−φ)`, …), and `gphase(a)`→`gphase(−a)`.
  - `pow(k)`: the body repeated `k` times.
  - `ctrl`: `ctrl @ x`→`cx`, `ctrl @ ctrl @ x`→`ccx`, `ctrl @ z`→`cz`, `ctrl @ p`→`cp`, `ctrl @ gphase(a)`→`p(a)` on the control. `negctrl` is `ctrl` between `x` gates on the control.
  - Anything else becomes one exact `UnitaryOp`, its matrix computed from the expanded body, if it acts on at most `kMaxUnitaryQubits` = 5 qubits. Above that it is refused with `UnsupportedFeature`, naming the line.
- **Global phase.** `gphase` and phases from definitions accumulate in `Circuit::global_phase`, which matters only under `ctrl`, where it becomes a relative phase.
- **Order.** Operations keep source order, measurements included, so a gate after a measurement on the same qubit reaches `ir::validate` and is refused there.
- **Inputs.** Every declared `input` must be bound, and every binding must name a declared `input`. A missing, extra, repeated or non-finite binding is `InvalidCircuit`. Values are substituted before any expression is evaluated, so the circuit is fully numeric before planning.
- **CLI.** `menos <explain|run> FILE [--input NAME=VALUE]...`. The format is read from the header: `OPENQASM 2.0` goes to `qasm2::parse`, `OPENQASM 3`/`3.0` to `qasm3::parse`, and a file with no header is refused with a message asking for one (Q6). `--input` with an OpenQASM 2 file is refused.
- **Python.** `menos.from_qasm3(text, inputs=None) -> Circuit` (Q7), binding `qasm3::parse`. No OpenQASM 2 entry point is added.

## 4. Conventions — each row has a test

| Convention | Rule |
|---|---|
| Qubit order | Registers flattened in declaration order; index `i` of a register is `offset + i`. |
| Classical bit order | Same rule; bit `k` of the flattened bits is bit `k` of the counts bitstring, as in Qiskit and the existing frontends. |
| `U(θ,φ,λ)` | The gate table's `u`, which is Qiskit's `UGate`: [[cos θ/2, −e^{iλ} sin θ/2], [e^{iφ} sin θ/2, e^{i(φ+λ)} cos θ/2]], no extra global phase. Under this reading `CX = ctrl @ U(π,0,π)` is exactly CNOT (section 4.1). |
| `stdgates.inc` names | Each name's emitted operations equal Qiskit's gate of that name, global phase included, entrywise within `kDefaultAbsTol` = 1e-12. |
| `gphase` | Added to `Circuit::global_phase`; under `ctrl` it is `p` on the control. |
| Angles | Radians. `angle` inputs are treated as real numbers, not reduced modulo 2π. |
| `measure` | Terminal only, as everywhere in MENOS; source order kept. |
| `barrier`, `id` | No operation. |

### 4.1 The `stdgates.inc` bodies — [MEASURED] 2026-10-14

The file is the one Qiskit 2.5.2 vendors (`qiskit/qasm/libs/stdgates.inc`, from OpenQASM commit `4ca1d793833b24a1`, Apache-2.0). Each body was evaluated literally — `ctrl @ G` = |0⟩⟨0|⊗I + |1⟩⟨1|⊗G with G's global phase, `gphase(a)` = e^{ia}, `pow(r)` the principal power, `inv` the adjoint — and compared with Qiskit's gate of the same name at angles (0.37, −1.21, 2.05, 0.83), under three readings of `U`:

| Reading of `U` | Exactly Qiskit's gate | Differs beyond global phase |
|---|---|---|
| Qiskit `UGate` | p z s sdg t tdg rz cz cp crz CX phase cphase id u1 | cx cy crx cry ch swap ccx cswap cu |
| `UGate` × e^{i(φ+λ)/2} | p x y z s sdg t tdg sx cx cy cz cp swap ccx cswap id | crx cry crz ch cu CX cphase |
| `UGate` × e^{−i(φ+λ)/2} | p z s sdg t tdg cz cp swap cswap id | cx cy crx cry crz ch ccx cu CX cphase |

No reading makes every body agree with its name. Under the specification's own matrix (the first row), the `gphase` terms give `x`, `y`, `h`, `sx`, `rx`, `ry`, `u2` and `u3` extra global phases, and the controlled gates built on them (`cx = ctrl @ x` is a controlled −iX) differ from the gates they name, while `CX = ctrl @ U(π, 0, π)` is CNOT. `qiskit.qasm3.dumps` writes `cx` for CNOT. The owner therefore chose built-in gates with Qiskit's matrices (Q8, revised).

## 5. Testing — [MENOS]

1. **Unit tests (C++).** Each accepted construct; each refused construct, asserting the exception type and that the message names the construct, line and column; each `InvalidCircuit` case; every input-binding error.
2. **`stdgates.inc` conformance.** For each of the 32 names, the emitted operations' matrix against Qiskit's gate of that name (Python), and for each modifier rule in section 3 against the matrix computed from its definition (C++), entrywise within 1e-12, global phase included. The section 4.1 measurement is kept as a Python test, so a change in Qiskit's vendored file is noticed.
3. **OpenQASM 2/3 equivalence.** Fixtures written in both languages give identical circuits from `qasm2::parse` and `qasm3::parse` (depends on Q5).
4. **Differential against Qiskit (Python).** The `tools/gen_fixtures.py` random circuits and a hand-written set covering `stdgates.inc`, user gates and modifiers are exported with `qiskit.qasm3.dumps`, imported with `menos.from_qasm3`, and their statevectors compared with Qiskit's `Statevector` within 1e-12. Seeded counts agree exactly with the same circuit run through `MENOSBackend`.
5. **Unchanged behaviour.** The existing OpenQASM 2 tests and the CLI golden test pass unmodified, except as Q5 decides.

## 6. Order of work

1. This ADR, the `docs/BUILD_PLAN.md` Phase 19 section and owner answers.
2. Measure the `U` convention and `stdgates.inc` definitions (section 4.1, done 2026-10-14).
3. Lexer and parser for declarations, built-in gates, expressions, `measure` and `barrier`, with the refusal tests.
4. Gate definitions, `stdgates.inc`, modifiers and the conformance tests.
5. `input` bindings.
6. CLI header dispatch and `--input`; Python `from_qasm3`.
7. Equivalence fixtures and the Qiskit differential tests.
8. Documentation; gate evidence (section 10).

## 7. Out of scope

`reset` and every dynamic-circuit construct (Phase 24); OpenQASM 3 export; pulse-level and timing semantics; an ANTLR-based parser; Python OpenQASM 2 import; changes to the planner or engines.

## 8. Risks

| Risk | How it is caught |
|---|---|
| A built-in gate differs from Qiskit's by a global phase, which `ctrl` would expose | Section 5 conformance test, entrywise with global phase |
| `qiskit.qasm3.dumps` emits constructs outside the subset (for example `gate` definitions of Qiskit-only gates, or annotations) | Each is either supported exactly or refused by name; the differential test records which fixtures are refused |
| The specification's grammar has corner cases a hand-rolled parser misses | Refusal by default: a token the parser does not know is `InvalidCircuit`, never skipped |
| Changing the OpenQASM 2 measure order changes accepted programs | Owner decision Q5 before any change |

## 9. Owner decisions

- **Q1 — Parser.** Hand-rolled or the reference ANTLR grammar.
- **Q2 — `reset`.** In this phase, or deferred to Phase 24.
- **Q3 — `input` parameters.** In scope or refused.
- **Q4 — Modifiers without a table equivalent.** An exact `UnitaryOp`, or refused.
- **Q5 — OpenQASM 2 measure order.** Proposed: fix `qasm2::parse` to keep source order, so a program that measures a qubit and then acts on it is refused instead of silently reordered. Programs whose measurements are all terminal give the same results; the KIR differs only in where measurements sit. **Alternative:** leave OpenQASM 2 unchanged and compare the 2/3 equivalence fixtures only on programs with all measurements last.
- **Q6 — CLI format selection.** Proposed: by header, with a header required. **Alternative:** a `--format qasm2|qasm3` flag.
- **Q7 — Python entry point.** Proposed: `menos.from_qasm3(text, inputs=None) -> Circuit`.
- **Q8 — `stdgates.inc` text.** Proposed: embed the specification's file verbatim with its licence notice (expected Apache-2.0).
- **Q9 — The gate.** As written in `docs/BUILD_PLAN.md`, Phase 19.

### Owner answers (2026-10-14)

1. **Q1:** hand-rolled.
2. **Q2:** refused, deferred to Phase 24.
3. **Q3:** in scope.
4. **Q4:** exact `UnitaryOp`, up to `kMaxUnitaryQubits`.
5. **Q5:** as proposed: `qasm2::parse` keeps source order.
6. **Q6:** as proposed: by header, header required.
7. **Q7:** as proposed: `menos.from_qasm3(text, inputs=None)`.
8. **Q8:** first answered "embed verbatim"; revised the same day after the section 4.1 measurement: the 32 names are built-in gates with Qiskit's matrices, `U` is Qiskit's `UGate`, and the file is not shipped.
9. **Q9:** the gate in `docs/BUILD_PLAN.md`, Phase 19, approved with the implementation plan. The differential oracle is Qiskit's own import (`qiskit.qasm3.loads`, from the pinned test-only package `qiskit-qasm3-import`), compared exactly, because `qiskit.qasm3.dumps` drops a circuit's global phase.

---

## 10. Gate evidence

Recorded on 2026-10-15 on commit `bb720d8`. **The owner has not yet confirmed the gate.**

### 10.1 Runs

- **Linux.** `scripts/verify.sh` in the WSL clone `~/MENOS`, `UV_PYTHON_PREFERENCE=only-managed`, exit 0 (`verify.sh: all stages passed`). GCC 15.2.0, CMake 4.2.3, clang-format/clang-tidy 21.1.8, Python 3.14.4, uv 0.12.21, CUDA 13.3.73 and 12.4.131. The documentation stage was skipped (no Node.js in WSL), as the script reports.
- **Windows.** `scripts\verify-windows.ps1` (without `-Wheels`) in a worktree of the same commit, exit 0 (`verify-windows: all stages passed`), MSVC 14.50.35717. Not a Phase 19 criterion; run because the branch is based on Phase 18 (`7fa38ce`).
- An earlier `verify.sh` run on `66c4918` also exited 0. A review of the tests against criteria 1 and 4 on that commit found gaps (`creg`, `τ`, `ℯ`, `tan`, `arcsin` and `arccos` untested; `inv` tested on 8 of the 20 gate-table opcodes), which `bb720d8` closes with tests only.

### 10.2 Criteria

| # | Criterion | Evidence |
|---|---|---|
| 1 | Subset | `tests/qasm3/parser_test.cpp`, 22 tests: header forms and its absence; the include; `qubit`/`bit` sized and unsized, `qreg`, `creg`, flattened in declaration order; `U`; `gphase`; each of the 32 names with its arity, the aliases and `id`; user gates and their argument substitution; every constant (`pi`/`π`, `tau`/`τ`, `euler`/`ℯ`), operator and the nine functions; `const` of `int`, `float`, `angle`; the three `input` forms and every binding error; the three `measure` forms in source order; `barrier`. |
| 2 | Refusals | `tests/qasm3/refusal_test.cpp`: 42 constructs, each asserting `UnsupportedFeature`, the construct's name and `line 4, column N`; another version than 3; a refused keyword inside a gate body. Malformed programs: `MalformedProgramsNameTheirLocation` (9 cases, location asserted). |
| 3 | Built-in gates | `python/tests/test_qasm3.py::test_each_stdgates_name_has_qiskits_matrix` (32 cases) and `test_builtin_u_is_qiskits_ugate`: the full unitary, built column by column, equals Qiskit's `Operator` within 1e-12, global phase included. The section 4.1 measurement is kept as `test_qiskits_vendored_stdgates_bodies_disagree_with_their_names`. |
| 4 | Modifiers | `tests/qasm3/modifiers_test.cpp`, 13 tests, each entrywise within 1e-12 against a matrix built in the test: `inv` of each of the 20 gate-table opcodes, of `U` and of a user gate; `pow` positive, negative and zero; every `ctrl` mapping (`cx`, `ccx`, `cz`, `cp`, `gphase`→`p`); `negctrl`, mixed controls and nested modifiers; a definition's global phase made relative under `ctrl`; the `UnitaryOp` path; acceptance at 5 qubits and refusal at 6 naming the line. Also `test_modifier_program_equals_qiskits_import`. |
| 5 | Differential | `test_statevector_equals_qiskits_import_of_the_same_text`: 13 fixtures (one covering the gate names plus `sxdg`, `rzz`, `u`; 12 `random_circuit` with up to 3-qubit gates), each within 1e-12 of `qiskit.qasm3.loads` of the same text and within 1e-12 of the original circuit after one phase factor. `test_seeded_counts_equal_menos_backend`: counts equal `MENOSBackend`'s exactly for seeds 3, 17 and 2026. |
| 6 | OpenQASM 2/3 equivalence | `tests/qasm3/equivalence_test.cpp`: three paired programs (Bell, every shared gate with `barrier` and `id`, interleaved measurements) give identical circuits, operation by operation, parameters and global phase compared exactly. |
| 7 | OpenQASM 2 measure order | `Qasm2ParserTest.MeasurementsKeepSourceOrder` and `GateAfterMeasuringTheSameQubitIsRefusedByValidate`. `git diff 7fa38ce..bb720d8` adds only these two tests to `tests/qasm2/parser_test.cpp` and changes no golden file; the CLI golden tests pass. |
| 8 | Entry points | `tests/cli/qasm3_test.cpp`, 7 tests: OpenQASM 2 and 3 programs give identical `run` and `explain` output; `--input` binds; missing binding; malformed flags; no header; `--input` refused for OpenQASM 2; a refusal surfaces with its location. Python: `test_inputs_bind_by_name_and_none_means_no_inputs`, `test_refused_constructs_raise_unsupported_feature_with_their_location`. |
| 9 | Verification | Linux: format and clang-tidy (all targets) clean; `dev` and `release` 679/679 (`dev` skips the wall-time report, which needs an optimised build); pytest 313 passed, 1 deselected (`test_wheel_smoke.py`), including 53 in `test_qasm3.py`; wheel smoke test 1 passed; `cuda-dev`/`cuda-release` 734/734 (56 and 55 GPU tests skipping: the Windows driver does not support CUDA 13); `cuda12-dev`/`cuda12-release` 734/734 (GPU tests executing). Windows: `windows-dev` and `windows-release` 680/680; pytest 313 passed, 1 deselected; wheel smoke 1 passed. Documentation: README, `docs/build/command-line.md`, `docs/reference/python-api.md`, `compatibility.md`, `limitations.md`, `changelog.md`, `docs/BUILD_PLAN.md` and this ADR. New symbols: `qasm3::parse` is called by the CLI and the Python binding; `menos.from_qasm3` is public; the `src/qasm3` internals are reached only through `parse`; each is tested above. |

### 10.3 Not verified

- The documentation site was not built (no Node.js in WSL); its Python snippets ran in the pytest stage.
- The Windows run did not include `-Wheels` (the cibuildwheel stage for CPython 3.10–3.13).
- GPU tests under CUDA 13 did not execute (driver); they did under CUDA 12.
