#pragma once

#include <cstdint>
#include <vector>

#include "menos/circuit.h"

namespace menos::analysis {

// One connected component of the interaction graph. qubits is ascending
// by global qubit index (docs/BUILD_PLAN.md Phase 3, 3.1: "preserve
// ascending global qubit order within each component").
struct Component {
    std::vector<QubitId> qubits;
};

struct InteractionGraph {
    std::uint32_t num_qubits;
    // Deterministic order: components[0] has the smallest minimum-qubit
    // index of any component, components[1] the next-smallest, and so
    // on -- never hash order (docs/BUILD_PLAN.md 3.1).
    std::vector<Component> components;
    // qubit_component[q] is the index into components owning global
    // qubit q. Same size as circuit.num_qubits().
    std::vector<std::uint32_t> qubit_component;
};

// Builds the interaction graph (docs/BUILD_PLAN.md Phase 3, 3.1): one
// vertex per qubit; for every operation with two or more quantum
// operands, an (implicit, via union-find) edge between every pair of
// operands; measurements and idle qubits add no edge. Precondition: the
// circuit already passed menos::ir::validate() -- this function performs
// no KIR-level validation of its own.
[[nodiscard]] InteractionGraph build_interaction_graph(const Circuit& circuit);

}  // namespace menos::analysis
