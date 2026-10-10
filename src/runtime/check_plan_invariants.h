#pragma once

#include "menos/circuit.h"
#include "menos/plan.h"

namespace menos::runtime {

// Verifies that every multi-operand operation's operands all lie in the
// same region, re-derived directly from plan.regions[*].local_to_global
// (never by re-invoking menos::analysis -- runtime does not depend on
// analysis, preserving the one-way architecture rule). A violation throws
// InternalError immediately: a violated planning invariant is never a
// reason to silently fall back to direct execution (docs/BUILD_PLAN.md
// Phase 3, 3.7; CLAUDE.md's "no silent fallback" guardrail). Called only
// for a Factorized plan, before any region is allocated.
void check_plan_invariants(const Circuit& circuit, const Plan& plan);

}  // namespace menos::runtime
