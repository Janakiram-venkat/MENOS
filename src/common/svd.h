#pragma once

#include <complex>
#include <cstddef>
#include <span>
#include <vector>

namespace menos::common {

// Thin (economy) singular value decomposition of an m x n complex128
// matrix a: a = u * diag(singular_values) * vt, with k = min(m, n).
// u is m x k, vt is k x n, singular_values has k entries in descending
// order. Matches LAPACKE_zgesdd's jobz='S' contract exactly -- this is
// what both the Schmidt-rank bound (src/analysis/schmidt_rank_bound.cpp)
// and the MPS engine's truncated gate application need: the full set of
// k singular values/vectors, never fewer.
struct SvdResult {
    std::vector<std::complex<double>> u;
    std::vector<double> singular_values;
    std::vector<std::complex<double>> vt;
};

// Computes the thin SVD of the m x n row-major matrix a (a.size() must
// be m * n). Throws InternalError if the underlying LAPACKE call
// reports a non-zero info (should not happen for a finite input matrix;
// a NaN/Inf entry is a caller bug, not a runtime condition to recover
// from).
[[nodiscard]] SvdResult svd(std::span<const std::complex<double>> a, std::size_t m, std::size_t n);

// The numerical rank of a matrix given its singular values (as returned
// by svd() above) and the matrix's original dimensions m, n: the count
// of singular values strictly greater than the standard cutoff
// singular_values[0] * max(m, n) * kSvdRankRelativeEpsilon (see
// tolerance.h for the derivation). Returns 0 for an all-zero matrix.
[[nodiscard]] std::size_t numerical_rank(const std::vector<double>& singular_values,
                                         std::size_t m,
                                         std::size_t n);

}  // namespace menos::common
