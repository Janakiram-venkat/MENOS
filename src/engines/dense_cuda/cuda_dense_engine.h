#pragma once

#include "menos/simulation_engine.h"

namespace menos::engines::dense_cuda {

// The dense statevector algorithm executed on an NVIDIA GPU
// (docs/BUILD_PLAN.md Phase 10, docs/adr/0014).
//
// engine_id() is Engine::Dense, not a new enum value: this is the SAME
// algorithm as engines::dense::DenseEngine, run on different hardware. The two
// differ only in backend(), and that pair is what the runtime resolves a
// Region to. Because the algorithm is identical, so is estimate()'s work-unit
// count -- the hardware difference lives in the calibrated rate the planner
// converts those work units with, never in the count itself.
//
// Shared code, not duplicated code:
//   - gate application, initialization and the Pauli expectation reduction go
//     through the dense primitive API (src/kernels/dense_kernels.h), whose CPU
//     and CUDA implementations obey one documented contract;
//   - counts sampling deliberately does NOT run on the device. The state is
//     copied back and the existing engines::dense sampler runs unchanged, so
//     counts for a given seed are identical to the CPU backend's rather than
//     merely close. Combined with the bitwise-identical statevector (the
//     device kernels are built with -fmad=false so their arithmetic matches
//     the CPU's operation for operation), that identity holds by
//     construction, which is what CLAUDE.md's determinism invariant requires.
//
// Device memory is budgeted separately from host memory; estimate() reports it
// in CostEstimate::estimated_device_bytes and execute() refuses an
// over-budget region with ResourceExceeded before any cudaMalloc is attempted.
class CudaDenseEngine : public SimulationEngine {
public:
    [[nodiscard]] std::string_view name() const override;
    [[nodiscard]] Engine engine_id() const override;
    [[nodiscard]] Backend backend() const override;
    [[nodiscard]] EngineCapabilities capabilities() const override;
    [[nodiscard]] CostEstimate estimate(const EngineEstimateRequest& request) const override;
    [[nodiscard]] EngineExecutionResult execute(
        const EngineExecutionRequest& request) const override;
};

// True iff this process can actually run work on a GPU right now: the binary
// was built with CUDA support AND a usable device is present. Checked at
// runtime, not inferred from the build flag, so a CUDA-enabled binary on a
// machine with no GPU simply never offers the backend instead of failing at
// execution time.
//
// The device query is performed once and cached.
[[nodiscard]] bool cuda_device_available();

}  // namespace menos::engines::dense_cuda
