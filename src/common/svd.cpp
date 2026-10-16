#include "common/svd.h"

// This system's installed lapacke.h includes lapack.h directly, which
// only consults LAPACK_COMPLEX_CPP inside an "#ifdef HAVE_LAPACK_CONFIG_H"
// block that is not otherwise reached -- verified by reading
// /usr/include/lapack.h, not assumed from memory. The header's own
// documented alternative (its comment block above the complex-type
// section) is LAPACK_COMPLEX_CUSTOM: define lapack_complex_float and
// lapack_complex_double ourselves before including, which this system's
// lapack.h honors directly (the "#ifndef lapack_complex_double" guard
// short-circuits once we've defined it). This still keeps the LAPACKE
// dependency fully encapsulated in this one translation unit -- svd.h
// exposes only std::complex<double>, no reinterpret_cast or _Complex
// GNU extension anywhere past this file.
#define LAPACK_COMPLEX_CUSTOM
#define lapack_complex_float std::complex<float>
#define lapack_complex_double std::complex<double>
#ifdef _MSC_VER
// C4190: lapacke.h declares lapack_make_complex_float/double extern "C" while
// returning the std::complex types defined above. MENOS never calls them; the
// functions it does call take only pointers and integers (docs/adr/0022).
#pragma warning(push)
#pragma warning(disable : 4190)
#endif
#include <lapacke.h>
#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include <algorithm>
#include <string>

#include "common/error.h"
#include "common/tolerance.h"

namespace menos::common {

SvdResult svd(std::span<const std::complex<double>> a, std::size_t m, std::size_t n) {
    ensure(a.size() == m * n, "svd: matrix size does not match m * n");
    ensure(m > 0 && n > 0, "svd: m and n must both be positive");

    const std::size_t k = std::min(m, n);
    // LAPACKE_zgesdd destroys its input matrix (standard LAPACK
    // convention) -- copy first so the caller's data is untouched.
    std::vector<std::complex<double>> a_copy(a.begin(), a.end());

    SvdResult result;
    result.singular_values.resize(k);
    result.u.resize(m * k);
    result.vt.resize(k * n);

    // Row-major throughout (matches this codebase's matrix convention);
    // jobz='S' is the thin/economy SVD, exactly k = min(m, n) singular
    // values and vectors -- what every caller (Schmidt-rank bound, MPS
    // truncation) needs. Leading dimensions follow LAPACKE's row-major
    // rule (lda/ldu/ldvt = the number of columns of that specific
    // array): lda = n for the m x n input, ldu = k for the m x k left
    // singular vectors, ldvt = n for the k x n right singular vectors
    // (conjugate-transposed convention). Verified end-to-end against
    // reconstruction in tests/common/svd_test.cpp, not assumed from
    // memory (CLAUDE.md: API behavior comes from the installed source
    // or a measurement).
    const lapack_int info = LAPACKE_zgesdd(LAPACK_ROW_MAJOR,
                                           'S',
                                           static_cast<lapack_int>(m),
                                           static_cast<lapack_int>(n),
                                           a_copy.data(),
                                           static_cast<lapack_int>(n),
                                           result.singular_values.data(),
                                           result.u.data(),
                                           static_cast<lapack_int>(k),
                                           result.vt.data(),
                                           static_cast<lapack_int>(n));
    ensure(info == 0, "svd: LAPACKE_zgesdd failed with info=" + std::to_string(info));

    return result;
}

std::size_t numerical_rank(const std::vector<double>& singular_values,
                           std::size_t m,
                           std::size_t n) {
    if (singular_values.empty()) {
        return 0;
    }
    const double sigma_max = singular_values.front();
    const double cutoff = sigma_max * static_cast<double>(std::max(m, n)) * kSvdRankRelativeEpsilon;
    std::size_t rank = 0;
    for (const double sigma : singular_values) {
        if (sigma > cutoff) {
            ++rank;
        }
    }
    return rank;
}

}  // namespace menos::common
