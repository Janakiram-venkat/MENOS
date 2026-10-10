#pragma once

#include <cstdint>

#include "menos/circuit.h"

namespace menos::ir {

// Preconditions on a KIR circuit, checked once at the runtime entry point:
// qubit/clbit indices in range, no repeated operand within one operation,
// parameter counts, finite parameters, Unitary dimension and unitarity
// within tolerance, terminal-measurement rule, and num_clbits <= 64
// (docs/BUILD_PLAN.md Phase 1, 1.1).
// Throws InvalidCircuit or UnsupportedFeature naming the offending
// operation's index.
void validate(const Circuit& circuit);

// Output-request rules, checked at the same entry point: shots > 0
// requires at least one Measure in the circuit, and a request with
// shots == 0, no statevector and no expectation produces nothing. Both
// throw InvalidCircuit.
void validate_output_request(const Circuit& circuit,
                             std::uint64_t shots,
                             bool want_statevector,
                             bool want_expectation);

}  // namespace menos::ir
