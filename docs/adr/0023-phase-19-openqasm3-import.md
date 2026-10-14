# ADR 0023: Phase 19 — OpenQASM 3 import

## Status

**Proposed (2026-10-14); section 9 partly answered.** The owner started Phase 19 on 2026-10-14, in parallel with Phase 18, whose gate is still open (`docs/adr/0022`, on branch `phase-18-windows-native`). The owner chose OpenQASM 3 import as the first phase of the interoperability roadmap and answered Q1–Q4 of section 9 the same day. Branch `phase-19-openqasm3-import`, from `main` at `ff74397`. ADR number 0022 belongs to Phase 18.

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

`reset` (Q2, deferred to Phase 24), `if`/`else`, `for`, `while`, `switch`, `break`/`continue`, `def` subroutines, `extern`, `delay`, `box`, `duration`/`stretch` and timing, `defcal`/`cal`/`defcalgrammar`, classical assignments and arithmetic on `bit`/`int` at run time, `measure` without a target, a measurement used as an expression, `pow` with a non-integer exponent, arrays, `#pragma` and annotations, and any include other than `stdgates.inc`. Malformed programs throw `InvalidCircuit` with the same location.

## 3. Design — [MENOS]

- **Library.** New `menos_qasm3` (`src/qasm3/`), public header `include/menos/qasm3.h`:
  ```cpp
  using InputBindings = std::map<std::string, double, std::less<>>;
  [[nodiscard]] Circuit parse(std::string_view text, const InputBindings& inputs = {});
  ```
  Like `qasm2::parse`, it does not call `ir::validate`; the caller does.
- **Parser.** Hand-rolled (Q1): a lexer to tokens with line and column, then recursive descent. It shares nothing with `src/qasm2` in this phase. Shared code waits for the second implementation (BUILD_PLAN invariants), and the two grammars differ in most productions.
- **`stdgates.inc`.** Built in as text and parsed by the same `gate`-definition path, so its semantics are the specification's own definitions. A gate whose expansion equals a gate-table entry exactly, global phase included, is emitted as that entry (section 4 test), so stabilizer detection and the specialised kernels still see `cx`, `h`, `s` and so on. The text is from the OpenQASM specification repository; its licence (expected Apache-2.0) is recorded in the source and checked before the file is added.
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
| `U(θ,φ,λ)` | **[OPEN] until checked:** the specification's matrix, believed equal to the gate table's `u` (Qiskit's `UGate`, no extra global phase). Measured against the specification text and `qiskit.qasm3` before code depends on it. |
| `stdgates.inc` names | Each name's MENOS expansion equals the matrix of the specification's definition, global phase included, entrywise within `kDefaultAbsTol` = 1e-12; and equals Qiskit's gate of that name. |
| `gphase` | Added to `Circuit::global_phase`; under `ctrl` it is `p` on the control. |
| Angles | Radians. `angle` inputs are treated as real numbers, not reduced modulo 2π. |
| `measure` | Terminal only, as everywhere in MENOS; source order kept. |
| `barrier`, `id` | No operation. |

## 5. Testing — [MENOS]

1. **Unit tests (C++).** Each accepted construct; each refused construct, asserting the exception type and that the message names the construct, line and column; each `InvalidCircuit` case; every input-binding error.
2. **`stdgates.inc` conformance (C++).** For each gate and each modifier rule in section 3, the emitted operations' matrix against the specification definition's matrix, entrywise within 1e-12, global phase included.
3. **OpenQASM 2/3 equivalence.** Fixtures written in both languages give identical circuits from `qasm2::parse` and `qasm3::parse` (depends on Q5).
4. **Differential against Qiskit (Python).** The `tools/gen_fixtures.py` random circuits and a hand-written set covering `stdgates.inc`, user gates and modifiers are exported with `qiskit.qasm3.dumps`, imported with `menos.from_qasm3`, and their statevectors compared with Qiskit's `Statevector` within 1e-12. Seeded counts agree exactly with the same circuit run through `MENOSBackend`.
5. **Unchanged behaviour.** The existing OpenQASM 2 tests and the CLI golden test pass unmodified, except as Q5 decides.

## 6. Order of work

1. This ADR, the `docs/BUILD_PLAN.md` Phase 19 section and owner answers.
2. Measure the `U` convention and `stdgates.inc` definitions (section 4).
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
| A `stdgates.inc` definition differs from the gate table by a global phase, which `ctrl` would expose | Section 4 conformance test, against the specification's own definition |
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
5. **Q5–Q9:** open.

---

## 10. Gate evidence

To be recorded at the end of the phase.
