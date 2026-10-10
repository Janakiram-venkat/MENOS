#include "menos/menos.h"

#include <cmath>
#include <numbers>

#include <gtest/gtest.h>

#include "common/error.h"
#include "common/tolerance.h"

namespace menos {
namespace {

using Complex = std::complex<double>;

const std::vector<Complex>& get_statevector(const Result& result) {
    EXPECT_TRUE(result.statevector().has_value());
    return *result.statevector();  // NOLINT(bugprone-unchecked-optional-access)
}

// The interleaved-components circuit named explicitly in
// docs/BUILD_PLAN.md Phase 3, 3.3: global qubits {0,2} form one Bell
// pair, {1,3} form another, non-contiguous and interleaved. Expected
// amplitudes are derived independently here, from scratch (the Kronecker
// structure of two known 2-qubit Bell states placed at their global bit
// positions), not by calling any of MENOS's own recombination code.
Circuit make_interleaved_bell_pairs_circuit() {
    Circuit circuit(4, 4);
    circuit.append(Opcode::H, {0});
    circuit.append(Opcode::CX, {0, 2});
    circuit.append(Opcode::H, {1});
    circuit.append(Opcode::CX, {1, 3});
    return circuit;
}

TEST(FactorizedDifferentialTest, InterleavedBellPairsMatchAnalyticStatevectorAndUsesFactorized) {
    Circuit circuit = make_interleaved_bell_pairs_circuit();
    const Result result = run(circuit, RunOptions{.shots = 0, .want_statevector = true, .seed = 0});

    // Confirm the interesting code path actually ran: the planner should
    // find factorizing this circuit strictly cheaper than direct (hand-
    // computed: direct=224 work units, factorized=96 -- see
    // tests/plan/planner_test.cpp's ManySmallComponentsMakeFactorizationCheaperThanDirect
    // for the same reasoning at a different scale).
    ASSERT_EQ(result.plan().strategy, Strategy::Factorized);
    ASSERT_EQ(result.plan().regions.size(), 2U);

    const auto& sv = get_statevector(result);
    ASSERT_EQ(sv.size(), 16U);

    // Nonzero only where q0==q2 and q1==q3 (global index bit layout:
    // q3 q2 q1 q0), each with amplitude 0.5 (1/sqrt2 * 1/sqrt2).
    for (std::size_t a = 0; a < 16; ++a) {
        const bool q0 = (a >> 0) & 1;
        const bool q1 = (a >> 1) & 1;
        const bool q2 = (a >> 2) & 1;
        const bool q3 = (a >> 3) & 1;
        const Complex expected = (q0 == q2 && q1 == q3) ? Complex{0.5, 0.0} : Complex{0.0, 0.0};
        EXPECT_NEAR(sv[a].real(), expected.real(), common::kDefaultAbsTol) << "index " << a;
        EXPECT_NEAR(sv[a].imag(), expected.imag(), common::kDefaultAbsTol) << "index " << a;
    }
}

TEST(FactorizedDifferentialTest, GlobalPhasePreservedExactlyOnceNotPerComponent) {
    // If global_phase were (incorrectly) applied to every region's own
    // initialize() call instead of exactly one, the result would carry
    // exp(i*phase)^2 instead of exp(i*phase) -- distinguishable for any
    // phase where exp(i*phase) != exp(i*phase)^2, e.g. phase = pi/2
    // (i vs. -1).
    Circuit circuit = make_interleaved_bell_pairs_circuit();
    circuit.global_phase = std::numbers::pi / 2.0;
    const Result result = run(circuit, RunOptions{.shots = 0, .want_statevector = true, .seed = 0});
    ASSERT_EQ(result.plan().strategy, Strategy::Factorized);

    const auto& sv = get_statevector(result);
    const Complex phase = std::exp(Complex{0.0, std::numbers::pi / 2.0});  // i
    for (std::size_t a = 0; a < 16; ++a) {
        const bool q0 = (a >> 0) & 1;
        const bool q1 = (a >> 1) & 1;
        const bool q2 = (a >> 2) & 1;
        const bool q3 = (a >> 3) & 1;
        const Complex expected =
            (q0 == q2 && q1 == q3) ? phase * Complex{0.5, 0.0} : Complex{0.0, 0.0};
        EXPECT_NEAR(sv[a].real(), expected.real(), common::kDefaultAbsTol) << "index " << a;
        EXPECT_NEAR(sv[a].imag(), expected.imag(), common::kDefaultAbsTol) << "index " << a;
    }
}

// Regression test: an earlier implementation folded global_phase into
// "region 0"'s own initialize() call specifically. That silently dropped
// the phase whenever region 0 (the region containing global qubit 0)
// happened to be idle, since the idle branch never touches
// global_phase at all. Caught by python/tests/test_differential_statevector.py's
// randomized 8-qubit case (docs/adr/0005); this is the minimal C++
// reproduction: qubit 0 is idle, qubits {1,2} carry the only real gate.
TEST(FactorizedDifferentialTest, GlobalPhaseAppliedEvenWhenRegionZeroIsIdle) {
    Circuit circuit(3, 0);  // qubit 0 idle; qubits {1,2} form the only real component
    circuit.global_phase = std::numbers::pi / 2.0;
    circuit.append(Opcode::H, {1});
    circuit.append(Opcode::CX, {1, 2});
    const Result result = run(circuit, RunOptions{.shots = 0, .want_statevector = true, .seed = 0});
    ASSERT_EQ(result.plan().strategy, Strategy::Factorized);
    ASSERT_EQ(result.plan().regions.size(), 2U);
    ASSERT_EQ(result.plan().regions[0].local_to_global, (std::vector<QubitId>{0}));
    ASSERT_EQ(result.plan().regions[0].estimated_state_bytes, 0U);  // region 0 is idle

    const auto& sv = get_statevector(result);
    const Complex phase = std::exp(Complex{0.0, std::numbers::pi / 2.0});  // i
    const double s = 1.0 / std::numbers::sqrt2;
    // Bell pair on {1,2}, qubit 0 fixed at |0>: nonzero only at global
    // indices 0 (q0=q1=q2=0) and 0b110 (q0=0,q1=1,q2=1), each s*phase.
    for (std::size_t a = 0; a < 8; ++a) {
        const Complex expected =
            (a == 0 || a == 0b110) ? phase * Complex{s, 0.0} : Complex{0.0, 0.0};
        EXPECT_NEAR(sv[a].real(), expected.real(), common::kDefaultAbsTol) << "index " << a;
        EXPECT_NEAR(sv[a].imag(), expected.imag(), common::kDefaultAbsTol) << "index " << a;
    }
}

// Exact counts statistical test (docs/BUILD_PLAN.md Phase 3, 3.11 item
// 6): the interleaved Bell pairs' combined outcome distribution is
// uniform over {0000, 0101, 1010, 1111} (q0=q2, q1=q3, each pair
// independently 50/50) -- each of the four combined outcomes should
// appear with probability 0.25. Chi-square critical value for df=3,
// p=0.001 (16.2662) independently re-verified this session via
// scipy.stats.chi2.ppf(0.999, 3), matching the constant already used in
// tests/engines/dense/sample_test.cpp.
TEST(FactorizedDifferentialTest, CombinedCountsAreStatisticallyUniformOverFourOutcomes) {
    Circuit circuit = make_interleaved_bell_pairs_circuit();
    circuit.append_measure(0, 0);
    circuit.append_measure(1, 1);
    circuit.append_measure(2, 2);
    circuit.append_measure(3, 3);

    constexpr std::uint64_t kShots = 100'000;
    const Result result = run(circuit, RunOptions{.shots = kShots, .seed = 31415});
    ASSERT_EQ(result.plan().strategy, Strategy::Factorized);

    // Expected combined outcomes (clbit c = qubit c, little-endian):
    // 0000 (q0=q2=0,q1=q3=0), 0101 (q0=q2=1,q1=q3=0), 1010 (q0=q2=0,q1=q3=1),
    // 1111 (q0=q2=1,q1=q3=1) -- each probability 0.25.
    const double expected_each = 0.25 * static_cast<double>(kShots);
    double chi_square = 0.0;
    std::uint64_t total = 0;
    for (const auto& [key, count] : result.counts()) {
        const bool b0 = key.test(0), b1 = key.test(1), b2 = key.test(2), b3 = key.test(3);
        EXPECT_EQ(b0, b2) << "q0/q2 must be correlated (same Bell pair)";
        EXPECT_EQ(b1, b3) << "q1/q3 must be correlated (same Bell pair)";
        const double diff = static_cast<double>(count) - expected_each;
        chi_square += diff * diff / expected_each;
        total += count;
    }
    // Missing outcomes (count 0 in the map) still contribute their
    // squared-deviation term.
    const std::size_t observed_outcomes = result.counts().size();
    for (std::size_t missing = observed_outcomes; missing < 4; ++missing) {
        chi_square += expected_each;  // (0 - expected)^2 / expected == expected
    }
    EXPECT_EQ(total, kShots);

    constexpr double kChiSquareCriticalDf3P0001 = 16.2662;
    EXPECT_LT(chi_square, kChiSquareCriticalDf3P0001)
        << "chi_square=" << chi_square << " observed_outcomes=" << observed_outcomes;
}

// 10 independent 10-qubit components (100 qubits total, each component
// well within the spec's named "at most 12 qubits" bound). Connected
// internally by a CX ladder so each block is genuinely one component.
Circuit make_one_hundred_qubit_circuit() {
    constexpr std::uint32_t kNumComponents = 10;
    constexpr std::uint32_t kComponentSize = 10;
    Circuit circuit(kNumComponents * kComponentSize, kNumComponents * kComponentSize);
    for (std::uint32_t block = 0; block < kNumComponents; ++block) {
        const QubitId base = block * kComponentSize;
        for (QubitId i = 0; i + 1 < kComponentSize; ++i) {
            circuit.append(Opcode::CX,
                           {static_cast<QubitId>(base + i), static_cast<QubitId>(base + i + 1)});
        }
        for (QubitId i = 0; i < kComponentSize; ++i) {
            circuit.append_measure(static_cast<QubitId>(base + i), static_cast<QubitId>(base + i));
        }
    }
    return circuit;
}

// Pinned to the dense engine on one component with a non-Clifford T gate
// (docs/BUILD_PLAN.md Phase 4: an all-CX circuit's components are now
// cheaper on the stabilizer engine, which would give a much smaller
// actual_state_bytes than this fixed-value test expects). Only one
// component needs pinning -- the peak is a max over regions, and the
// dense-pinned region's bytes still dominate the other (now cheaper)
// regions.
Circuit make_one_hundred_qubit_circuit_pinned_to_dense() {
    constexpr std::uint32_t kNumComponents = 10;
    constexpr std::uint32_t kComponentSize = 10;
    Circuit circuit(kNumComponents * kComponentSize, kNumComponents * kComponentSize);
    for (std::uint32_t block = 0; block < kNumComponents; ++block) {
        const QubitId base = block * kComponentSize;
        for (QubitId i = 0; i + 1 < kComponentSize; ++i) {
            circuit.append(Opcode::CX,
                           {static_cast<QubitId>(base + i), static_cast<QubitId>(base + i + 1)});
        }
        if (block == 0) {
            circuit.append(Opcode::T, {base});
        }
        for (QubitId i = 0; i < kComponentSize; ++i) {
            circuit.append_measure(static_cast<QubitId>(base + i), static_cast<QubitId>(base + i));
        }
    }
    return circuit;
}

// Acceptance criterion 5 (docs/BUILD_PLAN.md Phase 3, 3.12): a 100-qubit
// circuit made from components of at most 12 qubits can execute
// counts-only without allocating a 100-qubit dense state. 16*2^100 bytes
// is astronomically larger than any real budget, so this would be
// infeasible if a 100-qubit dense state were ever considered; a modest
// explicit budget (10 MiB, comfortably more than one 10-qubit region's
// 16 KiB but nowhere near global scale) proves only regional allocation
// happens. The criterion's own wording only requires "a small peak", not
// specifically the Factorized strategy -- as of Phase 6, this exact
// circuit (10 independent, internally-low-entanglement 10-qubit
// components) legitimately costs less as a single whole-circuit DirectMPS
// region than as 10 separate Factorized regions (every inter-component
// cut has Schmidt rank exactly 1, so MPS's own bond-based cost stays
// small for the whole 100-qubit circuit), which still satisfies this
// criterion (an even smaller peak, via a different -- also correct --
// route) without needing Factorized specifically.
TEST(FactorizedDifferentialTest, OneHundredQubitsCountsOnlySucceedsWithoutGlobalAllocation) {
    const Circuit circuit = make_one_hundred_qubit_circuit_pinned_to_dense();
    const RunOptions options{
        .shots = 10, .want_statevector = false, .seed = 1, .memory_budget_bytes = 10 * 1024 * 1024};
    const Result result = run(circuit, options);
    EXPECT_TRUE(result.plan().strategy == Strategy::Factorized ||
                result.plan().strategy == Strategy::DirectMPS)
        << "either route avoids a 100-qubit dense allocation, which is the acceptance property";
    EXPECT_LT(result.actual_state_bytes(), 10ULL * 1024 * 1024)
        << "must stay comfortably within budget, nowhere close to a 100-qubit dense state";

    std::uint64_t total = 0;
    for (const auto& [key, count] : result.counts()) {
        EXPECT_EQ(key.num_bits(), 100U);
        total += count;
    }
    EXPECT_EQ(total, 10U);
}

// Acceptance criterion 6: the same 100-qubit circuit, with a statevector
// request, is rejected by the planner before any global-state allocation
// when the configured budget cannot hold 16 * 2^100 bytes -- true of any
// real budget, since that figure vastly exceeds all storage that has ever
// existed. Both candidates' checked-arithmetic overflow at n=100
// (checked_pow2 refuses n>62) makes both infeasible, so plan_job itself
// throws before Runtime::execute -- and therefore before any allocation
// -- is ever reached.
TEST(FactorizedDifferentialTest, OneHundredQubitsStatevectorRejectedBeforeGlobalAllocation) {
    const Circuit circuit = make_one_hundred_qubit_circuit();
    const RunOptions options{
        .shots = 0, .want_statevector = true, .seed = 1, .memory_budget_bytes = 10 * 1024 * 1024};
    EXPECT_THROW(static_cast<void>(run(circuit, options)), common::ResourceExceeded);
}

}  // namespace
}  // namespace menos
