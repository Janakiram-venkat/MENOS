#pragma once

#include "menos/circuit.h"

namespace menos::ir {

// The longest chain of operations in which consecutive operations share a
// qubit, counting measurements (docs/BUILD_PLAN.md Phase 1, 1.1).
[[nodiscard]] int depth(const Circuit& circuit);

}  // namespace menos::ir
