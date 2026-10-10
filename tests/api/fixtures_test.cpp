#include "fixture_loader.h"

#include <gtest/gtest.h>

#include "common/tolerance.h"
#include "menos/menos.h"

namespace menos {
namespace {

// Acceptance criterion 2: "Every fixture circuit matches the Qiskit
// statevector within the tolerance constant, and that constant has a
// recorded derivation" (src/common/tolerance.h).
TEST(FixturesTest, MatchesQiskitStatevectorsWithinTolerance) {
    const testutil::FixtureFile file = testutil::load_fixture_file(
        std::string(MENOS_SOURCE_DIR) + "/tests/ir/fixtures/random_circuits.json");
    ASSERT_GE(file.circuits.size(), 90U) << "expected about 100 fixture circuits from "
                                            "tools/gen_fixtures.py";

    for (std::size_t i = 0; i < file.circuits.size(); ++i) {
        const testutil::FixtureCircuit& fixture = file.circuits[i];
        const Result result =
            run(fixture.circuit, RunOptions{.shots = 0, .want_statevector = true, .seed = 0});
        ASSERT_TRUE(result.statevector().has_value()) << "circuit " << i;
        // ASSERT_TRUE above already guarantees this; clang-tidy can't see
        // through the gtest macro.
        const auto& actual = *result.statevector();  // NOLINT(bugprone-unchecked-optional-access)
        ASSERT_EQ(actual.size(), fixture.expected_statevector.size()) << "circuit " << i;

        for (std::size_t k = 0; k < actual.size(); ++k) {
            EXPECT_NEAR(
                actual[k].real(), fixture.expected_statevector[k].real(), common::kDefaultAbsTol)
                << "circuit " << i << " amplitude " << k
                << " (real), qiskit_version=" << file.qiskit_version;
            EXPECT_NEAR(
                actual[k].imag(), fixture.expected_statevector[k].imag(), common::kDefaultAbsTol)
                << "circuit " << i << " amplitude " << k
                << " (imag), qiskit_version=" << file.qiskit_version;
        }
    }
}

// docs/BUILD_PLAN.md Phase 5's required test: "Dense expectation matches
// the oracle within tolerance on the full fixture set" -- the random
// weighted-Pauli-observable extension to tools/gen_fixtures.py, oracle
// values from qiskit.quantum_info.Statevector.expectation_value.
TEST(FixturesTest, ExpectationMatchesQiskitWithinTolerance) {
    const testutil::FixtureFile file = testutil::load_fixture_file(
        std::string(MENOS_SOURCE_DIR) + "/tests/ir/fixtures/random_circuits.json");

    std::size_t checked = 0;
    for (std::size_t i = 0; i < file.circuits.size(); ++i) {
        const testutil::FixtureCircuit& fixture = file.circuits[i];
        if (!fixture.observable.has_value() || !fixture.expected_expectation.has_value()) {
            continue;
        }
        ++checked;
        const Result result = run(fixture.circuit,
                                  RunOptions{.shots = 0,
                                             .want_statevector = false,
                                             .seed = 0,
                                             .expectation = fixture.observable});
        ASSERT_TRUE(result.expectation_value().has_value()) << "circuit " << i;
        // ASSERT_TRUE above already guarantees this; clang-tidy can't
        // see through the gtest macro.
        const std::complex<double> actual =
            *result.expectation_value();  // NOLINT(bugprone-unchecked-optional-access)
        EXPECT_NEAR(actual.real(), fixture.expected_expectation->real(), common::kDefaultAbsTol)
            << "circuit " << i << " (real), qiskit_version=" << file.qiskit_version;
        EXPECT_NEAR(actual.imag(), fixture.expected_expectation->imag(), common::kDefaultAbsTol)
            << "circuit " << i << " (imag), qiskit_version=" << file.qiskit_version;
    }
    EXPECT_GE(checked, 90U) << "expected observables on about 100 fixture circuits from "
                               "tools/gen_fixtures.py";
}

}  // namespace
}  // namespace menos
