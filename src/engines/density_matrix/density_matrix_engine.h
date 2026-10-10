#pragma once

#include "menos/simulation_engine.h"

namespace menos::engines::density_matrix {

// Exact mixed-state simulation (Phase 12, docs/adr/0016 section 4): the only
// engine that can run a noise channel (ChannelOp) or a measurement with a
// readout error. Nothing is truncated or sampled except the requested shots,
// so its results are exact for the requested noise model.
//
// The density matrix rho of an n-qubit region is stored as a 2n-qubit vector in
// dense::StateVector, entry (row r, column c) at index r | (c << n), so its
// 16 * 4^n bytes are budget-checked before allocation by the same code as a
// statevector. A gate U is applied as U on the row qubits and conj(U) on the
// column qubits (rho -> U rho U^dagger); a channel as its superoperator
// sum_i K_i (x) conj(K_i) on both, in place. Both use dense::apply_matrix
// unchanged.
//
// Counts are sampled from the diagonal Re rho[x, x]. A measurement's readout
// error is applied per shot after its true outcome is drawn, by one uniform
// per readout-errored measure target in measure_targets order. Expectation
// values are Tr(rho P), one pass over 2^n entries per term; readout errors do
// not affect them. No statevector is produced, and neither is the density
// matrix itself (nothing in Phase 12 consumes one). CPU only.
class DensityMatrixEngine : public SimulationEngine {
public:
    [[nodiscard]] std::string_view name() const override;
    [[nodiscard]] Engine engine_id() const override;
    [[nodiscard]] Backend backend() const override;
    [[nodiscard]] EngineCapabilities capabilities() const override;
    [[nodiscard]] CostEstimate estimate(const EngineEstimateRequest& request) const override;
    [[nodiscard]] EngineExecutionResult execute(
        const EngineExecutionRequest& request) const override;
};

}  // namespace menos::engines::density_matrix
