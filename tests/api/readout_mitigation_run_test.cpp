#include "menos/mitigation.h"

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <complex>
#include <cstdio>
#include <numbers>
#include <optional>
#include <string>
#include <vector>

#include "common/error.h"
#include "test_statistics.h"

// Phase 14 gate criteria 2-5 (docs/BUILD_PLAN.md, docs/adr/0018 section 6).

namespace menos {
namespace {

using test_statistics::binomial_two_sided_p;
using test_statistics::chi_square_quantile;
using test_statistics::normal_upper_quantile;

using Complex = std::complex<double>;
using Matrix = std::vector<Complex>;
using Kraus = std::vector<Matrix>;

constexpr std::uint64_t kShots = 20000;  // N = K = 20 000 (section 6)
constexpr double kDepolarizing = 0.01;

// rho -> (1 - p) rho + p I/2.
Kraus depolarizing_1q(double p) {
    const double a = std::sqrt(1.0 - 3.0 * p / 4.0);
    const double b = std::sqrt(p / 4.0);
    return {{a, 0, 0, a}, {0, b, b, 0}, {0, Complex(0, -b), Complex(0, b), 0}, {b, 0, 0, -b}};
}

// Builds a fixture circuit: with noise, every gate is followed by a one-qubit
// depolarizing channel on each of its operands.
class Builder {
public:
    Builder(std::uint32_t n, bool gate_noise) : circuit_(n, n), gate_noise_(gate_noise) {}

    Builder& gate(Opcode opcode,
                  const std::vector<QubitId>& qubits,
                  std::vector<double> params = {}) {
        circuit_.append(opcode, qubits, std::move(params));
        if (gate_noise_) {
            for (const QubitId q : qubits) {
                circuit_.append_channel({q}, depolarizing_1q(kDepolarizing));
            }
        }
        return *this;
    }

    // CRY(theta) by its standard decomposition.
    Builder& cry(QubitId control, QubitId target, double theta) {
        gate(Opcode::RY, {target}, {theta / 2});
        gate(Opcode::CX, {control, target});
        gate(Opcode::RY, {target}, {-theta / 2});
        return gate(Opcode::CX, {control, target});
    }

    // Measures every qubit q into clbit q with readout[q] (or none).
    Circuit measured(const std::vector<ReadoutError>& readout) const {
        Circuit out = circuit_;
        for (QubitId q = 0; q < out.num_qubits(); ++q) {
            out.append_measure(q, q, readout.empty() ? std::nullopt : std::optional(readout[q]));
        }
        return out;
    }

    const Circuit& unmeasured() const {
        return circuit_;
    }

private:
    Circuit circuit_;
    bool gate_noise_;
};

using Program = void (*)(Builder&);

struct FixtureSpec {
    std::string name;
    std::uint32_t qubits;
    Program program;
    std::vector<ReadoutError> readout;
};

ReadoutError readout(double e0, double e1) {
    return ReadoutError{{1.0 - e0, e0, e1, 1.0 - e1}};
}

std::vector<ReadoutError> uniform_readout(std::uint32_t n) {
    return std::vector<ReadoutError>(n, readout(0.04, 0.08));
}

// The six circuits of docs/adr/0018 section 6.
std::vector<FixtureSpec> fixture_specs() {
    return {
        {"bell",
         2,
         [](Builder& b) { b.gate(Opcode::H, {0}).gate(Opcode::CX, {0, 1}); },
         uniform_readout(2)},
        {"ghz3",
         3,
         [](Builder& b) {
             b.gate(Opcode::H, {0}).gate(Opcode::CX, {0, 1}).gate(Opcode::CX, {1, 2});
         },
         uniform_readout(3)},
        {"ghz5",
         5,
         [](Builder& b) {
             b.gate(Opcode::H, {0});
             for (QubitId q = 0; q + 1 < 5; ++q) {
                 b.gate(Opcode::CX, {q, static_cast<QubitId>(q + 1)});
             }
         },
         uniform_readout(5)},
        {"clifford_t4",
         4,
         [](Builder& b) {
             b.gate(Opcode::H, {0}).gate(Opcode::H, {1}).gate(Opcode::CX, {0, 1});
             b.gate(Opcode::T, {1}).gate(Opcode::S, {2}).gate(Opcode::H, {3});
             b.gate(Opcode::CX, {2, 3}).gate(Opcode::CX, {1, 2}).gate(Opcode::Tdg, {0});
             b.gate(Opcode::H, {2}).gate(Opcode::CZ, {0, 3}).gate(Opcode::SX, {1});
             b.gate(Opcode::T, {3}).gate(Opcode::CX, {3, 0}).gate(Opcode::H, {1});
         },
         uniform_readout(4)},
        {"w3",
         3,
         [](Builder& b) {
             // (|100> + |010> + |001>) / sqrt 3.
             b.gate(Opcode::RY, {0}, {2.0 * std::asin(std::sqrt(1.0 / 3.0))});
             b.gate(Opcode::X, {0});
             b.cry(0, 1, std::numbers::pi / 2);
             b.gate(Opcode::X, {1});  // q0 stays flipped: CCX fires on q0 = q1 = 0
             b.gate(Opcode::CCX, {0, 1, 2});
             b.gate(Opcode::X, {0}).gate(Opcode::X, {1});
         },
         uniform_readout(3)},
        {"uneven5",
         5,
         [](Builder& b) {
             for (QubitId q = 0; q < 5; ++q) {
                 b.gate(Opcode::RY, {q}, {0.3 + 0.4 * q});
             }
             b.gate(Opcode::CX, {0, 2}).gate(Opcode::CX, {4, 1}).gate(Opcode::CZ, {2, 3});
         },
         {readout(0.01, 0.02),
          readout(0.05, 0.10),
          readout(0.10, 0.05),
          readout(0.02, 0.15),
          readout(0.08, 0.08)}},
    };
}

struct Fixture {
    std::string name;
    Circuit target;
    std::array<Circuit, 2> calibration;
    std::vector<double> ideal;  // noiseless distribution, index bit q = qubit q
};

// The noiseless reference: |amplitude|^2 of the noiseless, unmeasured circuit.
std::vector<double> ideal_distribution(const FixtureSpec& spec) {
    Builder b(spec.qubits, false);
    spec.program(b);
    const Result result =
        run(b.unmeasured(), RunOptions{.shots = 0, .want_statevector = true, .seed = 0});
    std::vector<double> out;
    // NOLINTNEXTLINE(bugprone-unchecked-optional-access): requested above.
    for (const Complex& a : *result.statevector()) {
        out.push_back(std::norm(a));
    }
    return out;
}

// The calibration pair under the fixture's own noise: X preparations get gate
// noise too (docs/adr/0018 section 9, Q1).
std::array<Circuit, 2> calibration_pair(const FixtureSpec& spec, bool gate_noise) {
    Builder zero(spec.qubits, gate_noise);
    Builder one(spec.qubits, gate_noise);
    for (QubitId q = 0; q < spec.qubits; ++q) {
        one.gate(Opcode::X, {q});
    }
    return {zero.measured(spec.readout), one.measured(spec.readout)};
}

Fixture make_fixture(const FixtureSpec& spec, bool gate_noise) {
    Builder b(spec.qubits, gate_noise);
    spec.program(b);
    return {spec.name + (gate_noise ? "+depolarizing" : "+readout"),
            b.measured(spec.readout),
            calibration_pair(spec, gate_noise),
            ideal_distribution(spec)};
}

std::vector<double> raw_distribution(const MitigatedResult& m) {
    std::vector<double> p(m.quasi_probabilities().size(), 0.0);
    for (const auto& [outcome, count] : m.raw().counts()) {
        std::size_t index = 0;
        for (std::size_t k = 0; k < m.measured_clbits().size(); ++k) {
            index |= outcome.test(m.measured_clbits()[k]) ? (std::size_t{1} << k) : 0;
        }
        p[index] += static_cast<double>(count) / static_cast<double>(kShots);
    }
    return p;
}

double tvd(const std::vector<double>& a, const std::vector<double>& b) {
    double sum = 0.0;
    for (std::size_t i = 0; i < a.size(); ++i) {
        sum += std::abs(a[i] - b[i]);
    }
    return 0.5 * sum;
}

MitigatedResult mitigate(const Fixture& f, std::uint64_t seed) {
    return run_readout_mitigated(f.target,
                                 f.calibration,
                                 RunOptions{.shots = kShots, .seed = seed},
                                 ReadoutMitigationOptions{.calibration_shots = kShots});
}

// Gate criterion 2.
TEST(ReadoutMitigationRunTest, MitigationMovesEveryFixtureTowardTheNoiselessReference) {
    for (const bool gate_noise : {false, true}) {
        for (const FixtureSpec& spec : fixture_specs()) {
            const Fixture f = make_fixture(spec, gate_noise);
            const MitigatedResult m = mitigate(f, 2026);
            const double raw = tvd(raw_distribution(m), f.ideal);
            const double mitigated = tvd(m.quasi_probabilities(), f.ideal);
            std::printf("[readout-mitigation-tvd] %s raw=%.5f mitigated=%.5f gamma=%.4f\n",
                        f.name.c_str(),
                        raw,
                        mitigated,
                        m.gamma());
            EXPECT_LT(mitigated, raw) << f.name;
        }
    }
}

// Gate criterion 3: Bonferroni over every outcome of the six readout-only
// fixtures, family-wise rate 0.001.
TEST(ReadoutMitigationRunTest, ReadoutOnlyMitigationAgreesWithTheReferenceWithinItsErrors) {
    std::vector<Fixture> fixtures;
    std::size_t outcomes = 0;
    for (const FixtureSpec& spec : fixture_specs()) {
        fixtures.push_back(make_fixture(spec, false));
        outcomes += fixtures.back().ideal.size();
    }
    const double z_star = normal_upper_quantile(0.001 / (2.0 * static_cast<double>(outcomes)));
    double worst = 0.0;
    for (const Fixture& f : fixtures) {
        const MitigatedResult m = mitigate(f, 77);
        for (std::size_t i = 0; i < f.ideal.size(); ++i) {
            const double se = m.standard_errors()[i];
            ASSERT_GT(se, 0.0) << f.name << " outcome " << i;
            const double z = (m.quasi_probabilities()[i] - f.ideal[i]) / se;
            worst = std::max(worst, std::abs(z));
            EXPECT_LE(std::abs(z), z_star) << f.name << " outcome " << i;
        }
    }
    std::printf(
        "[readout-mitigation-z] outcomes=%zu max|z|=%.3f z*=%.3f\n", outcomes, worst, z_star);
}

// Gate criterion 4: on 400 seeds, (a) the fraction with |z| <= 1.96 is
// consistent with 0.95 (two-sided binomial at 0.001) and (b) the spread of the
// estimate across seeds matches the mean reported error (chi-square interval at
// 0.001, 399 degrees of freedom) -- the docs/adr/0017 section 6 method.
TEST(ReadoutMitigationRunTest, ReportedStandardErrorsAreGenuineAcrossFourHundredSeeds) {
    constexpr std::uint64_t kSeeds = 400;
    const Fixture f = make_fixture(fixture_specs()[0], false);
    const double z_975 = normal_upper_quantile(0.025);
    const double dof = static_cast<double>(kSeeds - 1);
    // One pass over the seeds; both outcomes are read from each run.
    constexpr std::array<std::size_t, 2> kOutcomes{0, 1};
    std::array<std::uint64_t, 2> covered_by{};
    std::array<double, 2> sum_by{};
    std::array<double, 2> sum_squares_by{};
    std::array<double, 2> se_sum_by{};
    for (std::uint64_t seed = 0; seed < kSeeds; ++seed) {
        const MitigatedResult m = mitigate(f, 50000 + seed);
        for (std::size_t j = 0; j < kOutcomes.size(); ++j) {
            const double q = m.quasi_probabilities()[kOutcomes[j]];
            const double se = m.standard_errors()[kOutcomes[j]];
            covered_by[j] += std::abs(q - f.ideal[kOutcomes[j]]) <= z_975 * se ? 1U : 0U;
            sum_by[j] += q;
            sum_squares_by[j] += q * q;
            se_sum_by[j] += se;
        }
    }
    for (std::size_t j = 0; j < kOutcomes.size(); ++j) {
        const std::size_t outcome = kOutcomes[j];
        const std::uint64_t covered = covered_by[j];
        const double sum = sum_by[j];
        const double sum_squares = sum_squares_by[j];
        const double se_sum = se_sum_by[j];
        const double p_value = binomial_two_sided_p(covered, kSeeds, 0.95);
        const double mean = sum / static_cast<double>(kSeeds);
        const double sample_variance =
            (sum_squares - static_cast<double>(kSeeds) * mean * mean) / dof;
        const double mean_se = se_sum / static_cast<double>(kSeeds);
        const double statistic = dof * sample_variance / (mean_se * mean_se);
        const double chi_lo = chi_square_quantile(dof, 0.0005);
        const double chi_hi = chi_square_quantile(dof, 0.9995);
        std::printf(
            "[readout-mitigation-coverage] bell outcome=%zu covered=%llu/%llu p=%.4g chi2=%.2f in "
            "[%.2f, %.2f]\n",
            outcome,
            static_cast<unsigned long long>(covered),
            static_cast<unsigned long long>(kSeeds),
            p_value,
            statistic,
            chi_lo,
            chi_hi);
        EXPECT_GE(p_value, 0.001) << "outcome " << outcome;
        EXPECT_GE(statistic, chi_lo) << "outcome " << outcome << ": errors overstated";
        EXPECT_LE(statistic, chi_hi) << "outcome " << outcome << ": errors understated";
    }
}

// Gate criterion 5.
TEST(ReadoutMitigationRunTest, AMitigatedResultIsNeverExactAndExplainsItself) {
    const Fixture f = make_fixture(fixture_specs()[0], false);
    const MitigatedResult m = mitigate(f, 1);
    EXPECT_FALSE(m.exact());
    ASSERT_EQ(m.calibration().size(), 2U);
    ASSERT_EQ(m.calibration_results().size(), 2U);
    EXPECT_EQ(m.calibration_shots(), kShots);
    const std::string report = m.explain();
    EXPECT_NE(report.find("mitigation readout (tensored inversion) exact=false outcomes=4"),
              std::string::npos)
        << report;
    EXPECT_NE(report.find("clbit 0 P(1|0)="), std::string::npos) << report;
    EXPECT_NE(report.find("calibration first-order delta method"), std::string::npos) << report;
    EXPECT_EQ(report.rfind(m.raw().explain(), 0), 0U) << "starts with the raw run's report";
    double total = 0.0;
    for (const double q : m.quasi_probabilities()) {
        total += q;
    }
    EXPECT_NEAR(total, 1.0, 1e-12);
}

TEST(ReadoutMitigationRunTest, TheSameSeedGivesTheSameMitigatedResult) {
    const Fixture f = make_fixture(fixture_specs()[1], true);
    EXPECT_EQ(mitigate(f, 9).quasi_probabilities(), mitigate(f, 9).quasi_probabilities());
    EXPECT_EQ(mitigate(f, 9).standard_errors(), mitigate(f, 9).standard_errors());
}

TEST(ReadoutMitigationRunTest, TheDefaultPairIsTheNoiselessPreparationPair) {
    const Fixture f = make_fixture(fixture_specs()[1], false);
    const RunOptions options{.shots = 1000, .seed = 4};
    const ReadoutMitigationOptions mitigation{.calibration_shots = 1000};
    const MitigatedResult supplied =
        run_readout_mitigated(f.target, f.calibration, options, mitigation);
    const MitigatedResult built =
        run_readout_mitigated(f.target, std::nullopt, options, mitigation);
    EXPECT_EQ(supplied.quasi_probabilities(), built.quasi_probabilities());
}

TEST(ReadoutMitigationRunTest, NoReadoutErrorGivesTheRawDistributionWithGammaOne) {
    FixtureSpec spec = fixture_specs()[0];
    spec.readout.clear();
    const Fixture f = make_fixture(spec, false);
    const MitigatedResult m = mitigate(f, 5);
    EXPECT_EQ(m.gamma(), 1.0);
    EXPECT_EQ(m.quasi_probabilities(), raw_distribution(m));
}

TEST(ReadoutMitigationRunTest, InvalidRequestsAreRefused) {
    const Fixture f = make_fixture(fixture_specs()[0], false);
    const ReadoutMitigationOptions mitigation{.calibration_shots = 100};
    const RunOptions options{.shots = 100, .seed = 1};

    Circuit unmeasured(2, 2);
    unmeasured.append(Opcode::H, {0});
    EXPECT_THROW((void)run_readout_mitigated(unmeasured, std::nullopt, options, mitigation),
                 common::InvalidCircuit);
    EXPECT_THROW(
        (void)run_readout_mitigated(f.target,
                                    std::nullopt,
                                    RunOptions{.shots = 0, .want_statevector = true, .seed = 1},
                                    mitigation),
        common::InvalidCircuit);
    EXPECT_THROW(
        (void)run_readout_mitigated(
            f.target, std::nullopt, options, ReadoutMitigationOptions{.calibration_shots = 0}),
        common::InvalidCircuit);

    // A pair that measures only one of the target's two clbits.
    std::array<Circuit, 2> partial{Circuit(2, 2), Circuit(2, 2)};
    partial[0].append_measure(0, 0);
    partial[1].append(Opcode::X, {0});
    partial[1].append_measure(0, 0);
    EXPECT_THROW((void)run_readout_mitigated(f.target, partial, options, mitigation),
                 common::InvalidCircuit);

    // Two measured clbits need 5 * 4 * 8 = 160 bytes of dense vectors.
    RunOptions tight = options;
    tight.memory_budget_bytes = 159;
    EXPECT_THROW((void)run_readout_mitigated(f.target, std::nullopt, tight, mitigation),
                 common::ResourceExceeded);
}

TEST(ReadoutMitigationRunTest, ACoinFlipReadoutIsRefused) {
    Circuit target(1, 1);
    target.append(Opcode::H, {0});
    // P(1|0) = P(0|1) = 0.7: worse than a coin flip, so the estimated sum
    // exceeds 1 by many standard errors.
    target.append_measure(0, 0, ReadoutError{{0.3, 0.7, 0.7, 0.3}});
    EXPECT_THROW((void)run_readout_mitigated(target,
                                             std::nullopt,
                                             RunOptions{.shots = 100, .seed = 3},
                                             ReadoutMitigationOptions{.calibration_shots = 1000}),
                 common::UnsupportedFeature);
}

}  // namespace
}  // namespace menos
