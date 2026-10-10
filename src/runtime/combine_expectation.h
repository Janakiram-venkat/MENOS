#pragma once

#include <complex>
#include <span>
#include <vector>

#include "menos/observable.h"

namespace menos::runtime {

// Combines each region's own raw per-term expectation into the job's
// total (docs/adr/0007): for term t with coefficient c_t, the job's
// contribution is c_t * (product over regions of that region's own raw
// <psi_r|P_t|psi_r>) -- the mixed-product property for a tensor-product
// state, the same argument docs/adr/0005 used for statevector
// recombination applied to an operator instead of a state.
// per_region_terms[r] must have exactly observable.terms.size() entries,
// in the same order as observable.terms, for EVERY region including
// idle ones: an idle region's own raw value per term is computed by the
// caller before calling this (1.0 for the identity, 0.0 for any term
// with nonzero support there, since an idle qubit stays exactly |0>) --
// this function does not special-case idleness itself.
[[nodiscard]] std::complex<double> combine_expectation(
    const Observable& observable,
    std::span<const std::vector<std::complex<double>>> per_region_terms);

}  // namespace menos::runtime
