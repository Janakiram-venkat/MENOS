#pragma once

#include <complex>
#include <cstdint>
#include <vector>

#include "analysis/cut.h"

namespace menos::runtime {

// Reconstructs one observable term's expectation from already-computed
// fragment values (docs/adr/0013 Eq. 2.3):
//
//     <P> = sum over branches ( prod_k c_{i_k} ) * prod_f <fragment f's value>
//
// with 8^K branches for K cuts, and c in {+1/2, -1/2} from the wire-cut basis.
//
// This is NOT runtime::combine_expectation. That one takes a product over
// regions, which is right for a tensor-product factorization; a cut needs a
// SIGNED SUM over branches of products over fragments. Reusing it would drop
// the coefficients and the sum entirely.
//
// fragment_values[f][a] is fragment f's expectation for its assignment index
// a, as enumerated by analysis::enumerate_assignments(). Branches are visited
// in a fixed order so the floating-point summation order is deterministic
// (CLAUDE.md's determinism domain invariant: an unpinned order changes the
// last bits between otherwise-identical runs).
[[nodiscard]] std::complex<double> recombine_cut_term(
    const analysis::CutPlan& cut_plan,
    const std::vector<std::vector<std::complex<double>>>& fragment_values);

// The number of branches, 8^K, for a plan with K cuts. Throws ResourceExceeded
// rather than wrapping if that overflows.
[[nodiscard]] std::uint64_t cut_branch_count(const analysis::CutPlan& cut_plan);

}  // namespace menos::runtime
