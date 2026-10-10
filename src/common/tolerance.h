#pragma once

#include <limits>

namespace menos::common {

// Absolute tolerance for complex128 amplitude comparisons. Derivation
// (CLAUDE.md testing rules): "Start at 1e-12 absolute for complex128 on
// circuits up to 20 qubits and 500 gates. Loosen only with written
// justification." Used both for comparing a computed statevector against
// the Qiskit oracle and for checking that a supplied Unitary operand is
// unitary within tolerance -- a single named constant rather than two
// near-identical ones.
inline constexpr double kDefaultAbsTol = 1e-12;

// Relative epsilon for declaring an SVD singular value numerically zero
// (Phase 6, src/common/svd.cpp's numerical_rank). Derivation: this is
// the exact default convention used by numpy.linalg.matrix_rank(M),
// which treats singular value sigma as zero when
// sigma <= sigma_max * max(M.shape) * eps, eps = the dtype's machine
// epsilon (numpy's default `tol` argument, computed from
// `S.max() * max(M.shape) * finfo(S.dtype).eps` when `tol` is not
// given). For complex128 (float64 singular values), eps is exactly
// std::numeric_limits<double>::epsilon() = 2^-52. Reused verbatim
// rather than inventing a new threshold, per CLAUDE.md's "never invent
// mathematics" rule -- this is a widely cited standard, not a MENOS
// judgment call.
inline constexpr double kSvdRankRelativeEpsilon = std::numeric_limits<double>::epsilon();

}  // namespace menos::common
