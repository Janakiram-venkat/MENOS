#pragma once

#include "menos/simulation_engine.h"

namespace menos::engines::mps {

// Wraps the MPS primitives (MPSState, apply_gate, MPSCountsSampler,
// compute_expectation) behind the SimulationEngine interface
// (docs/BUILD_PLAN.md Phase 6). Handles low-entanglement circuits with
// a rigorous pre-execution cost bound (Exact) or an honestly-bounded
// truncation mode (Controlled). Never produces a statevector:
// capabilities().statevector is always false, so this engine never
// needs to track global_phase (matches CLAUDE.md's "Engines that
// cannot track global phase must not advertise statevector output"
// domain invariant; execute() ignores request.global_phase entirely,
// since it cancels in both |amplitude|^2 (counts) and <psi|P|psi>
// (expectation) regardless).
class MPSEngine : public SimulationEngine {
public:
    [[nodiscard]] std::string_view name() const override;
    [[nodiscard]] Engine engine_id() const override;
    [[nodiscard]] Backend backend() const override;
    [[nodiscard]] EngineCapabilities capabilities() const override;
    [[nodiscard]] CostEstimate estimate(const EngineEstimateRequest& request) const override;
    [[nodiscard]] EngineExecutionResult execute(
        const EngineExecutionRequest& request) const override;
};

}  // namespace menos::engines::mps
