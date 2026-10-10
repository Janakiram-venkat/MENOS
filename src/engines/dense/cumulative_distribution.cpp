#include "engines/dense/cumulative_distribution.h"

#include <algorithm>
#include <complex>

#include "common/rng.h"

namespace menos::engines::dense {

template <typename ProbabilityOf>
void CumulativeDistribution::build(
    std::size_t size,
    ProbabilityOf probability_of,
    std::span<const std::pair<std::uint32_t, std::uint32_t>> measure_targets,
    std::uint32_t bitstring_width) {
    cumulative_.reserve(size);
    outcomes_.reserve(size);

    double cumulative = 0.0;
    for (std::size_t basis = 0; basis < size; ++basis) {
        cumulative += probability_of(basis);
        cumulative_.push_back(cumulative);

        BitString outcome(bitstring_width);
        for (const auto& [qubit, clbit] : measure_targets) {
            if ((basis >> qubit) & std::size_t{1}) {
                outcome.set(clbit, true);
            }
        }
        outcomes_.push_back(std::move(outcome));
    }
}

CumulativeDistribution::CumulativeDistribution(
    const StateVector& state,
    std::span<const std::pair<std::uint32_t, std::uint32_t>> measure_targets,
    std::uint32_t bitstring_width) {
    const std::span<const std::complex<double>> amps = state.amplitudes();
    build(
        amps.size(),
        [amps](std::size_t basis) { return std::norm(amps[basis]); },
        measure_targets,
        bitstring_width);
}

CumulativeDistribution::CumulativeDistribution(
    std::span<const double> probabilities,
    std::span<const std::pair<std::uint32_t, std::uint32_t>> measure_targets,
    std::uint32_t bitstring_width) {
    build(
        probabilities.size(),
        [probabilities](std::size_t basis) { return probabilities[basis]; },
        measure_targets,
        bitstring_width);
}

BitString CumulativeDistribution::sample_one(double uniform) const {
    const auto it = std::upper_bound(cumulative_.begin(), cumulative_.end(), uniform);
    // Floating point rounding may leave the cumulative sum just short of
    // 1.0; fall back to the last outcome rather than reading past the end
    // (mirrors engines::dense::sample()'s equivalent fallback).
    const std::size_t index = (it == cumulative_.end())
                                  ? outcomes_.size() - 1
                                  : static_cast<std::size_t>(it - cumulative_.begin());
    return outcomes_[index];
}

BitString CumulativeDistribution::sample_one(common::Rng& rng) const {
    return sample_one(common::Rng::word_to_uniform(rng.next_word()));
}

}  // namespace menos::engines::dense
