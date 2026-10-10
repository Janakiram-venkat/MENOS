#include "runtime/recombine_statevector.h"

#include <gtest/gtest.h>

#include <numbers>

#include "common/error.h"
#include "common/tolerance.h"

namespace menos::runtime {
namespace {

using Complex = std::complex<double>;

TEST(AllocateGlobalStatevectorTest, InitializesToAllOnes) {
    const auto global = allocate_global_statevector(2, /*budget_bytes=*/1'000'000);
    ASSERT_EQ(global.size(), 4U);
    for (const auto& amp : global) {
        EXPECT_EQ(amp, Complex(1.0, 0.0));
    }
}

TEST(AllocateGlobalStatevectorTest, ThrowsResourceExceededBeforeAllocatingWhenOverBudget) {
    EXPECT_THROW(static_cast<void>(allocate_global_statevector(20, /*budget_bytes=*/10)),
                 common::ResourceExceeded);
}

TEST(MultiplyRegionIntoGlobalTest, ContiguousTwoQubitRegionMultipliesCorrectly) {
    // Region covers global qubits {0,1} (contiguous, identity gather).
    Region region{.local_to_global = {0, 1},
                  .global_to_local = {0, 1},
                  .operations = {},
                  .engine = Engine::Dense,
                  .backend = Backend::Cpu,
                  .engine_selection_reason = "",
                  .observable = std::nullopt,
                  .estimated_state_bytes = 0,
                  .estimated_work_units = 0};
    auto global = allocate_global_statevector(2, 1'000'000);
    const std::vector<Complex> psi_r = {{2.0, 0.0}, {3.0, 0.0}, {5.0, 0.0}, {7.0, 0.0}};
    multiply_region_into_global(global, region, psi_r);
    EXPECT_EQ(global, psi_r);  // global started all-1.0, so this is exactly psi_r
}

TEST(MultiplyRegionIntoGlobalTest, InterleavedRegionGathersCorrectBitPositions) {
    // Region owns global qubits {0,2} out of 4 total (interleaved with
    // qubits {1,3}, owned by a different region). local bit0 <- global
    // qubit 0, local bit1 <- global qubit 2. Worked example from
    // docs/BUILD_PLAN.md 3.3 / the approved plan: global index 0b1011=11
    // (q0=1,q1=1,q2=0,q3=1) gathers to local index 0b01=1 (bit0=q0=1,
    // bit1=q2=0).
    Region region{.local_to_global = {0, 2},
                  .global_to_local = {},  // not used by multiply_region_into_global
                  .operations = {},
                  .engine = Engine::Dense,
                  .backend = Backend::Cpu,
                  .engine_selection_reason = "",
                  .observable = std::nullopt,
                  .estimated_state_bytes = 0,
                  .estimated_work_units = 0};
    auto global = allocate_global_statevector(4, 1'000'000);
    // psi_r indexed by the region's own 2-bit local index (4 amplitudes);
    // give each a distinct value so gather correctness is checkable.
    const std::vector<Complex> psi_r = {{10.0, 0.0}, {20.0, 0.0}, {30.0, 0.0}, {40.0, 0.0}};
    multiply_region_into_global(global, region, psi_r);

    // Global index 11 = 0b1011 -> local index 0b01 = 1 -> psi_r[1] = 20.
    EXPECT_EQ(global[11], Complex(20.0, 0.0));
    // Global index 0 -> local index 0 -> psi_r[0] = 10.
    EXPECT_EQ(global[0], Complex(10.0, 0.0));
    // Global index 0b0100 (q2=1, rest 0) -> local bit1=q2=1, bit0=q0=0 -> local=0b10=2 ->
    // psi_r[2]=30.
    EXPECT_EQ(global[0b0100], Complex(30.0, 0.0));
    // Global index 0b0101 (q0=1,q2=1) -> local=0b11=3 -> psi_r[3]=40.
    EXPECT_EQ(global[0b0101], Complex(40.0, 0.0));
}

TEST(MultiplyRegionIntoGlobalTest, MultipleRegionsAccumulateAsTensorProduct) {
    // Two contiguous regions {0} and {1}: multiplying both in should give
    // the outer product, matching Kronecker(psi_0, psi_1) in little-endian
    // qubit order (qubit 0 is the fast-varying index).
    Region region0{.local_to_global = {0},
                   .global_to_local = {},
                   .operations = {},
                   .engine = Engine::Dense,
                   .backend = Backend::Cpu,
                   .engine_selection_reason = "",
                   .observable = std::nullopt,
                   .estimated_state_bytes = 0,
                   .estimated_work_units = 0};
    Region region1{.local_to_global = {1},
                   .global_to_local = {},
                   .operations = {},
                   .engine = Engine::Dense,
                   .backend = Backend::Cpu,
                   .engine_selection_reason = "",
                   .observable = std::nullopt,
                   .estimated_state_bytes = 0,
                   .estimated_work_units = 0};
    auto global = allocate_global_statevector(2, 1'000'000);
    const std::vector<Complex> psi_0 = {{1.0, 0.0}, {2.0, 0.0}};
    const std::vector<Complex> psi_1 = {{3.0, 0.0}, {5.0, 0.0}};
    multiply_region_into_global(global, region0, psi_0);
    multiply_region_into_global(global, region1, psi_1);

    // index = q1*2 + q0 (little-endian): global[0]=psi0[0]*psi1[0]=3,
    // global[1]=psi0[1]*psi1[0]=6, global[2]=psi0[0]*psi1[1]=5,
    // global[3]=psi0[1]*psi1[1]=10.
    EXPECT_EQ(global[0], Complex(3.0, 0.0));
    EXPECT_EQ(global[1], Complex(6.0, 0.0));
    EXPECT_EQ(global[2], Complex(5.0, 0.0));
    EXPECT_EQ(global[3], Complex(10.0, 0.0));
}

}  // namespace
}  // namespace menos::runtime
