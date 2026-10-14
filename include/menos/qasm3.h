#pragma once

#include <functional>
#include <map>
#include <string>
#include <string_view>

#include "menos/circuit.h"

namespace menos::qasm3 {

// Values for a program's `input` declarations, by name.
using InputBindings = std::map<std::string, double, std::less<>>;

// Parses an OpenQASM 3 program into a Circuit (docs/adr/0023).
//
// Accepted: an optional `OPENQASM 3;`/`OPENQASM 3.0;` header; `include
// "stdgates.inc";`; `qubit`/`bit` declarations (sized or not) and the older
// `qreg`/`creg`, several registers flattened in declaration order; the
// built-in gates `U` (the gate table's `u`) and `gphase`; the 32
// `stdgates.inc` names, each with Qiskit's matrix, global phase included
// (docs/adr/0023 section 4.1 explains why the file's own bodies are not used);
// user `gate` definitions; the modifiers `inv @`, `pow(k) @` with an integer k,
// `ctrl @`/`ctrl(n) @` and `negctrl @`/`negctrl(n) @`, applied exactly (a
// controlled gate with no gate-table form becomes a UnitaryOp of at most
// kMaxUnitaryQubits qubits); `const` and `input` numbers; constant
// expressions with + - * / ** and sin cos tan arcsin arccos arctan exp ln
// sqrt; `measure` into classical bits; and `barrier`, which produces no
// operation. Operations keep source order, measurements included.
//
// Every `input` must have a binding in `inputs` and every binding must name an
// `input`; a missing, unknown or non-finite binding is InvalidCircuit.
//
// Throws menos::common::UnsupportedFeature, naming the construct, line and
// column, for anything outside that subset (reset, control flow, subroutines,
// timing, calibration grammar, classical variables, register broadcast, ...).
// Throws menos::common::InvalidCircuit, with the same location, for a
// malformed program. Does not call menos::ir::validate(); the caller does.
[[nodiscard]] Circuit parse(std::string_view text, const InputBindings& inputs = {});

}  // namespace menos::qasm3
