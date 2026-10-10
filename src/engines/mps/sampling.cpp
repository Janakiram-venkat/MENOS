#include "engines/mps/sampling.h"

#include <cmath>
#include <complex>

namespace menos::engines::mps {

namespace {
using Complex = std::complex<double>;
}  // namespace

MPSCountsSampler::MPSCountsSampler(MPSState state,
                                   std::span<const std::pair<QubitId, ClbitId>> measure_targets,
                                   std::uint32_t bitstring_width)
    : state_(std::move(state)),
      measure_targets_(measure_targets.begin(), measure_targets.end()),
      bitstring_width_(bitstring_width) {
    state_.move_orthogonality_center_to(0);
}

BitString MPSCountsSampler::sample_one(common::Rng& rng) const {
    const std::uint32_t n = state_.num_qubits();
    std::vector<Complex> left = {Complex{1.0, 0.0}};
    std::vector<bool> outcomes(n);

    for (std::uint32_t i = 0; i < n; ++i) {
        const SiteTensor& site = state_.site(i);
        std::vector<Complex> c0(site.chi_right, Complex{0.0, 0.0});
        std::vector<Complex> c1(site.chi_right, Complex{0.0, 0.0});
        for (std::size_t l = 0; l < site.chi_left; ++l) {
            for (std::size_t r = 0; r < site.chi_right; ++r) {
                c0[r] += left[l] * site.at(l, 0, r);
                c1[r] += left[l] * site.at(l, 1, r);
            }
        }
        double p0 = 0.0;
        double p1 = 0.0;
        for (std::size_t r = 0; r < site.chi_right; ++r) {
            p0 += std::norm(c0[r]);
            p1 += std::norm(c1[r]);
        }
        const double total = p0 + p1;
        const double u = rng.next_uniform();
        const bool outcome1 = (u * total) >= p0;
        outcomes[i] = outcome1;

        const std::vector<Complex>& chosen = outcome1 ? c1 : c0;
        const double p_chosen = outcome1 ? p1 : p0;
        const double scale = 1.0 / std::sqrt(p_chosen);
        left.assign(site.chi_right, Complex{0.0, 0.0});
        for (std::size_t r = 0; r < site.chi_right; ++r) {
            left[r] = chosen[r] * scale;
        }
    }

    BitString result(bitstring_width_);
    for (const auto& [qubit, clbit] : measure_targets_) {
        result.set(clbit, outcomes[qubit]);
    }
    return result;
}

}  // namespace menos::engines::mps
