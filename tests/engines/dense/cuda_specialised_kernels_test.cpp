// The specialised 1- and 2-qubit CUDA kernels must be BITWISE identical to
// the generic k-operand CUDA kernel (docs/adr/0020 section 3, owner decision
// Q3). The CPU/CUDA statevector identity of docs/adr/0014 section 10, and
// with it identical counts across backends, rests on every device path
// summing in the generic kernel's order, as the CPU's specialised kernels do
// (specialised_kernels_test.cpp). The comparison is of the bytes.
//
// Skipped (loudly) when no CUDA device is present; it never passes without
// one.

#include <gtest/gtest.h>

#include <complex>
#include <cstdint>
#include <cstring>
#include <random>
#include <vector>

#include "kernels/cuda/device_buffer.h"
#include "kernels/cuda/device_info.h"
#include "kernels/dense_kernels.h"

namespace menos::kernels::cuda {
namespace {

using Complex = std::complex<double>;

std::vector<Complex> random_matrix(std::size_t entries, std::mt19937_64& rng) {
    std::uniform_real_distribution<double> dist(-1.0, 1.0);
    std::vector<Complex> m(entries);
    for (Complex& c : m) {
        c = Complex{dist(rng), dist(rng)};
    }
    return m;
}

// The device has no host-to-device upload in MENOS's API, so a random state is
// built on the device: |0...0>, then a random (non-unitary) 1-qubit matrix on
// every qubit and a random 2-qubit matrix on each neighbouring pair, all
// through the generic kernel. Every amplitude ends up a distinct, non-trivial
// value. The same seed gives the same state, bit for bit.
void fill_random_state(DeviceState state, std::uint32_t n, std::uint64_t seed) {
    std::mt19937_64 rng(seed);
    initialize(state, 0.0);
    for (std::uint32_t q = 0; q < n; ++q) {
        const std::vector<std::uint32_t> operand{q};
        apply_matrix_generic(state, random_matrix(4, rng), operand);
    }
    for (std::uint32_t q = 0; q + 1 < n; ++q) {
        const std::vector<std::uint32_t> pair{q, q + 1};
        apply_matrix_generic(state, random_matrix(16, rng), pair);
    }
}

class CudaSpecialisedKernelsTest : public ::testing::Test {
protected:
    void SetUp() override {
        if (!device_info().present) {
            GTEST_SKIP() << "no CUDA device available -- CUDA specialised-kernel bitwise coverage "
                            "did NOT run in this configuration";
        }
    }

    // Applies matrix at operands to the same random n-qubit state through
    // apply_matrix (which dispatches to the specialised kernel) and
    // apply_matrix_generic, and requires identical bytes.
    static void expect_bitwise_equal_to_generic(const std::vector<Complex>& matrix,
                                                const std::vector<std::uint32_t>& operands,
                                                std::uint32_t n,
                                                std::uint64_t seed) {
        const std::size_t amplitudes = std::size_t{1} << n;
        DeviceBuffer specialised(amplitudes, device_available_bytes());
        DeviceBuffer generic(amplitudes, device_available_bytes());
        fill_random_state(specialised.state(), n, seed);
        fill_random_state(generic.state(), n, seed);
        apply_matrix(specialised.state(), matrix, operands);
        apply_matrix_generic(generic.state(), matrix, operands);
        std::vector<Complex> specialised_host(amplitudes);
        std::vector<Complex> generic_host(amplitudes);
        specialised.copy_to_host(specialised_host);
        generic.copy_to_host(generic_host);
        ASSERT_EQ(
            std::memcmp(specialised_host.data(), generic_host.data(), amplitudes * sizeof(Complex)),
            0)
            << "n=" << n << " operands=" << operands[0] << (operands.size() > 1 ? "," : "")
            << (operands.size() > 1 ? operands[1] : 0);
    }
};

TEST_F(CudaSpecialisedKernelsTest, OneQubitIsBitwiseIdenticalToGenericOnEveryQubit) {
    std::mt19937_64 rng(1601);
    for (std::uint32_t n = 1; n <= 12; ++n) {
        for (std::uint32_t q = 0; q < n; ++q) {
            expect_bitwise_equal_to_generic(random_matrix(4, rng), {q}, n, rng());
        }
    }
}

TEST_F(CudaSpecialisedKernelsTest, TwoQubitIsBitwiseIdenticalToGenericOnEveryOrderedPair) {
    // Every ordered pair covers adjacent and non-adjacent operands in both
    // orders; the order decides which operand is local bit 0.
    std::mt19937_64 rng(1602);
    for (std::uint32_t n = 2; n <= 10; ++n) {
        for (std::uint32_t q0 = 0; q0 < n; ++q0) {
            for (std::uint32_t q1 = 0; q1 < n; ++q1) {
                if (q0 != q1) {
                    expect_bitwise_equal_to_generic(random_matrix(16, rng), {q0, q1}, n, rng());
                }
            }
        }
    }
}

TEST_F(CudaSpecialisedKernelsTest, LargeStatesSpanningManyBlocksStayBitwiseIdentical) {
    // n = 20 is 2^19 pairs and 2^18 groups: thousands of blocks, so the grid
    // and bounds arithmetic are exercised, not one partial block.
    std::mt19937_64 rng(1603);
    constexpr std::uint32_t kN = 20;
    for (const std::uint32_t q : {0U, 9U, 19U}) {
        expect_bitwise_equal_to_generic(random_matrix(4, rng), {q}, kN, rng());
    }
    for (const std::vector<std::uint32_t>& pair :
         std::vector<std::vector<std::uint32_t>>{{0, 1}, {19, 0}, {7, 13}}) {
        expect_bitwise_equal_to_generic(random_matrix(16, rng), pair, kN, rng());
    }
}

TEST_F(CudaSpecialisedKernelsTest, SparseMatricesWithSignedZerosStayBitwiseIdentical) {
    // Named gates are mostly zeros (and a negated zero is a different bit
    // pattern), which is where a kernel that skipped or reordered terms would
    // first show.
    std::mt19937_64 rng(1604);
    const std::vector<Complex> x = {{0.0, 0.0}, {1.0, 0.0}, {1.0, 0.0}, {0.0, 0.0}};
    const std::vector<Complex> neg = {{-0.0, -0.0}, {-1.0, 0.0}, {0.0, -1.0}, {-0.0, 0.0}};
    std::vector<Complex> cx(16, Complex{0.0, 0.0});
    cx[0] = cx[5] = cx[11] = cx[14] = Complex{1.0, 0.0};
    std::vector<Complex> neg_cx(16, Complex{-0.0, -0.0});
    neg_cx[0] = neg_cx[7] = neg_cx[10] = neg_cx[13] = Complex{-1.0, 0.0};
    for (std::uint32_t q = 0; q < 6; ++q) {
        expect_bitwise_equal_to_generic(x, {q}, 6, rng());
        expect_bitwise_equal_to_generic(neg, {q}, 6, rng());
    }
    for (const auto& operands :
         std::vector<std::vector<std::uint32_t>>{{0, 1}, {1, 0}, {0, 5}, {5, 2}}) {
        expect_bitwise_equal_to_generic(cx, operands, 6, rng());
        expect_bitwise_equal_to_generic(neg_cx, operands, 6, rng());
    }
}

TEST_F(CudaSpecialisedKernelsTest, ThreeQubitGatesStillUseTheGenericKernel) {
    // k >= 3 is the generic kernel itself, so this is equality by
    // construction; it pins the dispatch so a later specialisation of k = 3
    // has to come with its own bitwise test.
    std::mt19937_64 rng(1605);
    expect_bitwise_equal_to_generic(random_matrix(64, rng), {4, 0, 2}, 6, rng());
}

}  // namespace
}  // namespace menos::kernels::cuda
