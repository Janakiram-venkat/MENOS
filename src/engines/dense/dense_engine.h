#pragma once

#include "menos/simulation_engine.h"

namespace menos::engines::dense {

// Wraps the existing dense-statevector primitives (StateVector,
// apply_matrix, sample, CumulativeDistribution -- all unchanged, see
// their own headers) behind the SimulationEngine interface introduced in
// Phase 4. The only engine that can produce a statevector.
class DenseEngine : public SimulationEngine {
public:
    [[nodiscard]] std::string_view name() const override;
    [[nodiscard]] Engine engine_id() const override;
    [[nodiscard]] Backend backend() const override;
    [[nodiscard]] EngineCapabilities capabilities() const override;
    [[nodiscard]] CostEstimate estimate(const EngineEstimateRequest& request) const override;
    [[nodiscard]] EngineExecutionResult execute(
        const EngineExecutionRequest& request) const override;
};

}  // namespace menos::engines::dense
