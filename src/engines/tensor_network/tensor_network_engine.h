#pragma once

#include "menos/simulation_engine.h"

namespace menos::engines::tensor_network {

// Computes an expectation value by contracting the closed bra-ket-
// observable tensor network (analysis::build_closed_contraction_graph)
// to a scalar via a min-fill elimination order (docs/BUILD_PLAN.md
// Phase 8). Expectation only, scoped this phase to the workload the
// Phase 8 precondition actually measured (docs/adr/0010): never a
// statevector (materializing one at this engine's own target scale
// defeats the reason to use it, the same reasoning that already kept
// statevector off DirectMPS) and never counts (a sampling algorithm from
// a network this large is a materially different problem, not validated
// by the precondition's own evidence). Always exact -- the contraction
// performs no truncation, so accuracy_policy is never consulted (an
// exact result trivially satisfies any Controlled tolerance), the same
// stance PauliPropagationEngine already takes.
class TensorNetworkEngine : public SimulationEngine {
public:
    [[nodiscard]] std::string_view name() const override;
    [[nodiscard]] Engine engine_id() const override;
    [[nodiscard]] Backend backend() const override;
    [[nodiscard]] EngineCapabilities capabilities() const override;
    [[nodiscard]] CostEstimate estimate(const EngineEstimateRequest& request) const override;
    [[nodiscard]] EngineExecutionResult execute(
        const EngineExecutionRequest& request) const override;
};

}  // namespace menos::engines::tensor_network
