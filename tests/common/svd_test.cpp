#include "common/svd.h"

#include <gtest/gtest.h>

#include <cmath>
#include <complex>
#include <vector>

namespace menos::common {
namespace {

using Complex = std::complex<double>;

// LAPACKE_zgesdd's own numerical accuracy over the small (<= 4x4)
// hand-picked matrices below, not a MENOS algorithm's own tolerance --
// looser than common/tolerance.h's kDefaultAbsTol (which governs
// MENOS's own statevector-vs-oracle agreement), since this only needs
// to catch a wrong row-major/leading-dimension convention, not measure
// MENOS numerical precision.
constexpr double kSvdTestTol = 1e-9;

// Row-major m x k times k x n -> row-major m x n. A swap of m and k
// (the adjacent pair the linter flags) fails loudly here: every call
// site's result is immediately checked by check_svd_reconstructs's own
// ASSERT_EQ size assertions and numeric reconstruction comparison, not
// silently accepted.
// NOLINTBEGIN(bugprone-easily-swappable-parameters)
std::vector<Complex> multiply(const std::vector<Complex>& a,
                              std::size_t m,
                              std::size_t k,
                              const std::vector<Complex>& b,
                              std::size_t n) {
    // NOLINTEND(bugprone-easily-swappable-parameters)
    std::vector<Complex> result(m * n, Complex{0.0, 0.0});
    for (std::size_t i = 0; i < m; ++i) {
        for (std::size_t j = 0; j < n; ++j) {
            Complex sum{0.0, 0.0};
            for (std::size_t p = 0; p < k; ++p) {
                sum += a[i * k + p] * b[p * n + j];
            }
            result[i * n + j] = sum;
        }
    }
    return result;
}

std::vector<Complex> conjugate_transpose(const std::vector<Complex>& a,
                                         std::size_t m,
                                         std::size_t n) {
    std::vector<Complex> result(n * m);
    for (std::size_t i = 0; i < m; ++i) {
        for (std::size_t j = 0; j < n; ++j) {
            result[j * m + i] = std::conj(a[i * n + j]);
        }
    }
    return result;
}

std::vector<Complex> diag(const std::vector<double>& s) {
    std::vector<Complex> result(s.size() * s.size(), Complex{0.0, 0.0});
    for (std::size_t i = 0; i < s.size(); ++i) {
        result[i * s.size() + i] = Complex{s[i], 0.0};
    }
    return result;
}

void expect_matrices_near(const std::vector<Complex>& a,
                          const std::vector<Complex>& b,
                          double tol) {
    ASSERT_EQ(a.size(), b.size());
    for (std::size_t i = 0; i < a.size(); ++i) {
        EXPECT_NEAR(a[i].real(), b[i].real(), tol) << "index " << i;
        EXPECT_NEAR(a[i].imag(), b[i].imag(), tol) << "index " << i;
    }
}

// Checks a is m x k with orthonormal columns: a^H * a == I_k. A swap
// of m and k fails loudly: multiply/expect_matrices_near below it
// immediately assert on the resulting shapes and values.
// NOLINTBEGIN(bugprone-easily-swappable-parameters)
void expect_orthonormal_columns(const std::vector<Complex>& a,
                                std::size_t m,
                                std::size_t k,
                                double tol) {
    // NOLINTEND(bugprone-easily-swappable-parameters)
    const std::vector<Complex> ah = conjugate_transpose(a, m, k);
    const std::vector<Complex> ahaa = multiply(ah, k, m, a, k);
    std::vector<Complex> identity(k * k, Complex{0.0, 0.0});
    for (std::size_t i = 0; i < k; ++i) {
        identity[i * k + i] = Complex{1.0, 0.0};
    }
    expect_matrices_near(ahaa, identity, tol);
}

// Checks a is k x n with orthonormal rows: a * a^H == I_k. A swap of
// k and n fails loudly: multiply/expect_matrices_near below it
// immediately assert on the resulting shapes and values.
// NOLINTBEGIN(bugprone-easily-swappable-parameters)
void expect_orthonormal_rows(const std::vector<Complex>& a,
                             std::size_t k,
                             std::size_t n,
                             double tol) {
    // NOLINTEND(bugprone-easily-swappable-parameters)
    const std::vector<Complex> ah = conjugate_transpose(a, k, n);
    const std::vector<Complex> aah = multiply(a, k, n, ah, k);
    std::vector<Complex> identity(k * k, Complex{0.0, 0.0});
    for (std::size_t i = 0; i < k; ++i) {
        identity[i * k + i] = Complex{1.0, 0.0};
    }
    expect_matrices_near(aah, identity, tol);
}

void check_svd_reconstructs(const std::vector<Complex>& a, std::size_t m, std::size_t n) {
    const SvdResult result = svd(a, m, n);
    const std::size_t k = std::min(m, n);
    ASSERT_EQ(result.singular_values.size(), k);
    ASSERT_EQ(result.u.size(), m * k);
    ASSERT_EQ(result.vt.size(), k * n);

    // Singular values are non-negative and descending (standard LAPACK
    // zgesdd contract).
    for (std::size_t i = 0; i < k; ++i) {
        EXPECT_GE(result.singular_values[i], 0.0);
        if (i > 0) {
            EXPECT_GE(result.singular_values[i - 1], result.singular_values[i]);
        }
    }

    expect_orthonormal_columns(result.u, m, k, kSvdTestTol);
    expect_orthonormal_rows(result.vt, k, n, kSvdTestTol);

    const std::vector<Complex> u_s = multiply(result.u, m, k, diag(result.singular_values), k);
    const std::vector<Complex> reconstructed = multiply(u_s, m, k, result.vt, n);
    expect_matrices_near(reconstructed, a, kSvdTestTol);
}

TEST(SvdTest, IdentityMatrixHasUnitSingularValues) {
    const std::vector<Complex> identity = {
        Complex{1.0, 0.0}, Complex{0.0, 0.0}, Complex{0.0, 0.0}, Complex{1.0, 0.0}};
    check_svd_reconstructs(identity, 2, 2);
    const SvdResult result = svd(identity, 2, 2);
    EXPECT_NEAR(result.singular_values[0], 1.0, kSvdTestTol);
    EXPECT_NEAR(result.singular_values[1], 1.0, kSvdTestTol);
}

TEST(SvdTest, GenericSquareComplexMatrixReconstructs) {
    const std::vector<Complex> a = {
        Complex{1.0, 1.0}, Complex{0.0, -2.0}, Complex{3.0, 0.0}, Complex{-1.0, 0.5}};
    check_svd_reconstructs(a, 2, 2);
}

TEST(SvdTest, TallRectangularMatrixReconstructs) {
    // 3 x 2, m > n.
    const std::vector<Complex> a = {Complex{1.0, 0.0},
                                    Complex{2.0, -1.0},
                                    Complex{0.0, 3.0},
                                    Complex{-1.0, 1.0},
                                    Complex{2.0, 2.0},
                                    Complex{0.5, -0.5}};
    check_svd_reconstructs(a, 3, 2);
}

TEST(SvdTest, WideRectangularMatrixReconstructs) {
    // 2 x 3, m < n.
    const std::vector<Complex> a = {Complex{1.0, 0.0},
                                    Complex{2.0, -1.0},
                                    Complex{0.0, 3.0},
                                    Complex{-1.0, 1.0},
                                    Complex{2.0, 2.0},
                                    Complex{0.5, -0.5}};
    check_svd_reconstructs(a, 2, 3);
}

TEST(SvdTest, NumericalRankOfFullRankIdentityIsDimension) {
    const std::vector<Complex> identity = {
        Complex{1.0, 0.0}, Complex{0.0, 0.0}, Complex{0.0, 0.0}, Complex{1.0, 0.0}};
    const SvdResult result = svd(identity, 2, 2);
    EXPECT_EQ(numerical_rank(result.singular_values, 2, 2), 2u);
}

TEST(SvdTest, NumericalRankOfOuterProductIsOne) {
    // Rank-1: outer product of (1, 2, 3) and (1, 1) -> 3 x 2.
    const std::vector<Complex> a = {Complex{1.0, 0.0},
                                    Complex{1.0, 0.0},
                                    Complex{2.0, 0.0},
                                    Complex{2.0, 0.0},
                                    Complex{3.0, 0.0},
                                    Complex{3.0, 0.0}};
    const SvdResult result = svd(a, 3, 2);
    EXPECT_EQ(numerical_rank(result.singular_values, 3, 2), 1u);
    // The construction is exactly rank-1: the second singular value
    // must be (numerically) zero, not merely small.
    EXPECT_NEAR(result.singular_values[1], 0.0, kSvdTestTol);
}

}  // namespace
}  // namespace menos::common
