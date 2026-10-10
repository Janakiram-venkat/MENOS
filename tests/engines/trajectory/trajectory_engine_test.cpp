#include "engines/trajectory/trajectory_engine.h"

#include <gtest/gtest.h>

#include <cmath>
#include <complex>
#include <cstdint>
#include <map>
#include <utility>
#include <vector>

#include "common/error.h"
#include "common/splitmix64.h"

namespace menos::engines::trajectory {
namespace {

using Complex = std::complex<double>;
using Kraus = std::vector<std::vector<Complex>>;

constexpr std::uint64_t kBudget = std::uint64_t{1} << 30;

Kraus depolarizing_1q(double p) {
    const double a = std::sqrt(1.0 - 3.0 * p / 4.0);
    const double b = std::sqrt(p / 4.0);
    return {{a, 0.0, 0.0, a},
            {0.0, b, b, 0.0},
            {0.0, Complex{0.0, -b}, Complex{0.0, b}, 0.0},
            {b, 0.0, 0.0, -b}};
}

// Amplitude damping: |1> decays to |0> with probability gamma. Not a mixed
// unitary channel, so its branch probabilities depend on the state.
Kraus amplitude_damping(double gamma) {
    return {{1.0, 0.0, 0.0, std::sqrt(1.0 - gamma)}, {0.0, std::sqrt(gamma), 0.0, 0.0}};
}

Observable single_term(std::uint32_t n, std::uint64_t x, std::uint64_t z, Complex c = 1.0) {
    Observable observable;
    observable.terms.emplace_back(PauliString{n, {x}, {z}}, c);
    return observable;
}

// measure_targets is filled by the caller (the runtime resolves it in
// production); here every test measures qubit q into clbit q.
EngineExecutionRequest request_for(const Circuit& circuit,
                                   std::uint64_t trajectories,
                                   std::uint64_t shots,
                                   const Observable* observable,
                                   const std::vector<std::pair<QubitId, ClbitId>>& targets) {
    return EngineExecutionRequest{.num_qubits = circuit.num_qubits(),
                                  .operations = circuit.operations(),
                                  .want_statevector = false,
                                  .want_counts = shots > 0,
                                  .want_expectation = observable != nullptr,
                                  .shots = shots,
                                  .measure_targets = targets,
                                  .bitstring_width = circuit.num_clbits(),
                                  .seed = 7,
                                  .global_phase = 0.0,
                                  .budget_bytes = kBudget,
                                  .is_sole_region = true,
                                  .observable = observable,
                                  .trajectories = trajectories};
}

TEST(TrajectoryEngineTest, TrajectorySeedIsTheMatchingSplitMix64Output) {
    constexpr std::uint64_t kBase = 0xDEADBEEFULL;
    common::SplitMix64 mixer(kBase);
    for (std::uint64_t t = 0; t < 8; ++t) {
        EXPECT_EQ(trajectory_seed(kBase, t), mixer.next()) << "t = " << t;
    }
}

TEST(TrajectoryEngineTest, EstimateRefusesAStatevectorAndAMissingCount) {
    Circuit circuit(1, 0);
    circuit.append_channel({0}, depolarizing_1q(0.1));
    const TrajectoryEngine engine;
    EngineEstimateRequest request{.num_qubits = 1,
                                  .operations = &circuit.operations(),
                                  .want_statevector = true,
                                  .want_counts = false,
                                  .want_expectation = false,
                                  .observable = nullptr,
                                  .trajectories = 10};
    EXPECT_FALSE(engine.estimate(request).feasible);
    request.want_statevector = false;
    request.want_counts = true;
    request.trajectories = 0;
    const CostEstimate missing = engine.estimate(request);
    EXPECT_FALSE(missing.feasible);
    EXPECT_FALSE(missing.infeasible_reason.empty());
}

TEST(TrajectoryEngineTest, EstimateScalesWithTheTrajectoryCount) {
    Circuit circuit(3, 0);
    circuit.append(Opcode::H, {0});
    circuit.append_channel({0}, depolarizing_1q(0.1));
    const TrajectoryEngine engine;
    EngineEstimateRequest request{.num_qubits = 3,
                                  .operations = &circuit.operations(),
                                  .want_statevector = false,
                                  .want_counts = true,
                                  .want_expectation = false,
                                  .observable = nullptr,
                                  .trajectories = 100};
    const CostEstimate hundred = engine.estimate(request);
    request.trajectories = 200;
    const CostEstimate two_hundred = engine.estimate(request);
    ASSERT_TRUE(hundred.feasible);
    ASSERT_TRUE(two_hundred.feasible);
    EXPECT_EQ(two_hundred.estimated_work_units, 2 * hundred.estimated_work_units);
    EXPECT_EQ(two_hundred.estimated_peak_bytes, hundred.estimated_peak_bytes);
    // Per trajectory: 8 reset + 16 (H) + 16 + 16 + 8 (channel) + 8 (distribution).
    EXPECT_EQ(hundred.estimated_work_units, 100U * (8 + 16 + 40 + 8));
}

// Amplitude damping on |1> leaves |0> with probability gamma: a state-dependent
// branch choice, which a mixed-unitary shortcut would get wrong.
TEST(TrajectoryEngineTest, AmplitudeDampingDecaysWithTheRightFrequency) {
    constexpr double kGamma = 0.3;
    constexpr std::uint64_t kShots = 20000;
    Circuit circuit(1, 1);
    circuit.append(Opcode::X, {0});
    circuit.append_channel({0}, amplitude_damping(kGamma));
    circuit.append_measure(0, 0);
    const std::vector<std::pair<QubitId, ClbitId>> targets{{0, 0}};
    const TrajectoryEngine engine;
    const EngineExecutionResult result =
        engine.execute(request_for(circuit, kShots, kShots, nullptr, targets));
    ASSERT_TRUE(result.counts.has_value());
    // Guarded by the ASSERT_TRUE above; clang-tidy cannot see through it.
    const auto& counts = *result.counts;  // NOLINT(bugprone-unchecked-optional-access)
    EXPECT_TRUE(result.statistical);
    BitString zero(1);
    const double frequency = static_cast<double>(counts.contains(zero) ? counts.at(zero) : 0) /
                             static_cast<double>(kShots);
    // Binomial standard deviation at 20000 shots is 0.0032; 5 sigma.
    const double sigma = std::sqrt(kGamma * (1.0 - kGamma) / static_cast<double>(kShots));
    EXPECT_NEAR(frequency, kGamma, 5.0 * sigma);
}

TEST(TrajectoryEngineTest, AReadoutErrorIsAppliedToEachShot) {
    Circuit circuit(1, 1);
    circuit.append_channel({0}, depolarizing_1q(0.0));
    // P(record 1 | true 0) = 1: every |0> outcome is recorded as 1.
    circuit.append_measure(0, 0, ReadoutError{{0.0, 1.0, 0.0, 1.0}});
    const std::vector<std::pair<QubitId, ClbitId>> targets{{0, 0}};
    const TrajectoryEngine engine;
    const EngineExecutionResult result =
        engine.execute(request_for(circuit, 50, 50, nullptr, targets));
    ASSERT_TRUE(result.counts.has_value());
    // Guarded by the ASSERT_TRUE above; clang-tidy cannot see through it.
    const auto& counts = *result.counts;  // NOLINT(bugprone-unchecked-optional-access)
    BitString one(1);
    one.set(0, true);
    ASSERT_EQ(counts.size(), 1U);
    EXPECT_EQ(counts.at(one), 50U);
}

// Depolarizing H|0> with probability p shrinks <X> from 1 to 1 - p.
TEST(TrajectoryEngineTest, ExpectationMeanAndStandardErrorAgreeWithTheExactValue) {
    constexpr double kP = 0.4;
    Circuit circuit(1, 0);
    circuit.append(Opcode::H, {0});
    circuit.append_channel({0}, depolarizing_1q(kP));
    const Observable observable = single_term(1, 1, 0);
    const TrajectoryEngine engine;
    const EngineExecutionResult result =
        engine.execute(request_for(circuit, 4000, 0, &observable, {}));
    ASSERT_TRUE(result.expectation_terms.has_value());
    ASSERT_TRUE(result.expectation_standard_error.has_value());
    // Guarded by the ASSERT_TRUEs above; clang-tidy cannot see through them.
    // NOLINTBEGIN(bugprone-unchecked-optional-access)
    const double mean = (*result.expectation_terms)[0].real();
    const double se = *result.expectation_standard_error;
    // NOLINTEND(bugprone-unchecked-optional-access)
    EXPECT_GT(se, 0.0);
    // Each X_t is +1 (no error, or an X error) or -1 (a Y or Z error), so the
    // exact standard error is sqrt(1 - (1 - p)^2) / sqrt(N).
    EXPECT_NEAR(se, std::sqrt(1.0 - (1.0 - kP) * (1.0 - kP)) / std::sqrt(4000.0), 0.1 * se);
    EXPECT_NEAR(mean, 1.0 - kP, 5.0 * se);
}

TEST(TrajectoryEngineTest, TheSameSeedGivesTheSameResult) {
    Circuit circuit(2, 2);
    circuit.append(Opcode::H, {0});
    circuit.append(Opcode::CX, {0, 1});
    circuit.append_channel({1}, amplitude_damping(0.2));
    circuit.append_measure(0, 0);
    circuit.append_measure(1, 1);
    const Observable observable = single_term(2, 0, 3);
    const std::vector<std::pair<QubitId, ClbitId>> targets{{0, 0}, {1, 1}};
    const TrajectoryEngine engine;
    const EngineExecutionResult a =
        engine.execute(request_for(circuit, 300, 200, &observable, targets));
    const EngineExecutionResult b =
        engine.execute(request_for(circuit, 300, 200, &observable, targets));
    EXPECT_EQ(a.counts, b.counts);
    EXPECT_EQ(a.expectation_terms, b.expectation_terms);
    EXPECT_EQ(a.expectation_standard_error, b.expectation_standard_error);
}

class CountingMonitor : public ExecutionMonitor {
public:
    void at_safe_boundary(std::uint64_t boundaries_crossed) const override {
        last = boundaries_crossed;
    }
    mutable std::uint64_t last = 0;
};

// A boundary after every operation of every trajectory, and one between
// trajectories (docs/adr/0017 section 2).
TEST(TrajectoryEngineTest, TheMonitorSeesEveryOperationAndEveryTrajectory) {
    Circuit circuit(1, 0);
    circuit.append(Opcode::H, {0});
    circuit.append_channel({0}, depolarizing_1q(0.1));
    const Observable observable = single_term(1, 1, 0);
    const CountingMonitor monitor;
    EngineExecutionRequest request = request_for(circuit, 25, 0, &observable, {});
    request.monitor = &monitor;
    const TrajectoryEngine engine;
    (void)engine.execute(request);
    EXPECT_EQ(monitor.last, 25U * (2 + 1));
}

}  // namespace
}  // namespace menos::engines::trajectory
