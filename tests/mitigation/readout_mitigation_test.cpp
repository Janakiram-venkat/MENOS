#include "mitigation/readout_mitigation.h"

#include <gtest/gtest.h>

#include <cmath>
#include <vector>

#include "common/error.h"

namespace menos::mitigation {
namespace {

using Dense = std::vector<std::vector<double>>;

// The full 2^m x 2^m matrix of factors[0] (x) ... (x) factors[m-1], with factor
// k acting on bit k: M[i][j] = prod_k F_k[bit_k(i)][bit_k(j)].
Dense dense_tensor(const std::vector<Matrix2>& factors) {
    const std::size_t dim = std::size_t{1} << factors.size();
    Dense out(dim, std::vector<double>(dim, 1.0));
    for (std::size_t i = 0; i < dim; ++i) {
        for (std::size_t j = 0; j < dim; ++j) {
            for (std::size_t k = 0; k < factors.size(); ++k) {
                const std::size_t r = (i >> k) & 1U;
                const std::size_t c = (j >> k) & 1U;
                out[i][j] *= factors[k][2 * r + c];
            }
        }
    }
    return out;
}

std::vector<double> times(const Dense& m, const std::vector<double>& v) {
    std::vector<double> out(m.size(), 0.0);
    for (std::size_t i = 0; i < m.size(); ++i) {
        for (std::size_t j = 0; j < v.size(); ++j) {
            out[i] += m[i][j] * v[j];
        }
    }
    return out;
}

Matrix2 assignment(const ClbitEstimate& e) {
    return {1.0 - e.e0, e.e1, e.e0, 1.0 - e.e1};
}

Matrix2 inverse_of(const ClbitEstimate& e) {
    const double det = 1.0 - e.e0 - e.e1;
    return {(1.0 - e.e1) / det, -e.e1 / det, -e.e0 / det, (1.0 - e.e0) / det};
}

const std::vector<ClbitEstimate> kEstimates{{0.05, 0.12}, {0.2, 0.03}, {0.1, 0.1}};
const std::vector<double> kTrue{0.30, 0.05, 0.10, 0.15, 0.02, 0.08, 0.20, 0.10};

std::vector<double> noisy_distribution() {
    std::vector<Matrix2> a;
    a.reserve(kEstimates.size());
    for (const ClbitEstimate& e : kEstimates) {
        a.push_back(assignment(e));
    }
    return times(dense_tensor(a), kTrue);
}

TEST(ReadoutMitigationTest, ApplyTensoredMatchesTheDenseKroneckerProduct) {
    const std::vector<Matrix2> factors{
        {1.0, 2.0, 3.0, 4.0}, {0.5, -1.0, 0.25, 2.0}, {2.0, 0.0, 1.0, -3.0}};
    std::vector<double> v{1, -2, 3, 0.5, -1, 4, 2, -0.25};
    const std::vector<double> expected = times(dense_tensor(factors), v);
    apply_tensored(factors, v);
    for (std::size_t i = 0; i < v.size(); ++i) {
        EXPECT_NEAR(v[i], expected[i], 1e-12) << i;
    }
}

TEST(ReadoutMitigationTest, InvertingTheExactNoisyDistributionRecoversTheTrueOne) {
    const Mitigation m = mitigate(noisy_distribution(), 1000, kEstimates, 1000);
    double total = 0.0;
    for (std::size_t i = 0; i < kTrue.size(); ++i) {
        EXPECT_NEAR(m.quasi_probabilities[i], kTrue[i], 1e-12) << i;
        total += m.quasi_probabilities[i];
    }
    EXPECT_NEAR(total, 1.0, 1e-12);
}

TEST(ReadoutMitigationTest, TargetVarianceEqualsTheDenseMultinomialPropagation) {
    const std::vector<double> p = noisy_distribution();
    constexpr std::uint64_t kShots = 5000;
    const Mitigation m = mitigate(p, kShots, kEstimates, 1000);

    std::vector<Matrix2> inverses;
    inverses.reserve(kEstimates.size());
    for (const ClbitEstimate& e : kEstimates) {
        inverses.push_back(inverse_of(e));
    }
    const Dense big_m = dense_tensor(inverses);
    const std::size_t dim = p.size();
    for (std::size_t i = 0; i < dim; ++i) {
        // diag(M Cov M^T), Cov = (diag p - p p^T) / N.
        double var = 0.0;
        for (std::size_t j = 0; j < dim; ++j) {
            for (std::size_t k = 0; k < dim; ++k) {
                const double cov = ((j == k ? p[j] : 0.0) - p[j] * p[k]) / kShots;
                var += big_m[i][j] * cov * big_m[i][k];
            }
        }
        EXPECT_NEAR(m.target_variance[i], var, 1e-15) << i;
    }
}

TEST(ReadoutMitigationTest, CalibrationVarianceMatchesFiniteDifferenceDerivatives) {
    const std::vector<double> p = noisy_distribution();
    constexpr std::uint64_t kCalibrationShots = 4000;
    const Mitigation m = mitigate(p, 1000, kEstimates, kCalibrationShots);

    const auto q_of = [&](const std::vector<ClbitEstimate>& estimates) {
        return mitigate(p, 1000, estimates, kCalibrationShots).quasi_probabilities;
    };
    constexpr double kH = 1e-6;
    std::vector<double> expected(p.size(), 0.0);
    for (std::size_t c = 0; c < kEstimates.size(); ++c) {
        for (int which = 0; which < 2; ++which) {
            std::vector<ClbitEstimate> up = kEstimates;
            std::vector<ClbitEstimate> down = kEstimates;
            double& u = which == 0 ? up[c].e0 : up[c].e1;
            double& d = which == 0 ? down[c].e0 : down[c].e1;
            u += kH;
            d -= kH;
            const double e = which == 0 ? kEstimates[c].e0 : kEstimates[c].e1;
            const double var_e = e * (1.0 - e) / kCalibrationShots;
            const std::vector<double> qu = q_of(up);
            const std::vector<double> qd = q_of(down);
            for (std::size_t i = 0; i < p.size(); ++i) {
                const double derivative = (qu[i] - qd[i]) / (2.0 * kH);
                expected[i] += derivative * derivative * var_e;
            }
        }
    }
    for (std::size_t i = 0; i < p.size(); ++i) {
        EXPECT_NEAR(m.calibration_variance[i], expected[i], 1e-9 * expected[i] + 1e-14) << i;
    }
}

TEST(ReadoutMitigationTest, GammaIsTheProductOfColumnNorms) {
    // e0 = 0.1, e1 = 0.2: det 0.7, column sums 0.9 / 0.7 and 1.1 / 0.7.
    const std::vector<ClbitEstimate> one{{0.1, 0.2}};
    EXPECT_NEAR(mitigate(std::vector<double>{0.5, 0.5}, 10, one, 10).gamma, 1.1 / 0.7, 1e-12);
    const std::vector<ClbitEstimate> two{{0.1, 0.2}, {0.1, 0.2}};
    EXPECT_NEAR(mitigate(std::vector<double>(4, 0.25), 10, two, 10).gamma,
                (1.1 / 0.7) * (1.1 / 0.7),
                1e-12);
}

TEST(ReadoutMitigationTest, NoReadoutErrorLeavesTheDistributionAndOnlyShotNoise) {
    const std::vector<double> p{0.5, 0.25, 0.125, 0.125};
    const std::vector<ClbitEstimate> none{{0.0, 0.0}, {0.0, 0.0}};
    const Mitigation m = mitigate(p, 100, none, 100);
    EXPECT_EQ(m.gamma, 1.0);
    for (std::size_t i = 0; i < p.size(); ++i) {
        EXPECT_EQ(m.quasi_probabilities[i], p[i]);
        EXPECT_NEAR(m.target_variance[i], p[i] * (1.0 - p[i]) / 100.0, 1e-15);
        EXPECT_EQ(m.calibration_variance[i], 0.0);
    }
}

std::map<BitString, std::uint64_t> counts_of(
    std::uint32_t width, const std::vector<std::pair<std::uint64_t, std::uint64_t>>& entries) {
    std::map<BitString, std::uint64_t> counts;
    for (const auto& [value, count] : entries) {
        BitString bits(width);
        for (std::uint32_t b = 0; b < width; ++b) {
            bits.set(b, ((value >> b) & 1U) != 0);
        }
        counts[bits] = count;
    }
    return counts;
}

TEST(ReadoutMitigationTest, EstimatesAreMarginalFrequenciesOfTheCalibrationCounts) {
    // Clbits 0 and 2 of 3 are measured. Zero circuit: 100 shots. One circuit: 100.
    const auto zero = counts_of(3, {{0b000, 85}, {0b001, 10}, {0b100, 5}});
    const auto one = counts_of(3, {{0b101, 70}, {0b100, 20}, {0b001, 10}});
    const std::vector<ClbitId> clbits{0, 2};
    const std::vector<ClbitEstimate> e = estimate_assignment(zero, one, clbits, 100);
    ASSERT_EQ(e.size(), 2U);
    EXPECT_DOUBLE_EQ(e[0].e0, 0.10);
    EXPECT_DOUBLE_EQ(e[0].e1, 0.20);
    EXPECT_DOUBLE_EQ(e[1].e0, 0.05);
    EXPECT_DOUBLE_EQ(e[1].e1, 0.10);
}

TEST(ReadoutMitigationTest, AReadoutNoBetterThanACoinFlipIsRefused) {
    const auto zero = counts_of(1, {{0, 50}, {1, 50}});
    const auto one = counts_of(1, {{0, 50}, {1, 50}});
    const std::vector<ClbitId> clbits{0};
    EXPECT_THROW((void)estimate_assignment(zero, one, clbits, 100), common::UnsupportedFeature);
}

TEST(ReadoutMitigationTest, EmpiricalDistributionIndexesByMeasuredClbitOrder) {
    const auto counts = counts_of(3, {{0b100, 3}, {0b001, 1}});
    const std::vector<ClbitId> clbits{0, 2};
    const std::vector<double> p = empirical_distribution(counts, clbits);
    ASSERT_EQ(p.size(), 4U);
    EXPECT_DOUBLE_EQ(p[0b01], 0.25);  // clbit 0 set
    EXPECT_DOUBLE_EQ(p[0b10], 0.75);  // clbit 2 set
}

TEST(ReadoutMitigationTest, MeasuredPairsFollowTheLastWriteToEachClbit) {
    Circuit circuit(3, 3);
    circuit.append_measure(0, 1);
    circuit.append_measure(2, 0);
    circuit.append_measure(1, 1);  // overwrites clbit 1
    const std::vector<std::pair<QubitId, ClbitId>> expected{{2, 0}, {1, 1}};
    EXPECT_EQ(measured_pairs(circuit), expected);
}

TEST(ReadoutMitigationTest, PeakBytesCountsFiveDenseVectors) {
    EXPECT_EQ(peak_bytes(3), 5U * 8U * 8U);
    EXPECT_THROW((void)peak_bytes(63), common::ResourceExceeded);
}

}  // namespace
}  // namespace menos::mitigation
