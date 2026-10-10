#include "runtime/recombine_cut.h"

#include <span>

#include "common/checked_math.h"
#include "common/error.h"
#include "ir/wire_cut_basis.h"

namespace menos::runtime {

std::uint64_t cut_branch_count(const analysis::CutPlan& cut_plan) {
    std::uint64_t branches = 1;
    for (std::size_t i = 0; i < cut_plan.cuts.size(); ++i) {
        branches = common::checked_mul(branches, ir::kWireCutTermCount);
    }
    return branches;
}

std::complex<double> recombine_cut_term(
    const analysis::CutPlan& cut_plan,
    const std::vector<std::vector<std::complex<double>>>& fragment_values) {
    common::ensure(fragment_values.size() == cut_plan.fragments.size(),
                   "recombine_cut_term: one value table per fragment is required");
    const std::span<const ir::WireCutTerm> terms = ir::wire_cut_terms();
    const std::size_t cut_count = cut_plan.cuts.size();
    const std::uint64_t branches = cut_branch_count(cut_plan);

    std::complex<double> total{0.0, 0.0};
    std::vector<std::size_t> branch_terms(cut_count, 0);

    for (std::uint64_t branch = 0; branch < branches; ++branch) {
        // Decode the branch into one wire-cut term index per cut, in a fixed
        // radix-8 order.
        std::uint64_t rest = branch;
        for (std::size_t c = 0; c < cut_count; ++c) {
            branch_terms[c] = static_cast<std::size_t>(rest % ir::kWireCutTermCount);
            rest /= ir::kWireCutTermCount;
        }

        // prod_k c_{i_k}
        double coefficient = 1.0;
        for (std::size_t c = 0; c < cut_count; ++c) {
            coefficient *= terms[branch_terms[c]].coefficient;
        }

        // prod_f <fragment f's value for the assignment this branch selects>
        std::complex<double> product{1.0, 0.0};
        for (std::size_t f = 0; f < cut_plan.fragments.size(); ++f) {
            const std::size_t index =
                analysis::assignment_index_for_branch(cut_plan.fragments[f], branch_terms);
            common::ensure(index < fragment_values[f].size(),
                           "recombine_cut_term: fragment value table is too small for the "
                           "assignment this branch selects -- internal invariant violated");
            product *= fragment_values[f][index];
        }
        total += coefficient * product;
    }
    return total;
}

}  // namespace menos::runtime
