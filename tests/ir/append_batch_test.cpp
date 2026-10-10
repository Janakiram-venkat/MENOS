#include "menos/circuit.h"

#include <gtest/gtest.h>

#include <random>
#include <vector>

#include "common/error.h"

namespace menos {
namespace {

// Builds a fixed-seed randomized sequence of GateOp-shaped data (opcode,
// qubits, params), used both to append() one-by-one and to append_batch()
// in one call, so the two circuits are built from identical source data.
struct RandomOps {
    std::vector<Opcode> opcodes;
    std::vector<std::vector<QubitId>> qubits;
    std::vector<std::vector<double>> params;
};

// seed (an arbitrary 64-bit RNG seed) and count (an operation count,
// typically in the hundreds) are not realistically confusable at any call
// site.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
RandomOps make_random_ops(std::uint64_t seed, std::size_t count) {
    std::mt19937_64 rng(seed);
    std::uniform_int_distribution<int> pick_shape(0, 2);  // 0: 1q/0p, 1: 1q/1p, 2: 2q/0p
    std::uniform_int_distribution<QubitId> pick_qubit(0, 4);
    std::uniform_real_distribution<double> pick_param(-3.14, 3.14);

    RandomOps ops;
    for (std::size_t i = 0; i < count; ++i) {
        switch (pick_shape(rng)) {
            case 0:
                ops.opcodes.push_back(Opcode::X);
                ops.qubits.push_back({pick_qubit(rng)});
                ops.params.push_back({});
                break;
            case 1:
                ops.opcodes.push_back(Opcode::RX);
                ops.qubits.push_back({pick_qubit(rng)});
                ops.params.push_back({pick_param(rng)});
                break;
            default: {
                QubitId a = pick_qubit(rng);
                QubitId b = pick_qubit(rng);
                if (a == b) {
                    b = (b + 1) % 5;
                }
                ops.opcodes.push_back(Opcode::CX);
                ops.qubits.push_back({a, b});
                ops.params.push_back({});
                break;
            }
        }
    }
    return ops;
}

Circuit build_via_append(const RandomOps& ops) {
    Circuit circuit(5, 0);
    for (std::size_t i = 0; i < ops.opcodes.size(); ++i) {
        circuit.append(ops.opcodes[i], ops.qubits[i], ops.params[i]);
    }
    return circuit;
}

Circuit build_via_append_batch(const RandomOps& ops) {
    Circuit circuit(5, 0);
    std::vector<QubitId> flat_qubits;
    std::vector<std::uint32_t> qubit_offsets{0};
    std::vector<double> flat_params;
    std::vector<std::uint32_t> param_offsets{0};
    for (std::size_t i = 0; i < ops.opcodes.size(); ++i) {
        flat_qubits.insert(flat_qubits.end(), ops.qubits[i].begin(), ops.qubits[i].end());
        qubit_offsets.push_back(static_cast<std::uint32_t>(flat_qubits.size()));
        flat_params.insert(flat_params.end(), ops.params[i].begin(), ops.params[i].end());
        param_offsets.push_back(static_cast<std::uint32_t>(flat_params.size()));
    }
    circuit.append_batch(ops.opcodes, flat_qubits, qubit_offsets, flat_params, param_offsets);
    return circuit;
}

TEST(AppendBatchTest, MatchesSequentialAppendElementForElement) {
    const RandomOps ops = make_random_ops(/*seed=*/12345, /*count=*/200);
    const Circuit expected = build_via_append(ops);
    const Circuit actual = build_via_append_batch(ops);

    ASSERT_EQ(actual.operations().size(), expected.operations().size());
    for (std::size_t i = 0; i < expected.operations().size(); ++i) {
        const auto& e = std::get<GateOp>(expected.operations()[i]);
        const auto& a = std::get<GateOp>(actual.operations()[i]);
        EXPECT_EQ(a.opcode, e.opcode) << "operation " << i;
        EXPECT_EQ(a.qubits, e.qubits) << "operation " << i;
        EXPECT_EQ(a.params, e.params) << "operation " << i;
    }
}

TEST(AppendBatchTest, EmptyBatchAppendsNothing) {
    Circuit circuit(2, 0);
    circuit.append_batch({}, {}, {0}, {}, {0});
    EXPECT_TRUE(circuit.operations().empty());
}

TEST(AppendBatchTest, MalformedOffsetsAreInternalError) {
    Circuit circuit(2, 0);
    // qubit_offsets.size() must be opcodes.size() + 1 -- here it's short.
    EXPECT_THROW(circuit.append_batch({Opcode::X}, {0}, {0}, {}, {0}), common::InternalError);
}

}  // namespace
}  // namespace menos
