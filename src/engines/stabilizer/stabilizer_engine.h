#pragma once

#include "menos/simulation_engine.h"

namespace menos::engines::stabilizer {

// Aaronson-Gottesman-style tableau engine (docs/adr/0006). Produces
// counts only -- it never tracks global phase, so it cannot advertise
// statevector output (CLAUDE.md's global-phase domain invariant).
// Feasible only when every operation is a named Clifford gate
// (src/ir/gate_table.h's is_clifford).
class StabilizerEngine : public SimulationEngine {
public:
    [[nodiscard]] std::string_view name() const override;
    [[nodiscard]] Engine engine_id() const override;
    [[nodiscard]] Backend backend() const override;
    [[nodiscard]] EngineCapabilities capabilities() const override;
    [[nodiscard]] CostEstimate estimate(const EngineEstimateRequest& request) const override;
    [[nodiscard]] EngineExecutionResult execute(
        const EngineExecutionRequest& request) const override;
};

}  // namespace menos::engines::stabilizer
