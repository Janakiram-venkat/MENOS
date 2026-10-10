// The CUDA dense engine refuses a noisy region with a reason, like every CPU
// pure-state engine (tests/plan/engine_noise_refusal_test.cpp; Phase 12,
// docs/adr/0016 section 7 risk 1). It needs a usable device, because without
// one its estimate() reports that first.

#include <gtest/gtest.h>

#include <complex>
#include <vector>

#include "engines/dense_cuda/cuda_dense_engine.h"

namespace menos::plan {
namespace {

class CudaEngineNoiseRefusalTest : public ::testing::Test {
protected:
    void SetUp() override {
        if (!engines::dense_cuda::cuda_device_available()) {
            GTEST_SKIP() << "no CUDA device available -- CUDA noise-refusal coverage did NOT run";
        }
    }
};

TEST_F(CudaEngineNoiseRefusalTest, RefusesANoiseChannelAndAReadoutError) {
    const engines::dense_cuda::CudaDenseEngine engine;

    Circuit with_channel(1, 1);
    with_channel.append(Opcode::H, {0});
    with_channel.append_channel({0}, {{1.0, 0.0, 0.0, 1.0}});
    with_channel.append_measure(0, 0);
    const CostEstimate channel_estimate =
        engine.estimate(EngineEstimateRequest{.num_qubits = 1,
                                              .operations = &with_channel.operations(),
                                              .want_statevector = false,
                                              .want_counts = true,
                                              .want_expectation = false,
                                              .observable = nullptr});
    EXPECT_FALSE(channel_estimate.feasible);
    EXPECT_EQ(channel_estimate.infeasible_reason,
              "operation 1 is a noise channel; this engine simulates pure states only");

    Circuit with_readout(1, 1);
    with_readout.append(Opcode::H, {0});
    with_readout.append_measure(0, 0, ReadoutError{{0.9, 0.1, 0.2, 0.8}});
    const CostEstimate readout_estimate =
        engine.estimate(EngineEstimateRequest{.num_qubits = 1,
                                              .operations = &with_readout.operations(),
                                              .want_statevector = false,
                                              .want_counts = true,
                                              .want_expectation = false,
                                              .observable = nullptr});
    EXPECT_FALSE(readout_estimate.feasible);
    EXPECT_EQ(readout_estimate.infeasible_reason,
              "operation 1 is a measurement with a readout error; this engine does not model "
              "readout error");
}

}  // namespace
}  // namespace menos::plan
