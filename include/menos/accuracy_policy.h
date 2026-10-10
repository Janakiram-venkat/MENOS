#pragma once

#include <cstdint>
#include <optional>

namespace menos {

// Exact (the default, and the implicit behavior of every phase before
// Phase 6) or Controlled(tolerance): whether the planner may consider a
// truncating MPS candidate at all (docs/BUILD_PLAN.md Phase 6). Every
// engine that existed before Phase 6 is exact under both policies --
// this only changes DirectMPS's own feasibility.
enum class AccuracyPolicyKind { Exact, Controlled };

// tolerance and max_bond_dimension are meaningful only when
// kind == Controlled. tolerance is used directly as the MPS engine's
// per-truncation-step relative singular-value cutoff, never as a
// pre-allocated global error budget (CLAUDE.md's "Controlled(tolerance)
// is a per-step cutoff, not a global budget" domain invariant).
// max_bond_dimension is an optional additional cap alongside the
// cutoff.
struct AccuracyPolicy {
    AccuracyPolicyKind kind = AccuracyPolicyKind::Exact;
    double tolerance = 0.0;
    std::optional<std::uint32_t> max_bond_dimension = std::nullopt;
};

}  // namespace menos
