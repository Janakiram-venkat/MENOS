#include "menos/batch.h"

#include <gtest/gtest.h>

#include <vector>

#include "common/error.h"
#include "common/splitmix64.h"

namespace menos {
namespace {

Circuit bell() {
    Circuit circuit(2, 2);
    circuit.append(Opcode::H, {0});
    circuit.append(Opcode::CX, {0, 1});
    circuit.append_measure(0, 0);
    circuit.append_measure(1, 1);
    return circuit;
}

Circuit noisy_ghz3() {
    Circuit circuit(3, 3);
    circuit.append(Opcode::H, {0});
    circuit.append(Opcode::CX, {0, 1});
    circuit.append(Opcode::CX, {1, 2});
    for (QubitId q = 0; q < 3; ++q) {
        circuit.append_measure(q, q, ReadoutError{{0.9, 0.1, 0.2, 0.8}});
    }
    return circuit;
}

// docs/BUILD_PLAN.md Phase 14 gate criterion 1: every circuit is planned and
// run by run() itself, under the documented derived seed.
TEST(BatchTest, EachResultEqualsRunUnderItsDerivedSeed) {
    const std::vector<Circuit> circuits{bell(), noisy_ghz3(), bell()};
    const RunOptions options{.shots = 3000, .want_statevector = false, .seed = 2026};
    const std::vector<Result> results = run_batch(circuits, options);
    ASSERT_EQ(results.size(), circuits.size());

    common::SplitMix64 seed_mixer(options.seed);
    for (std::size_t i = 0; i < circuits.size(); ++i) {
        RunOptions single = options;
        single.seed = seed_mixer.next();
        const Result expected = run(circuits[i], single);
        EXPECT_EQ(results[i].counts(), expected.counts()) << "circuit " << i;
        EXPECT_EQ(results[i].plan().strategy, expected.plan().strategy) << "circuit " << i;
        EXPECT_EQ(results[i].exact(), expected.exact()) << "circuit " << i;
    }
    // Identical circuits in one batch get independent streams.
    EXPECT_NE(results[0].counts(), results[2].counts());
}

TEST(BatchTest, TheSameSeedGivesTheSameResults) {
    const std::vector<Circuit> circuits{bell(), noisy_ghz3()};
    const RunOptions options{.shots = 2000, .want_statevector = false, .seed = 7};
    const std::vector<Result> a = run_batch(circuits, options);
    const std::vector<Result> b = run_batch(circuits, options);
    for (std::size_t i = 0; i < circuits.size(); ++i) {
        EXPECT_EQ(a[i].counts(), b[i].counts());
    }
}

TEST(BatchTest, AnEmptyBatchIsRejected) {
    const RunOptions options{.shots = 10, .want_statevector = false, .seed = 1};
    EXPECT_THROW((void)run_batch({}, options), common::InvalidCircuit);
}

TEST(BatchTest, AFailingCircuitFailsTheWholeBatch) {
    std::vector<Circuit> circuits{bell(), Circuit(1, 1)};
    circuits[1].append(Opcode::H, {5});  // out of range: validation rejects it
    const RunOptions options{.shots = 10, .want_statevector = false, .seed = 1};
    EXPECT_THROW((void)run_batch(circuits, options), common::InvalidCircuit);
}

}  // namespace
}  // namespace menos
