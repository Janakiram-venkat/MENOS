#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <span>
#include <utility>
#include <vector>

#include "menos/bitstring.h"
#include "menos/circuit.h"

namespace menos::mitigation {

// Tensored readout-error mitigation (Phase 14, docs/adr/0018 sections 3-4).
//
// Every dense vector here has 2^m entries over the m measured clbits: bit k of
// an index is the k-th entry of the measured-clbit list, which is in ascending
// clbit order.

// A 2x2 matrix, row-major: {a, b, c, d} is [[a, b], [c, d]].
using Matrix2 = std::array<double, 4>;

// The surviving (qubit, clbit) measurement pairs of a circuit, in ascending
// clbit order (the global last-write-wins rule, ir::compute_final_writes).
[[nodiscard]] std::vector<std::pair<QubitId, ClbitId>> measured_pairs(const Circuit& circuit);

// The estimated readout error of one measured clbit: e0 = P(record 1 | true 0)
// and e1 = P(record 0 | true 1).
struct ClbitEstimate {
    double e0;
    double e1;
};

// Estimates each clbit's readout error from the counts of the all-zero and
// all-one calibration circuits, K shots each. Throws UnsupportedFeature, naming
// the clbit, when 1 - e0 - e1 <= 0: a readout no better than a coin flip has no
// meaningful inverse. No tolerance constant is involved.
[[nodiscard]] std::vector<ClbitEstimate> estimate_assignment(
    const std::map<BitString, std::uint64_t>& zero_counts,
    const std::map<BitString, std::uint64_t>& one_counts,
    std::span<const ClbitId> clbits,
    std::uint64_t calibration_shots);

// The empirical distribution of counts over the measured clbits.
[[nodiscard]] std::vector<double> empirical_distribution(
    const std::map<BitString, std::uint64_t>& counts, std::span<const ClbitId> clbits);

// Applies factor to bit `bit` of every index of vector, in place.
void apply_factor(const Matrix2& factor, std::size_t bit, std::vector<double>& vector);

// Applies factors[0] (x) ... (x) factors[m-1] to vector in place, one factor at
// a time, in a fixed order: factor k acts on bit k of the index.
void apply_tensored(std::span<const Matrix2> factors, std::vector<double>& vector);

// The result of inverting the readout channel on an empirical distribution.
struct Mitigation {
    // q = M p, M = A_1^-1 (x) ... (x) A_m^-1. May have negative entries.
    std::vector<double> quasi_probabilities;
    // Var(q_i) from the target's shot noise, [((M o M) p)_i - q_i^2] / N: the
    // exact multinomial variance with the empirical p substituted for p.
    std::vector<double> target_variance;
    // Var(q_i) from calibration noise, by the first-order delta method.
    std::vector<double> calibration_variance;
    // prod_c ||A_c^-1||_1 (maximum absolute column sum).
    double gamma;
};

[[nodiscard]] Mitigation mitigate(std::span<const double> distribution,
                                  std::uint64_t shots,
                                  std::span<const ClbitEstimate> estimates,
                                  std::uint64_t calibration_shots);

// The bytes held at peak while mitigating m measured clbits: five dense double
// vectors of 2^m entries (the distribution, the quasi-probabilities, the two
// variances and one work vector). Throws ResourceExceeded when that size cannot
// be represented.
[[nodiscard]] std::uint64_t peak_bytes(std::size_t measured_clbits);

}  // namespace menos::mitigation
