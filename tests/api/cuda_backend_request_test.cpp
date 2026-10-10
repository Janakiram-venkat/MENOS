// Phase 17 (docs/adr/0021 section 2): RunOptions::backend on a real device.
//
// A forced backend is the one that executes. When both runs use dense
// simulation, forcing CPU or CUDA gives bitwise-identical statevectors and
// counts (docs/adr/0014). Forcing CUDA can change the algorithm (to dense)
// relative to the planner's own choice, so the counts test requests a
// statevector too, which keeps both runs on dense, and asserts that it did.
// Skipped (loudly) when no device is present, except the refusal test, which
// is what a build with CUDA support but no usable device must do.

#include <gtest/gtest.h>

#include <algorithm>
#include <complex>
#include <cstring>
#include <random>
#include <string>
#include <utility>
#include <vector>

#include "common/error.h"
#include "engines/dense_cuda/cuda_dense_engine.h"
#include "menos/menos.h"

namespace menos {
namespace {

Circuit layered(std::uint32_t num_qubits, bool measure) {
    Circuit circuit(num_qubits, measure ? num_qubits : 0);
    for (std::uint32_t layer = 0; layer < 3; ++layer) {
        for (QubitId q = 0; q < num_qubits; ++q) {
            circuit.append(Opcode::H, {q});
        }
        circuit.append(Opcode::T, {0});
        for (QubitId q = layer % 2; q + 1 < num_qubits; q += 2) {
            circuit.append(Opcode::CX, {q, static_cast<QubitId>(q + 1)});
        }
    }
    if (measure) {
        for (QubitId q = 0; q < num_qubits; ++q) {
            circuit.append_measure(q, q);
        }
    }
    return circuit;
}

Result run_on(const Circuit& circuit, Backend backend, bool want_statevector) {
    RunOptions options{
        .shots = want_statevector ? 0U : 2000U, .want_statevector = want_statevector, .seed = 11};
    options.backend = backend;
    return run(circuit, options);
}

TEST(CudaBackendRequestTest, ForcedCudaWithoutAUsableDeviceIsUnsupported) {
    if (engines::dense_cuda::cuda_device_available()) {
        GTEST_SKIP() << "a device is present; the device-less refusal is covered by "
                        "CudaBackendSelectionTest.ForcedCudaWithoutADeviceIsUnsupported";
    }
    RunOptions options{.shots = 10, .seed = 1};
    options.backend = Backend::Cuda;
    EXPECT_THROW(static_cast<void>(run(layered(4, true), options)), common::UnsupportedFeature);
}

class CudaBackendRequestDeviceTest : public ::testing::Test {
protected:
    void SetUp() override {
        if (!engines::dense_cuda::cuda_device_available()) {
            GTEST_SKIP() << "no CUDA device available -- forced-backend execution coverage did "
                            "NOT run in this configuration";
        }
    }
};

TEST_F(CudaBackendRequestDeviceTest, EachForcedBackendIsTheOneThatExecutes) {
    // 6 qubits: small enough that the planner alone would keep it on the CPU.
    for (const Backend backend : {Backend::Cpu, Backend::Cuda}) {
        const Result result = run_on(layered(6, false), backend, /*want_statevector=*/true);
        ASSERT_EQ(result.plan().regions.size(), 1U);
        EXPECT_EQ(result.plan().regions[0].engine, Engine::Dense);
        EXPECT_EQ(result.plan().regions[0].backend, backend);
    }
}

TEST_F(CudaBackendRequestDeviceTest, ForcingABackendNeverChangesTheStatevector) {
    const Circuit circuit = layered(10, false);
    const Result cpu = run_on(circuit, Backend::Cpu, /*want_statevector=*/true);
    const Result cuda = run_on(circuit, Backend::Cuda, /*want_statevector=*/true);
    ASSERT_TRUE(cpu.statevector().has_value() && cuda.statevector().has_value());
    const auto& a = *cpu.statevector();
    const auto& b = *cuda.statevector();
    ASSERT_EQ(a.size(), b.size());
    EXPECT_EQ(std::memcmp(a.data(), b.data(), a.size() * sizeof(std::complex<double>)), 0);
}

TEST_F(CudaBackendRequestDeviceTest, DenseCountsAreIdenticalOnEitherForcedBackend) {
    const Circuit circuit = layered(10, true);
    RunOptions options{.shots = 2000, .want_statevector = true, .seed = 11};
    options.backend = Backend::Cpu;
    const Result cpu = run(circuit, options);
    options.backend = Backend::Cuda;
    const Result cuda = run(circuit, options);
    ASSERT_EQ(cpu.plan().strategy, Strategy::DirectDense) << "precondition: both runs are dense";
    ASSERT_EQ(cuda.plan().strategy, Strategy::DirectDense);
    EXPECT_EQ(cpu.counts(), cuda.counts());
}

// Phase 17 item 4b: a Factorized plan whose dense regions run on CUDA gives the
// same counts as the all-CPU plan, bit for bit: each region is dense on both
// backends, and counts are sampled on the host from identical states.
TEST_F(CudaBackendRequestDeviceTest, AFactorizedPlanOnTheDeviceGivesIdenticalCounts) {
    Circuit circuit(18, 18);
    for (const auto& [offset, n] : {std::pair<QubitId, std::uint32_t>{0, 6}, {6, 12}}) {
        // A scrambling block, as in benchmarks/cuda_factorized_backend_benchmark.cpp.
        std::mt19937 rng(1700 + n);
        std::vector<std::uint32_t> order(n);
        for (std::uint32_t layer = 0; layer < 6; ++layer) {
            for (std::uint32_t q = 0; q < n; ++q) {
                circuit.append(Opcode::H, {static_cast<QubitId>(offset + q)});
                circuit.append(Opcode::T, {static_cast<QubitId>(offset + q)});
            }
            for (std::uint32_t q = 0; q < n; ++q) {
                order[q] = q;
            }
            std::shuffle(order.begin(), order.end(), rng);
            for (std::uint32_t q = 0; q + 1 < n; q += 2) {
                circuit.append(Opcode::CX,
                               {static_cast<QubitId>(offset + order[q]),
                                static_cast<QubitId>(offset + order[q + 1])});
            }
        }
    }
    for (QubitId q = 0; q < 18; ++q) {
        circuit.append_measure(q, q);
    }
    RunOptions options{.shots = 2000, .seed = 21};
    options.backend = Backend::Cpu;
    const Result cpu = run(circuit, options);
    options.backend = Backend::Cuda;
    const Result cuda = run(circuit, options);
    ASSERT_EQ(cpu.plan().strategy, Strategy::Factorized) << cpu.explain();
    ASSERT_EQ(cuda.plan().strategy, Strategy::Factorized) << cuda.explain();
    for (const Region& region : cuda.plan().regions) {
        EXPECT_EQ(region.backend, Backend::Cuda) << cuda.explain();
    }
    EXPECT_EQ(cpu.counts(), cuda.counts());
}

}  // namespace
}  // namespace menos
