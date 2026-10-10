#include "engines/dense/apply_matrix.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <complex>
#include <cstdint>
#include <numeric>
#include <random>
#include <vector>

#include "common/tolerance.h"
#include "ir/gate_table.h"

namespace menos::engines::dense {
namespace {

using Complex = std::complex<double>;
using Matrix = std::vector<Complex>;

// Independent oracle: embeds a k-qubit gate matrix into the full 2^n x 2^n
// space by brute-force enumeration, not by zero-bit insertion. Full
// indices row/col contribute a nonzero entry only when they agree on every
// non-operand bit; when they do, the entry is looked up from the small
// gate matrix using the LOCAL indices formed by reading bit operands[j] of
// row/col into local bit j -- the same little-endian, operand-order
// convention apply_matrix is supposed to implement, but derived here from
// first principles instead of from apply_matrix's own algorithm.
Matrix kronecker_embed(const Matrix& gate_matrix,
                       const std::vector<std::uint32_t>& operands,
                       int n) {
    const std::size_t k = operands.size();
    const std::size_t dim = std::size_t{1} << k;
    const std::size_t full_dim = std::size_t{1} << n;
    Matrix full(full_dim * full_dim, Complex{0.0, 0.0});

    std::uint64_t operand_mask = 0;
    for (std::uint32_t q : operands) {
        operand_mask |= (std::uint64_t{1} << q);
    }

    auto local_index = [&](std::size_t full_index) {
        std::size_t local = 0;
        for (std::size_t j = 0; j < k; ++j) {
            if ((full_index >> operands[j]) & std::size_t{1}) {
                local |= (std::size_t{1} << j);
            }
        }
        return local;
    };

    for (std::size_t row = 0; row < full_dim; ++row) {
        for (std::size_t col = 0; col < full_dim; ++col) {
            const std::uint64_t non_operand_diff =
                (static_cast<std::uint64_t>(row) ^ static_cast<std::uint64_t>(col)) & ~operand_mask;
            if (non_operand_diff != 0) {
                continue;  // differ outside the operand bits -> zero entry
            }
            const std::size_t row_local = local_index(row);
            const std::size_t col_local = local_index(col);
            full[row * full_dim + col] = gate_matrix[row_local * dim + col_local];
        }
    }
    return full;
}

std::vector<Complex> matvec(const Matrix& full_matrix, const std::vector<Complex>& v) {
    const std::size_t dim = v.size();
    std::vector<Complex> out(dim, Complex{0.0, 0.0});
    for (std::size_t r = 0; r < dim; ++r) {
        Complex sum{0.0, 0.0};
        for (std::size_t c = 0; c < dim; ++c) {
            sum += full_matrix[r * dim + c] * v[c];
        }
        out[r] = sum;
    }
    return out;
}

// n (a qubit count, always small) and seed (an arbitrary RNG seed) are not
// realistically confusable at any call site in this file.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
std::vector<Complex> arbitrary_state(int n, std::uint32_t seed) {
    std::mt19937 rng(seed);
    std::uniform_real_distribution<double> dist(-1.0, 1.0);
    const std::size_t dim = std::size_t{1} << n;
    std::vector<Complex> v(dim);
    for (std::size_t i = 0; i < dim; ++i) {
        v[i] = Complex{dist(rng), dist(rng)};
    }
    return v;
}

void expect_apply_matrix_matches_oracle(const Matrix& gate_matrix,
                                        const std::vector<std::uint32_t>& operands,
                                        int n,
                                        std::uint32_t seed) {
    const std::vector<Complex> initial = arbitrary_state(n, seed);

    const Matrix full = kronecker_embed(gate_matrix, operands, n);
    const std::vector<Complex> expected = matvec(full, initial);

    std::vector<Complex> actual = initial;
    apply_matrix(actual, gate_matrix, operands);

    ASSERT_EQ(actual.size(), expected.size());
    for (std::size_t i = 0; i < actual.size(); ++i) {
        EXPECT_NEAR(actual[i].real(), expected[i].real(), common::kDefaultAbsTol)
            << "index " << i << " (real), operands seed " << seed;
        EXPECT_NEAR(actual[i].imag(), expected[i].imag(), common::kDefaultAbsTol)
            << "index " << i << " (imag), operands seed " << seed;
    }
}

// All permutations of a fixed subset of qubits, so both adjacent and
// non-adjacent, both forward and reversed operand orders are exercised.
std::vector<std::vector<std::uint32_t>> all_permutations(std::vector<std::uint32_t> qubits) {
    std::sort(qubits.begin(), qubits.end());
    std::vector<std::vector<std::uint32_t>> perms;
    do {
        perms.push_back(qubits);
    } while (std::next_permutation(qubits.begin(), qubits.end()));
    return perms;
}

TEST(ApplyMatrixKroneckerOracleTest, TwoQubitGatesAllOperandOrders) {
    const int n = 4;
    const std::vector<Opcode> two_qubit_gates{Opcode::CX, Opcode::CZ, Opcode::SWAP};
    // Adjacent {0,1} and non-adjacent {0,3} qubit pairs, both orders.
    for (const std::vector<std::uint32_t>& operands : {std::vector<std::uint32_t>{0, 1},
                                                       std::vector<std::uint32_t>{1, 0},
                                                       std::vector<std::uint32_t>{0, 3},
                                                       std::vector<std::uint32_t>{3, 0},
                                                       std::vector<std::uint32_t>{1, 3},
                                                       std::vector<std::uint32_t>{3, 1}}) {
        for (Opcode opcode : two_qubit_gates) {
            const Matrix matrix = ir::generate_matrix(opcode, {});
            expect_apply_matrix_matches_oracle(matrix, operands, n, /*seed=*/42);
        }
    }
}

TEST(ApplyMatrixKroneckerOracleTest, CpAllOperandOrders) {
    const int n = 4;
    const std::vector<double> params{1.23};
    for (const std::vector<std::uint32_t>& operands : {std::vector<std::uint32_t>{0, 1},
                                                       std::vector<std::uint32_t>{1, 0},
                                                       std::vector<std::uint32_t>{0, 3},
                                                       std::vector<std::uint32_t>{3, 0}}) {
        const Matrix matrix = ir::generate_matrix(Opcode::CP, params);
        expect_apply_matrix_matches_oracle(matrix, operands, n, /*seed=*/7);
    }
}

TEST(ApplyMatrixKroneckerOracleTest, CcxAllOperandPermutationsAdjacent) {
    const int n = 5;
    const Matrix matrix = ir::generate_matrix(Opcode::CCX, {});
    for (const std::vector<std::uint32_t>& operands : all_permutations({0, 1, 2})) {
        expect_apply_matrix_matches_oracle(matrix, operands, n, /*seed=*/99);
    }
}

TEST(ApplyMatrixKroneckerOracleTest, CcxAllOperandPermutationsNonAdjacent) {
    const int n = 5;
    const Matrix matrix = ir::generate_matrix(Opcode::CCX, {});
    for (const std::vector<std::uint32_t>& operands : all_permutations({0, 2, 4})) {
        expect_apply_matrix_matches_oracle(matrix, operands, n, /*seed=*/123);
    }
}

TEST(ApplyMatrixKroneckerOracleTest, SingleQubitGateOnEveryQubit) {
    const int n = 4;
    const Matrix matrix = ir::generate_matrix(Opcode::H, {});
    for (std::uint32_t q = 0; q < static_cast<std::uint32_t>(n); ++q) {
        expect_apply_matrix_matches_oracle(matrix, {q}, n, /*seed=*/q + 1);
    }
}

TEST(ApplyMatrixKroneckerOracleTest, AsymmetricUnitaryOnNonAdjacentReversedOperands) {
    // An explicit, deliberately asymmetric (non-Hermitian-looking, no
    // special symmetry) 4x4 unitary: a permutation-plus-phase matrix.
    const Matrix matrix{
        Complex{0, 0},
        Complex{1, 0},
        Complex{0, 0},
        Complex{0, 0},
        Complex{0, 0},
        Complex{0, 0},
        Complex{0, 1},
        Complex{0, 0},
        Complex{0, 0},
        Complex{0, 0},
        Complex{0, 0},
        Complex{-1, 0},
        Complex{1, 0},
        Complex{0, 0},
        Complex{0, 0},
        Complex{0, 0},
    };
    const int n = 5;
    for (const std::vector<std::uint32_t>& operands :
         {std::vector<std::uint32_t>{4, 1}, std::vector<std::uint32_t>{1, 4}}) {
        expect_apply_matrix_matches_oracle(matrix, operands, n, /*seed=*/55);
    }
}

}  // namespace
}  // namespace menos::engines::dense
