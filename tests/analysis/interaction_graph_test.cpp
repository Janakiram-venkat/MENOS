#include "analysis/interaction_graph.h"

#include <gtest/gtest.h>

#include <complex>
#include <vector>

namespace menos::analysis {
namespace {

TEST(InteractionGraphTest, NoGatesGivesAllSingletons) {
    const Circuit circuit(4, 0);
    const InteractionGraph graph = build_interaction_graph(circuit);
    ASSERT_EQ(graph.components.size(), 4U);
    for (std::uint32_t q = 0; q < 4; ++q) {
        EXPECT_EQ(graph.components[q].qubits, (std::vector<QubitId>{q}));
        EXPECT_EQ(graph.qubit_component[q], q);
    }
}

TEST(InteractionGraphTest, OneQubitGatesDoNotConnect) {
    Circuit circuit(3, 0);
    circuit.append(Opcode::H, {0});
    circuit.append(Opcode::X, {1});
    circuit.append(Opcode::RZ, {2}, {0.5});
    const InteractionGraph graph = build_interaction_graph(circuit);
    ASSERT_EQ(graph.components.size(), 3U);
    for (std::uint32_t q = 0; q < 3; ++q) {
        EXPECT_EQ(graph.components[q].qubits, (std::vector<QubitId>{q}));
    }
}

TEST(InteractionGraphTest, TwoQubitGateConnectsItsOperands) {
    // q0-q1 connected by CX; q2 idle.
    Circuit circuit(3, 0);
    circuit.append(Opcode::CX, {0, 1});
    const InteractionGraph graph = build_interaction_graph(circuit);
    ASSERT_EQ(graph.components.size(), 2U);
    EXPECT_EQ(graph.components[0].qubits, (std::vector<QubitId>{0, 1}));
    EXPECT_EQ(graph.components[1].qubits, (std::vector<QubitId>{2}));
    EXPECT_EQ(graph.qubit_component[0], graph.qubit_component[1]);
    EXPECT_NE(graph.qubit_component[0], graph.qubit_component[2]);
}

TEST(InteractionGraphTest, ChainOfTwoQubitGatesMergesTransitively) {
    // CX(0,1), CX(1,2): all three qubits end up in one component even
    // though 0 and 2 never appear together in an operation.
    Circuit circuit(3, 0);
    circuit.append(Opcode::CX, {0, 1});
    circuit.append(Opcode::CX, {1, 2});
    const InteractionGraph graph = build_interaction_graph(circuit);
    ASSERT_EQ(graph.components.size(), 1U);
    EXPECT_EQ(graph.components[0].qubits, (std::vector<QubitId>{0, 1, 2}));
}

TEST(InteractionGraphTest, ThreeQubitCcxConnectsAllOperands) {
    Circuit circuit(4, 0);
    circuit.append(Opcode::CCX, {0, 2, 3});  // q1 idle
    const InteractionGraph graph = build_interaction_graph(circuit);
    ASSERT_EQ(graph.components.size(), 2U);
    EXPECT_EQ(graph.components[0].qubits, (std::vector<QubitId>{0, 2, 3}));
    EXPECT_EQ(graph.components[1].qubits, (std::vector<QubitId>{1}));
}

TEST(InteractionGraphTest, UnitaryConnectsAllOperandsIncludingInterleaved) {
    // A 2-qubit Unitary on non-adjacent, interleaved qubits {0, 2}; q1
    // and q3 stay in their own singleton components.
    Circuit circuit(4, 0);
    const std::vector<std::complex<double>> matrix(16, {0.0, 0.0});
    circuit.append_unitary({0, 2}, matrix);
    const InteractionGraph graph = build_interaction_graph(circuit);
    ASSERT_EQ(graph.components.size(), 3U);
    EXPECT_EQ(graph.components[0].qubits, (std::vector<QubitId>{0, 2}));
    EXPECT_EQ(graph.components[1].qubits, (std::vector<QubitId>{1}));
    EXPECT_EQ(graph.components[2].qubits, (std::vector<QubitId>{3}));
}

TEST(InteractionGraphTest, MeasurementsDoNotConnect) {
    Circuit circuit(2, 2);
    circuit.append_measure(0, 0);
    circuit.append_measure(1, 1);
    const InteractionGraph graph = build_interaction_graph(circuit);
    ASSERT_EQ(graph.components.size(), 2U);
}

TEST(InteractionGraphTest, ComponentsOrderedByAscendingSmallestQubit) {
    // q3-q4 connect first in program order, then q0-q1 -- components must
    // still be numbered by ascending smallest-qubit, not discovery order
    // within the union-find unite() calls.
    Circuit circuit(5, 0);
    circuit.append(Opcode::CX, {3, 4});
    circuit.append(Opcode::CX, {0, 1});
    const InteractionGraph graph = build_interaction_graph(circuit);
    ASSERT_EQ(graph.components.size(), 3U);
    EXPECT_EQ(graph.components[0].qubits, (std::vector<QubitId>{0, 1}));
    EXPECT_EQ(graph.components[1].qubits, (std::vector<QubitId>{2}));
    EXPECT_EQ(graph.components[2].qubits, (std::vector<QubitId>{3, 4}));
}

TEST(InteractionGraphTest, DeterministicAcrossRepeatedCalls) {
    Circuit circuit(6, 0);
    circuit.append(Opcode::CX, {4, 1});
    circuit.append(Opcode::CX, {1, 5});
    circuit.append(Opcode::CX, {2, 0});
    const InteractionGraph first = build_interaction_graph(circuit);
    for (int i = 0; i < 20; ++i) {
        const InteractionGraph again = build_interaction_graph(circuit);
        ASSERT_EQ(again.components.size(), first.components.size());
        for (std::size_t c = 0; c < first.components.size(); ++c) {
            EXPECT_EQ(again.components[c].qubits, first.components[c].qubits) << "component " << c;
        }
        EXPECT_EQ(again.qubit_component, first.qubit_component);
    }
}

// A two-qubit noise channel can correlate its qubits, so it joins them exactly
// as a two-qubit gate does; a one-qubit channel joins nothing (Phase 12,
// docs/adr/0016 section 5).
TEST(InteractionGraphTest, AChannelConnectsItsOperandsLikeAGate) {
    std::vector<std::complex<double>> identity_4x4(16, 0.0);
    for (std::size_t i = 0; i < 4; ++i) {
        identity_4x4[i * 4 + i] = 1.0;
    }
    Circuit circuit(3, 0);
    circuit.append_channel({0, 2}, {identity_4x4});
    circuit.append_channel({1}, {{1.0, 0.0, 0.0, 1.0}});
    const InteractionGraph graph = build_interaction_graph(circuit);
    ASSERT_EQ(graph.components.size(), 2U);
    EXPECT_EQ(graph.qubit_component[0], graph.qubit_component[2]);
    EXPECT_NE(graph.qubit_component[0], graph.qubit_component[1]);
}

}  // namespace
}  // namespace menos::analysis
