#pragma once

#include "menos/simulation_engine.h"

namespace menos::engines::pauli {

// Heisenberg-picture Pauli propagation (docs/adr/0007): computes
// <0...0|U^dagger P U|0...0> for each observable term by propagating it
// backward through the circuit's operations (reverse program order),
// then evaluating the fully-propagated term(s) against the trivial
// |0...0> state. Expectation only -- it never produces counts or a
// statevector. Feasible only when every operation is a named Clifford
// gate (X Y Z H S Sdg SX SXdg CX CZ SWAP, single-Pauli-to-single-Pauli,
// no branching) or one of RZ/RX/RY/P (branching factor at most 2) or U
// (branching factor at most 3) -- T, Tdg, CP, CCX and Unitary are not
// supported this phase and make estimate() infeasible, naming the
// operation. Exact mode only: an unrepresentable worst-case term count
// (checked arithmetic) makes the job infeasible before propagation ever
// starts, never truncated. A region whose operations are entirely
// Clifford is also infeasible here (a hard routing rule, not a cost
// preference, docs/BUILD_PLAN.md Phase 5): the stabilizer engine is
// used for a Clifford region's expectation, even on the rare region
// where Pauli propagation's own per-term cost would estimate lower.
class PauliPropagationEngine : public SimulationEngine {
public:
    [[nodiscard]] std::string_view name() const override;
    [[nodiscard]] Engine engine_id() const override;
    [[nodiscard]] Backend backend() const override;
    [[nodiscard]] EngineCapabilities capabilities() const override;
    [[nodiscard]] CostEstimate estimate(const EngineEstimateRequest& request) const override;
    [[nodiscard]] EngineExecutionResult execute(
        const EngineExecutionRequest& request) const override;
};

}  // namespace menos::engines::pauli
