// The specialised 1- and 2-qubit CPU kernels must be BITWISE identical to the
// generic k-operand kernel (docs/adr/0019 section 4, owner decision Q4): the
// CPU/CUDA statevector identity of docs/adr/0014 section 10, and with it
// identical counts across backends, rests on every CPU path summing in the
// generic kernel's order. Tolerance is not enough here; the comparison is of
// the bytes. Agreement with an independent oracle is apply_matrix_test.cpp's
// job.

#include <gtest/gtest.h>

#include <complex>
#include <cstdint>
#include <cstring>
#include <random>
#include <span>
#include <vector>

#include "kernels/dense_kernels.h"

namespace menos::kernels::cpu {
namespace {

using Complex = std::complex<double>;

std::vector<Complex> random_vector(std::size_t size, std::mt19937_64& rng) {
    std::uniform_real_distribution<double> dist(-1.0, 1.0);
    std::vector<Complex> v(size);
    for (Complex& c : v) {
        c = Complex{dist(rng), dist(rng)};
    }
    return v;
}

bool bytes_equal(const std::vector<Complex>& a, const std::vector<Complex>& b) {
    return a.size() == b.size() && std::memcmp(a.data(), b.data(), a.size() * sizeof(Complex)) == 0;
}

// Applies matrix at operands to the same random state through apply_matrix
// (which dispatches to the specialised kernel) and apply_matrix_generic, and
// requires identical bytes.
void expect_bitwise_equal_to_generic(const std::vector<Complex>& matrix,
                                     const std::vector<std::uint32_t>& operands,
                                     std::uint32_t n,
                                     std::mt19937_64& rng) {
    const std::vector<Complex> initial = random_vector(std::size_t{1} << n, rng);
    std::vector<Complex> specialised = initial;
    std::vector<Complex> generic = initial;
    apply_matrix(specialised, matrix, operands, /*num_threads=*/1);
    apply_matrix_generic(generic, matrix, operands, /*num_threads=*/1);
    ASSERT_TRUE(bytes_equal(specialised, generic))
        << "n=" << n << " operands=" << operands[0] << (operands.size() > 1 ? "," : "")
        << (operands.size() > 1 ? operands[1] : 0);
}

TEST(SpecialisedKernelsTest, OneQubitIsBitwiseIdenticalToGenericOnEveryQubit) {
    std::mt19937_64 rng(1501);
    for (std::uint32_t n = 1; n <= 10; ++n) {
        for (std::uint32_t q = 0; q < n; ++q) {
            expect_bitwise_equal_to_generic(random_vector(4, rng), {q}, n, rng);
        }
    }
}

TEST(SpecialisedKernelsTest, TwoQubitIsBitwiseIdenticalToGenericOnEveryOrderedPair) {
    // Every ordered pair covers adjacent and non-adjacent operands in both
    // orders; the order decides which operand is local bit 0.
    std::mt19937_64 rng(1502);
    for (std::uint32_t n = 2; n <= 9; ++n) {
        for (std::uint32_t q0 = 0; q0 < n; ++q0) {
            for (std::uint32_t q1 = 0; q1 < n; ++q1) {
                if (q0 != q1) {
                    expect_bitwise_equal_to_generic(random_vector(16, rng), {q0, q1}, n, rng);
                }
            }
        }
    }
}

TEST(SpecialisedKernelsTest, SparseMatricesWithSignedZerosStayBitwiseIdentical) {
    // Named gates are mostly zeros (and a negated zero is a different bit
    // pattern), which is where a summation that skipped or reordered terms
    // would first show.
    std::mt19937_64 rng(1503);
    const std::vector<Complex> x = {{0.0, 0.0}, {1.0, 0.0}, {1.0, 0.0}, {0.0, 0.0}};
    const std::vector<Complex> neg = {{-0.0, -0.0}, {-1.0, 0.0}, {0.0, -1.0}, {-0.0, 0.0}};
    std::vector<Complex> cx(16, Complex{0.0, 0.0});
    cx[0] = cx[5] = cx[11] = cx[14] = Complex{1.0, 0.0};
    std::vector<Complex> neg_cx(16, Complex{-0.0, -0.0});
    neg_cx[0] = neg_cx[7] = neg_cx[10] = neg_cx[13] = Complex{-1.0, 0.0};
    for (std::uint32_t q = 0; q < 6; ++q) {
        expect_bitwise_equal_to_generic(x, {q}, 6, rng);
        expect_bitwise_equal_to_generic(neg, {q}, 6, rng);
    }
    for (const auto& operands :
         std::vector<std::vector<std::uint32_t>>{{0, 1}, {1, 0}, {0, 5}, {5, 2}}) {
        expect_bitwise_equal_to_generic(cx, operands, 6, rng);
        expect_bitwise_equal_to_generic(neg_cx, operands, 6, rng);
    }
}

TEST(SpecialisedKernelsTest, ThreeQubitGatesStillUseTheGenericKernel) {
    // k >= 3 is the generic kernel itself, so this is equality by
    // construction; it pins the dispatch so a later specialisation of k = 3
    // has to come with its own bitwise test.
    std::mt19937_64 rng(1504);
    expect_bitwise_equal_to_generic(random_vector(64, rng), {4, 0, 2}, 6, rng);
}

}  // namespace
}  // namespace menos::kernels::cpu
