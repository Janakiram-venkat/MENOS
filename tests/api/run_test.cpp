#include "menos/menos.h"

#include <gtest/gtest.h>

namespace menos {
namespace {

// width (a clbit count, typically small) and value (the bits to set) are
// not realistically confusable at any call site.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
BitString make_bitstring(std::uint32_t width, std::uint64_t value) {
    BitString bs(width);
    for (std::uint32_t bit = 0; bit < width; ++bit) {
        if ((value >> bit) & std::uint64_t{1}) {
            bs.set(bit, true);
        }
    }
    return bs;
}

TEST(RunTest, BellCircuitProducesOnlyCorrelatedOutcomes) {
    Circuit circuit(2, 2);
    circuit.append(Opcode::H, {0});
    circuit.append(Opcode::CX, {0, 1});
    circuit.append_measure(0, 0);
    circuit.append_measure(1, 1);

    const Result result = run(circuit, RunOptions{.shots = 1000, .seed = 42});

    std::uint64_t total = 0;
    const BitString zero = make_bitstring(2, 0);
    const BitString three = make_bitstring(2, 3);
    for (const auto& [key, count] : result.counts()) {
        EXPECT_TRUE(key == zero || key == three);
        total += count;
    }
    EXPECT_EQ(total, 1000U);
}

TEST(RunTest, AppendBatchProducesByteIdenticalAmplitudesToSequentialAppend) {
    Circuit sequential(3, 0);
    sequential.append(Opcode::H, {0});
    sequential.append(Opcode::CX, {0, 1});
    sequential.append(Opcode::RX, {2}, {0.7});
    sequential.append(Opcode::CX, {1, 2});

    Circuit batched(3, 0);
    batched.append_batch({Opcode::H, Opcode::CX, Opcode::RX, Opcode::CX},
                         {0, 0, 1, 2, 1, 2},
                         /*qubit_offsets=*/{0, 1, 3, 4, 6},
                         /*params=*/{0.7},
                         /*param_offsets=*/{0, 0, 0, 1, 1});

    const RunOptions options{.shots = 0, .want_statevector = true, .seed = 1};
    const Result direct = run(sequential, options);
    const Result via_batch = run(batched, options);

    ASSERT_TRUE(direct.statevector().has_value());
    ASSERT_TRUE(via_batch.statevector().has_value());
    // ASSERT_TRUE above already guarantees this; clang-tidy can't see
    // through the gtest macro.
    EXPECT_EQ(direct.statevector().value(),      // NOLINT(bugprone-unchecked-optional-access)
              via_batch.statevector().value());  // NOLINT(bugprone-unchecked-optional-access)
}

}  // namespace
}  // namespace menos
