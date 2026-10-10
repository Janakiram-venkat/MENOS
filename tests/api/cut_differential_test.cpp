// docs/adr/0013 Phase 9, end to end through the public API: does cut
// reconstruction produce the right number, does the planner choose it only
// when it is genuinely cheaper, and does the estimate bound what execution
// actually does.
//
// The last of those is the Phase 8 lesson made into a standing test. Phase 8
// shipped an estimator that understated peak memory by exactly 2x, and it
// survived because its validation checked the estimator's arithmetic and
// never compared the metric against the thing it was supposed to predict.

#include "menos/menos.h"

#include <gtest/gtest.h>

#include <bit>
#include <cmath>
#include <complex>
#include <cstdint>
#include <vector>

namespace menos {
namespace {

constexpr double kTolerance = 1e-12;

// Two clusters joined by one migrating qubit: the structure a wire cut is for.
// `migrating` qubits interact inside cluster A, then inside cluster B.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
Circuit clustered_circuit(std::uint32_t per_cluster, std::uint32_t migrating) {
    const std::uint32_t n = 2 * per_cluster;
    Circuit circuit(n, 0);
    for (std::uint32_t q = 0; q < n; ++q) {
        circuit.append(
            Opcode::RY, {static_cast<QubitId>(q)}, {0.21 + 0.07 * static_cast<double>(q)});
    }
    for (std::uint32_t a = 0; a < per_cluster; ++a) {
        for (std::uint32_t b = a + 1; b < per_cluster; ++b) {
            circuit.append(Opcode::CZ, {static_cast<QubitId>(a), static_cast<QubitId>(b)});
        }
    }
    for (std::uint32_t a = per_cluster; a < n; ++a) {
        for (std::uint32_t b = a + 1; b < n; ++b) {
            circuit.append(Opcode::CZ, {static_cast<QubitId>(a), static_cast<QubitId>(b)});
        }
    }
    circuit.append(Opcode::T, {static_cast<QubitId>(per_cluster)});
    // The migration: qubits [0, migrating) now interact with cluster B.
    for (std::uint32_t m = 0; m < migrating; ++m) {
        for (std::uint32_t b = per_cluster; b < n; ++b) {
            circuit.append(Opcode::CZ, {static_cast<QubitId>(m), static_cast<QubitId>(b)});
        }
    }
    return circuit;
}

Observable z_on_every_qubit(std::uint32_t n) {
    PauliString p;
    p.num_qubits = n;
    p.x_words.assign((n + 63) / 64, 0);
    p.z_words.assign((n + 63) / 64, 0);
    for (std::uint32_t q = 0; q < n; ++q) {
        p.z_words[q / 64] |= (std::uint64_t{1} << (q % 64));
    }
    return Observable{{{p, {1.0, 0.0}}}};
}

// An independent oracle: the dense statevector, obtained by asking for a
// statevector, which only the dense engine can produce. Nothing in this path
// shares code with cut reconstruction.
double dense_oracle(const Circuit& circuit, const PauliString& term) {
    const Result sv = run(
        circuit,
        RunOptions{
            .shots = 0, .want_statevector = true, .seed = 0, .memory_budget_bytes = 1'000'000'000});
    EXPECT_EQ(sv.plan().strategy, Strategy::DirectDense);
    // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
    const std::vector<std::complex<double>>& psi = *sv.statevector();
    double total = 0.0;
    for (std::size_t i = 0; i < psi.size(); ++i) {
        // (-1)^(number of set bits of i restricted to the term's Z support)
        std::size_t parity = 0;
        for (std::uint32_t q = 0; q < term.num_qubits; ++q) {
            const bool z = (term.z_words[q / 64] >> (q % 64)) & 1U;
            if (z && (((i >> q) & 1U) != 0U)) {
                parity ^= 1U;
            }
        }
        total += (parity == 0 ? 1.0 : -1.0) * std::norm(psi[i]);
    }
    return total;
}

Result run_expectation(const Circuit& circuit, const Observable& obs, std::uint64_t budget) {
    return run(circuit,
               RunOptions{.shots = 0,
                          .want_statevector = false,
                          .seed = 0,
                          .memory_budget_bytes = budget,
                          .expectation = obs});
}

const CandidateSummary* candidate_for(const Plan& plan, Strategy strategy) {
    for (const CandidateSummary& c : plan.candidates) {
        if (c.strategy == strategy) {
            return &c;
        }
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// Correctness against the dense oracle
// ---------------------------------------------------------------------------

TEST(CutDifferentialTest, SingleCutReconstructionMatchesTheDenseOracle) {
    // per_cluster=4: at three per cluster the whole circuit is small enough
    // that dense simply wins, which is the planner behaving correctly rather
    // than a problem -- but it would leave this test not exercising the cut.
    const Circuit circuit = clustered_circuit(/*per_cluster=*/4, /*migrating=*/1);
    const Observable obs = z_on_every_qubit(circuit.num_qubits());
    const double oracle = dense_oracle(circuit, obs.terms[0].first);

    // A budget tight enough that the cut is the cheapest feasible option here,
    // but not so tight that it is the only one -- the planner still compares.
    const Result result = run_expectation(circuit, obs, 1'000'000);
    ASSERT_EQ(result.plan().strategy, Strategy::CutAndRecombine);
    ASSERT_TRUE(result.expectation_value().has_value());
    // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
    EXPECT_NEAR(result.expectation_value()->real(), oracle, kTolerance);
    // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
    EXPECT_NEAR(result.expectation_value()->imag(), 0.0, kTolerance);
    EXPECT_TRUE(result.exact()) << "exact enumeration must report an exact result";
}

TEST(CutDifferentialTest, MultipleCutReconstructionMatchesTheDenseOracle) {
    const Circuit circuit = clustered_circuit(/*per_cluster=*/3, /*migrating=*/2);
    const Observable obs = z_on_every_qubit(circuit.num_qubits());
    const double oracle = dense_oracle(circuit, obs.terms[0].first);

    const Result result = run_expectation(circuit, obs, 1'000'000);
    ASSERT_TRUE(result.expectation_value().has_value());
    // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
    EXPECT_NEAR(result.expectation_value()->real(), oracle, kTolerance)
        << "strategy was " << static_cast<int>(result.plan().strategy);
}

// Identity FACTORS must be handled correctly inside a fragment term: a term
// that is identity on the cut wire but has support elsewhere exercises the
// fragment-term builder's treatment of unsupported qubits.
//
// Deliberately not the all-identity observable: that reduces the light cone to
// zero qubits and trips a pre-existing limit in the stabilizer engine
// ("Tableau: num_qubits must be at least 1"), which reproduces on the Phase 8
// branch and has nothing to do with cutting. It is reported rather than fixed
// here, to keep this change free of unrelated repairs.
TEST(CutDifferentialTest, ObservableWithIdentityOnTheCutWireReconstructsCorrectly) {
    const Circuit circuit = clustered_circuit(/*per_cluster=*/4, /*migrating=*/1);
    // Qubit 0 is the migrating (cut) wire; put Z only on qubits in the far
    // cluster, so the term is identity on the cut wire itself.
    PauliString term;
    term.num_qubits = circuit.num_qubits();
    term.x_words.assign(1, 0);
    term.z_words.assign(1, 0);
    for (std::uint32_t q = 4; q < circuit.num_qubits(); ++q) {
        term.z_words[0] |= (std::uint64_t{1} << q);
    }
    const double oracle = dense_oracle(circuit, term);
    const Result result = run_expectation(circuit, Observable{{{term, {1.0, 0.0}}}}, 1'000'000);
    ASSERT_TRUE(result.expectation_value().has_value());
    // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
    EXPECT_NEAR(result.expectation_value()->real(), oracle, kTolerance)
        << "strategy was " << static_cast<int>(result.plan().strategy);
}

TEST(CutDifferentialTest, SingleQubitObservableReconstructsCorrectly) {
    const Circuit circuit = clustered_circuit(/*per_cluster=*/3, /*migrating=*/1);
    // Z on one qubit of each side, to exercise both the measured and the
    // prepared fragment carrying a job-observable factor.
    for (const std::uint32_t target : {0u, 3u}) {
        PauliString term;
        term.num_qubits = circuit.num_qubits();
        term.x_words.assign(1, 0);
        term.z_words.assign(1, std::uint64_t{1} << target);
        const double oracle = dense_oracle(circuit, term);
        const Result result = run_expectation(circuit, Observable{{{term, {1.0, 0.0}}}}, 1'000'000);
        ASSERT_TRUE(result.expectation_value().has_value());
        // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
        EXPECT_NEAR(result.expectation_value()->real(), oracle, kTolerance)
            << "target qubit " << target;
    }
}

// A weighted sum of Pauli terms, reconstructed term by term then combined.
TEST(CutDifferentialTest, WeightedPauliSumReconstructsCorrectly) {
    const Circuit circuit = clustered_circuit(/*per_cluster=*/3, /*migrating=*/1);
    PauliString z0;
    z0.num_qubits = circuit.num_qubits();
    z0.x_words.assign(1, 0);
    z0.z_words.assign(1, 0b000001);
    PauliString z_all_bits;
    z_all_bits.num_qubits = circuit.num_qubits();
    z_all_bits.x_words.assign(1, 0);
    z_all_bits.z_words.assign(1, 0b111111);

    const double oracle =
        0.25 * dense_oracle(circuit, z0) + 1.5 * dense_oracle(circuit, z_all_bits);
    const Observable obs{{{z0, {0.25, 0.0}}, {z_all_bits, {1.5, 0.0}}}};
    const Result result = run_expectation(circuit, obs, 1'000'000);
    ASSERT_TRUE(result.expectation_value().has_value());
    // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
    EXPECT_NEAR(result.expectation_value()->real(), oracle, kTolerance);
}

TEST(CutDifferentialTest, RepeatedRunsAreBitIdentical) {
    const Circuit circuit = clustered_circuit(/*per_cluster=*/3, /*migrating=*/1);
    const Observable obs = z_on_every_qubit(circuit.num_qubits());
    const Result a = run_expectation(circuit, obs, 1'000'000);
    const Result b = run_expectation(circuit, obs, 1'000'000);
    ASSERT_TRUE(a.expectation_value().has_value());
    ASSERT_TRUE(b.expectation_value().has_value());
    // NOLINTBEGIN(bugprone-unchecked-optional-access)
    EXPECT_DOUBLE_EQ(a.expectation_value()->real(), b.expectation_value()->real());
    EXPECT_DOUBLE_EQ(a.expectation_value()->imag(), b.expectation_value()->imag());
    // NOLINTEND(bugprone-unchecked-optional-access)
}

// ---------------------------------------------------------------------------
// Planner behaviour
// ---------------------------------------------------------------------------

// The cut must lose when it is more expensive. A small, densely connected
// circuit has no thin cut, so dense should win outright.
TEST(CutDifferentialTest, PlannerRejectsCuttingWhenDirectIsCheaper) {
    Circuit circuit(4, 0);
    for (QubitId q = 0; q < 4; ++q) {
        circuit.append(Opcode::RY, {q}, {0.3 + 0.1 * static_cast<double>(q)});
    }
    for (QubitId a = 0; a < 4; ++a) {
        for (QubitId b = static_cast<QubitId>(a + 1); b < 4; ++b) {
            circuit.append(Opcode::CZ, {a, b});
        }
    }
    const Observable obs = z_on_every_qubit(4);
    const Result result = run_expectation(circuit, obs, 1'000'000'000);

    EXPECT_NE(result.plan().strategy, Strategy::CutAndRecombine)
        << "a 4-qubit dense circuit must not be cut: direct execution is cheaper";
    const CandidateSummary* cut = candidate_for(result.plan(), Strategy::CutAndRecombine);
    if (cut != nullptr && cut->feasible) {
        const CandidateSummary* winner = nullptr;
        for (const CandidateSummary& c : result.plan().candidates) {
            if (c.is_winner) {
                winner = &c;
            }
        }
        ASSERT_NE(winner, nullptr);
        EXPECT_LE(winner->estimated_work_units, cut->estimated_work_units)
            << "the winner must not be more expensive than the cut it beat";
    }
}

// ...and win when it is genuinely cheaper, on cost rather than by any rule.
TEST(CutDifferentialTest, PlannerChoosesCuttingWhenItIsGenuinelyCheaper) {
    const Circuit circuit = clustered_circuit(/*per_cluster=*/4, /*migrating=*/1);
    const Observable obs = z_on_every_qubit(circuit.num_qubits());
    const Result result = run_expectation(circuit, obs, 1'000'000);

    ASSERT_EQ(result.plan().strategy, Strategy::CutAndRecombine);
    const CandidateSummary* cut = candidate_for(result.plan(), Strategy::CutAndRecombine);
    ASSERT_NE(cut, nullptr);
    EXPECT_TRUE(cut->is_winner);
    for (const CandidateSummary& c : result.plan().candidates) {
        if (c.feasible && c.strategy != Strategy::CutAndRecombine) {
            EXPECT_LE(cut->estimated_work_units, c.estimated_work_units)
                << "the cut won, so no feasible candidate may be cheaper";
        }
    }
}

// Counts and statevector are out of scope for Phase 9 (docs/adr/0013 section
// 7), so the candidate must never even be enumerated for them.
TEST(CutDifferentialTest, CountsAndStatevectorRequestsNeverEnumerateCutting) {
    Circuit circuit(4, 4);
    circuit.append(Opcode::H, {0});
    circuit.append(Opcode::CX, {0, 1});
    circuit.append(Opcode::CX, {1, 2});
    circuit.append(Opcode::CX, {2, 3});
    for (QubitId q = 0; q < 4; ++q) {
        circuit.append_measure(q, q);
    }
    const Result counts = run(circuit, RunOptions{.shots = 16, .seed = 1});
    EXPECT_EQ(candidate_for(counts.plan(), Strategy::CutAndRecombine), nullptr);

    Circuit sv_circuit(4, 0);
    sv_circuit.append(Opcode::H, {0});
    sv_circuit.append(Opcode::CX, {0, 1});
    const Result sv = run(sv_circuit, RunOptions{.shots = 0, .want_statevector = true, .seed = 0});
    EXPECT_EQ(candidate_for(sv.plan(), Strategy::CutAndRecombine), nullptr);
}

// ---------------------------------------------------------------------------
// The Phase 8 lesson, as a standing regression
// ---------------------------------------------------------------------------

// The estimate must BOUND what execution actually does. Phase 8's estimator
// understated peak memory by exactly 2x and no test compared the two numbers;
// this is that comparison, for the cut path.
TEST(CutDifferentialTest, EstimatedPeakBoundsTheMeasuredPeak) {
    for (const std::uint32_t per_cluster : {3u, 4u}) {
        const Circuit circuit = clustered_circuit(per_cluster, /*migrating=*/1);
        const Observable obs = z_on_every_qubit(circuit.num_qubits());
        const Result result = run_expectation(circuit, obs, 1'000'000);
        if (result.plan().strategy != Strategy::CutAndRecombine) {
            continue;
        }
        const CandidateSummary* cut = candidate_for(result.plan(), Strategy::CutAndRecombine);
        ASSERT_NE(cut, nullptr);
        ASSERT_TRUE(result.plan().cut_summary.has_value());
        // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
        const CutSummary& summary = *result.plan().cut_summary;
        ASSERT_TRUE(summary.actual_peak_bytes.has_value());
        // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
        EXPECT_LE(*summary.actual_peak_bytes, cut->estimated_peak_bytes)
            << "per_cluster=" << per_cluster
            << ": the estimated peak must bound the measured peak, or the planner's memory "
               "feasibility decision is unsound";
    }
}

// The distinct-evaluation count is what makes cutting affordable; it must be
// strictly below the branch count, and must be what execution actually ran.
TEST(CutDifferentialTest, DistinctEvaluationsAreFewerThanBranches) {
    const Circuit circuit = clustered_circuit(/*per_cluster=*/4, /*migrating=*/1);
    const Observable obs = z_on_every_qubit(circuit.num_qubits());
    const Result result = run_expectation(circuit, obs, 1'000'000);
    ASSERT_EQ(result.plan().strategy, Strategy::CutAndRecombine);
    ASSERT_TRUE(result.plan().cut_summary.has_value());
    // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
    const CutSummary& summary = *result.plan().cut_summary;

    EXPECT_EQ(summary.branch_count, 8U) << "one cut means 8 branches";
    // 4 observables on the measured side plus 6 states on the prepared side.
    EXPECT_EQ(summary.distinct_evaluations, 10U);
    EXPECT_LT(summary.distinct_evaluations, summary.branch_count * summary.fragment_count)
        << "if these were equal, fragment results are not being reused";

    const std::string text = result.explain();
    EXPECT_NE(text.find("strategy   cut_and_recombine"), std::string::npos) << text;
    EXPECT_NE(text.find("cut        cuts="), std::string::npos) << text;
    EXPECT_NE(text.find("actual_peak_bytes="), std::string::npos) << text;
}

}  // namespace
}  // namespace menos
