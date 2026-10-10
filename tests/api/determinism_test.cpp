#include "menos/menos.h"

#include <gtest/gtest.h>

#include "common/error.h"

namespace menos {
namespace {

TEST(DeterminismTest, SameCircuitAndSeedGiveSameCounts) {
    Circuit circuit(3, 3);
    circuit.append(Opcode::H, {0});
    circuit.append(Opcode::H, {1});
    circuit.append(Opcode::CX, {1, 2});
    circuit.append_measure(0, 0);
    circuit.append_measure(1, 1);
    circuit.append_measure(2, 2);

    const RunOptions options{.shots = 5000, .want_statevector = false, .seed = 12345};
    const auto counts_a = run(circuit, options).counts();
    const auto counts_b = run(circuit, options).counts();

    EXPECT_EQ(counts_a, counts_b);
}

// docs/BUILD_PLAN.md Phase 6 acceptance criterion 6: "Same-seed
// determinism holds for counts and expectation values." (Cross-build,
// dev-vs-release, is verified by construction, not a dual-binary test:
// MENOS's determinism argument -- fixed enumeration order, splitmix64
// sub-seeding, no hash-order or pointer-value dependence, no
// std::uniform_*_distribution -- is about the algorithm's structure,
// independent of optimization level, matching every earlier phase's
// own determinism test, none of which run a dual-binary comparison
// either.)
TEST(DeterminismTest, MPSCountsAndExpectationAreDeterministicForTheSameSeed) {
    Circuit circuit(6, 6);
    circuit.append(Opcode::H, {0});
    for (QubitId q = 0; q + 1 < 6; ++q) {
        circuit.append(Opcode::CX, {q, static_cast<QubitId>(q + 1)});
    }
    for (QubitId q = 0; q < 6; ++q) {
        circuit.append_measure(q, q);
    }

    const RunOptions options{.shots = 2000, .want_statevector = false, .seed = 777};
    const Result result_a = run(circuit, options);
    const Result result_b = run(circuit, options);
    ASSERT_EQ(result_a.plan().strategy, Strategy::DirectMPS);
    ASSERT_EQ(result_b.plan().strategy, Strategy::DirectMPS);
    EXPECT_EQ(result_a.counts(), result_b.counts());

    PauliString z;
    z.num_qubits = 6;
    z.x_words = {0};
    z.z_words = {0b1};
    const Observable observable{{{z, {1.0, 0.0}}}};
    const RunOptions expectation_options{
        .shots = 0, .want_statevector = false, .seed = 777, .expectation = observable};
    const Result expectation_a = run(circuit, expectation_options);
    const Result expectation_b = run(circuit, expectation_options);
    ASSERT_TRUE(expectation_a.expectation_value().has_value());
    ASSERT_TRUE(expectation_b.expectation_value().has_value());
    // NOLINTBEGIN(bugprone-unchecked-optional-access)
    EXPECT_EQ(*expectation_a.expectation_value(), *expectation_b.expectation_value());
    // NOLINTEND(bugprone-unchecked-optional-access)
}

TEST(RunFailurePathsTest, ShotsWithNoMeasureThrowsInvalidCircuit) {
    Circuit circuit(1, 1);
    circuit.append(Opcode::X, {0});
    EXPECT_THROW((void)run(circuit, RunOptions{.shots = 10, .seed = 0}), common::InvalidCircuit);
}

TEST(RunFailurePathsTest, NothingRequestedThrowsInvalidCircuit) {
    const Circuit circuit(1, 0);
    EXPECT_THROW((void)run(circuit, RunOptions{.shots = 0, .want_statevector = false, .seed = 0}),
                 common::InvalidCircuit);
}

TEST(RunFailurePathsTest, TinyBudgetThrowsResourceExceeded) {
    Circuit circuit(30, 0);
    for (QubitId q = 0; q < 30; ++q) {
        circuit.append(Opcode::H, {q});
    }
    EXPECT_THROW(
        (void)run(
            circuit,
            RunOptions{
                .shots = 0, .want_statevector = true, .seed = 0, .memory_budget_bytes = 1024}),
        common::ResourceExceeded);
}

}  // namespace
}  // namespace menos
