#include "runtime/combine_expectation.h"

#include "common/error.h"

namespace menos::runtime {

std::complex<double> combine_expectation(
    const Observable& observable,
    std::span<const std::vector<std::complex<double>>> per_region_terms) {
    const std::size_t num_terms = observable.terms.size();
    std::complex<double> total{0.0, 0.0};
    for (std::size_t t = 0; t < num_terms; ++t) {
        std::complex<double> product{1.0, 0.0};
        for (const std::vector<std::complex<double>>& region_terms : per_region_terms) {
            common::ensure(region_terms.size() == num_terms,
                           "combine_expectation: a region's per-term vector length does not "
                           "match the observable's term count");
            product *= region_terms[t];
        }
        total += observable.terms[t].second * product;
    }
    return total;
}

}  // namespace menos::runtime
