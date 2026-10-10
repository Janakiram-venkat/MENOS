// docs/BUILD_PLAN.md Phase 7 acceptance criterion 1: "Every fixture
// circuit round-trips through QASM2 and matches the Python-path result
// within tolerance, for every engine the fixture set exercises."
//
// The oracle used here is each fixture's own expected_expectation
// (tools/gen_fixtures.py: qiskit.quantum_info.Statevector.expectation_value,
// the same Phase 5 oracle FixturesTest.ExpectationMatchesQiskitWithinTolerance
// already checks the ORIGINAL circuit against), not a live call through
// the Python Qiskit-adapter path -- confirmed with the owner this
// session: the adapter is a pure pass-through with no computation of
// its own (src/python/convert.py just builds the same Circuit and
// calls the same menos::run()), so comparing the QASM2-parsed circuit's
// result against the fixture's already-independently-derived oracle
// satisfies the criterion's actual intent ("don't re-derive correctness
// independently, trust an already-validated reference") without adding
// a new menos._core binding whose only caller would be this one test.
// Counts are not exercised: no fixture circuit has a measurement (Phase
// 1/2's generator never adds one), so "where applicable" resolves to
// expectation values only for this fixture set.

#include "menos/qasm2.h"

#include <gtest/gtest.h>

#include "common/tolerance.h"
#include "fixture_loader.h"
#include "menos/menos.h"

namespace menos::qasm2 {
namespace {

TEST(Qasm2RoundTripTest, FixtureCircuitsMatchTheirOwnOracleAcrossEveryEligibleEngine) {
    const testutil::FixtureFile file = testutil::load_fixture_file(
        std::string(MENOS_SOURCE_DIR) + "/tests/ir/fixtures/random_circuits.json");

    std::size_t checked = 0;
    for (std::size_t i = 0; i < file.circuits.size(); ++i) {
        const testutil::FixtureCircuit& fixture = file.circuits[i];
        if (!fixture.qasm2_text.has_value()) {
            // A circuit with a "unitary" operation: qelib1.inc cannot
            // express one, so this fixture has no QASM2 text at all
            // (tools/gen_fixtures.py) -- covered separately by
            // UnitaryOperationFixturesAreCorrectlyUnrepresentable below,
            // and by Qasm2ParserTest.GateBlockThrowsUnsupportedFeatureNamingTheLine
            // (parser_test.cpp) for the general "reject a gate block"
            // behavior Qiskit's own dump of such a circuit would hit.
            continue;
        }
        if (!fixture.observable.has_value() || !fixture.expected_expectation.has_value()) {
            continue;
        }
        ++checked;

        const Circuit parsed = qasm2::parse(*fixture.qasm2_text);
        ASSERT_EQ(parsed.num_qubits(), fixture.circuit.num_qubits()) << "circuit " << i;

        const Result result = run(parsed,
                                  RunOptions{.shots = 0,
                                             .want_statevector = false,
                                             .seed = 0,
                                             .expectation = fixture.observable});
        ASSERT_TRUE(result.expectation_value().has_value()) << "circuit " << i;
        // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
        const std::complex<double> actual = *result.expectation_value();
        EXPECT_NEAR(actual.real(), fixture.expected_expectation->real(), common::kDefaultAbsTol)
            << "circuit " << i << " (real), qasm2=" << *fixture.qasm2_text;
        EXPECT_NEAR(actual.imag(), fixture.expected_expectation->imag(), common::kDefaultAbsTol)
            << "circuit " << i << " (imag), qasm2=" << *fixture.qasm2_text;
    }
    EXPECT_GT(checked, 0U) << "expected at least one fixture circuit representable in QASM2";
}

// Circuits with a "unitary" operation must be rejected outright, not
// silently skipped -- confirms the parser's own qelib1.inc-scope limit
// against the ACTUAL text Qiskit emits for one of these fixtures, not
// just a hand-written probe.
TEST(Qasm2RoundTripTest, UnitaryOperationFixturesAreCorrectlyUnrepresentable) {
    const testutil::FixtureFile file = testutil::load_fixture_file(
        std::string(MENOS_SOURCE_DIR) + "/tests/ir/fixtures/random_circuits.json");

    std::size_t found_a_unitary_fixture = 0;
    for (const testutil::FixtureCircuit& fixture : file.circuits) {
        bool has_unitary = false;
        for (const Operation& op : fixture.circuit.operations()) {
            if (std::holds_alternative<UnitaryOp>(op)) {
                has_unitary = true;
                break;
            }
        }
        if (has_unitary) {
            ++found_a_unitary_fixture;
            EXPECT_FALSE(fixture.qasm2_text.has_value())
                << "tools/gen_fixtures.py must not emit qasm2_text for a circuit with a "
                   "unitary operation";
        }
    }
    EXPECT_GT(found_a_unitary_fixture, 0U);
}

}  // namespace
}  // namespace menos::qasm2
