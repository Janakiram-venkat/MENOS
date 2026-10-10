// End-to-end AccuracyPolicy coverage through the full public API (run()),
// exercising the accuracy_policy plumbing added in src/runtime/runtime.cpp:
// EngineExecutionRequest::accuracy_policy actually reaching the engine,
// Result::exact() reflecting the post-execution truth (not just
// Plan::exact's own pre-execution estimate), and Region::
// mps_accumulated_error_bound/explain() reporting the a-posteriori bound
// (docs/BUILD_PLAN.md Phase 6). Truncation's own numerical correctness
// (measured error vs. the reported bound) is unit-tested directly in
// tests/engines/mps/apply_gate_test.cpp; this file only tests that the
// plumbing between RunOptions and the engine is wired correctly.

#include "menos/menos.h"

#include <gtest/gtest.h>

namespace menos {
namespace {

// A 6-qubit GHZ chain: Schmidt rank exactly 2 at every one of its 5
// cuts, so MPS is the cheapest route (dense/stabilizer both cost far
// more at this trivial bond scale -- consistent with
// tests/api/stabilizer_scale_test.cpp's own findings).
Circuit make_ghz_chain_circuit() {
    Circuit circuit(6, 6);
    circuit.append(Opcode::H, {0});
    for (QubitId q = 0; q + 1 < 6; ++q) {
        circuit.append(Opcode::CX, {q, static_cast<QubitId>(q + 1)});
    }
    for (QubitId q = 0; q < 6; ++q) {
        circuit.append_measure(q, q);
    }
    return circuit;
}

TEST(AccuracyPolicyTest, ExactPolicyOnMPSWinnerNeverTruncates) {
    const Circuit circuit = make_ghz_chain_circuit();
    const Result result = run(circuit,
                              RunOptions{.shots = 20,
                                         .want_statevector = false,
                                         .seed = 1});  // accuracy_policy defaults to Exact

    ASSERT_EQ(result.plan().strategy, Strategy::DirectMPS);
    EXPECT_TRUE(result.plan().exact);
    EXPECT_TRUE(result.exact());
    ASSERT_EQ(result.plan().regions.size(), 1U);
    EXPECT_FALSE(result.plan().regions[0].mps_accumulated_error_bound.has_value());

    // Bell/GHZ correlation must hold exactly: every measured qubit
    // agrees.
    for (const auto& [key, count] : result.counts()) {
        static_cast<void>(count);
        const bool first = key.test(0);
        for (std::uint32_t q = 1; q < 6; ++q) {
            EXPECT_EQ(key.test(q), first) << "GHZ state must measure all-equal bits";
        }
    }
}

TEST(AccuracyPolicyTest, ControlledPolicyWithTightCapTruncatesAndReportsBound) {
    const Circuit circuit = make_ghz_chain_circuit();
    AccuracyPolicy controlled;
    controlled.kind = AccuracyPolicyKind::Controlled;
    controlled.max_bond_dimension = 1;  // the true bond is 2 at every cut: forces truncation

    const Result result =
        run(circuit,
            RunOptions{
                .shots = 20, .want_statevector = false, .seed = 1, .accuracy_policy = controlled});

    ASSERT_EQ(result.plan().strategy, Strategy::DirectMPS);
    EXPECT_FALSE(result.plan().exact)
        << "Controlled always marks the PLAN as not provably exact, regardless of whether this "
           "particular run ends up truncating";
    EXPECT_FALSE(result.exact()) << "a bond cap of 1 against a true bond of 2 must truncate here";
    ASSERT_EQ(result.plan().regions.size(), 1U);
    ASSERT_TRUE(result.plan().regions[0].mps_accumulated_error_bound.has_value());
    // ASSERT_TRUE above already guarantees this; clang-tidy can't see
    // through the gtest macro.
    // NOLINTBEGIN(bugprone-unchecked-optional-access)
    EXPECT_GE(*result.plan().regions[0].mps_accumulated_error_bound, 0.0);
    // NOLINTEND(bugprone-unchecked-optional-access)

    const std::string text = result.explain();
    EXPECT_NE(text.find("strategy   direct_mps (approximate)"), std::string::npos) << text;
    EXPECT_NE(text.find("accumulated_error_bound="), std::string::npos) << text;
    EXPECT_NE(text.find("not a pre-execution guarantee"), std::string::npos) << text;
}

// A Controlled policy whose cap never actually binds (the true bond,
// 2, fits comfortably under a cap of 8) must still report exact()
// == true -- the a-posteriori truth about what this specific run
// actually discarded (nothing), which can differ from Plan::exact's
// own pre-execution estimate (always false under Controlled, since
// the policy permits truncation even when none is ultimately needed).
TEST(AccuracyPolicyTest, ControlledPolicyThatNeverBindsStillReportsExactResult) {
    const Circuit circuit = make_ghz_chain_circuit();
    AccuracyPolicy controlled;
    controlled.kind = AccuracyPolicyKind::Controlled;
    controlled.max_bond_dimension = 8;

    const Result result =
        run(circuit,
            RunOptions{
                .shots = 20, .want_statevector = false, .seed = 1, .accuracy_policy = controlled});

    ASSERT_EQ(result.plan().strategy, Strategy::DirectMPS);
    EXPECT_FALSE(result.plan().exact)
        << "the plan-time estimate stays pessimistic under Controlled";
    EXPECT_TRUE(result.exact()) << "but nothing was actually discarded, so the run itself is exact";
    ASSERT_EQ(result.plan().regions.size(), 1U);
    EXPECT_FALSE(result.plan().regions[0].mps_accumulated_error_bound.has_value());

    for (const auto& [key, count] : result.counts()) {
        static_cast<void>(count);
        const bool first = key.test(0);
        for (std::uint32_t q = 1; q < 6; ++q) {
            EXPECT_EQ(key.test(q), first) << "GHZ state must measure all-equal bits";
        }
    }
}

}  // namespace
}  // namespace menos
