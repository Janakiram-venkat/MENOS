#pragma once

#include <string_view>

#include "menos/circuit.h"

namespace menos::qasm2 {

// Parses OpenQASM 2 text into a Circuit (docs/BUILD_PLAN.md Phase 7).
// Scoped to a required `OPENQASM 2.0;` header, a required
// `include "qelib1.inc";`, exactly one `qreg`, at most one `creg`, gate
// calls whose name has an entry in the Phase 1 gate table
// (ir::gate_info_by_name -- this parser defines no gate-name mapping of
// its own), `measure`, and a numeric-parameter grammar of literals,
// `pi`, unary +/-, binary +-*/ and parentheses (the common subset real
// OpenQASM 2 programs use for gate angles; function calls such as
// `sin(...)` are not supported).
//
// `barrier` and `id` are recognized and produce no operation (both are
// true no-ops here, matching the existing Qiskit-adapter precedent of
// dropping the same two instruction names).
//
// Throws menos::common::UnsupportedFeature, naming the line number, for
// a `gate` block, `if`, `opaque`, `reset`, a version other than 2.0, a
// missing `include`, more than one `qreg`/`creg`, or a gate name absent
// from the Phase 1 table. Throws menos::common::InvalidCircuit, naming
// the line number, for any other malformed input (bad syntax, an
// undeclared register, an out-of-range index, a measure target with no
// declared classical register). Does not call menos::ir::validate()
// itself -- the caller does, exactly like every other Circuit-producing
// entry point (docs/BUILD_PLAN.md Phase 7).
[[nodiscard]] Circuit parse(std::string_view text);

}  // namespace menos::qasm2
