#include "ir/depth.h"

#include <algorithm>
#include <type_traits>
#include <unordered_map>
#include <variant>

namespace menos::ir {
namespace {

// Exhaustive over Operation: a new alternative fails the static_assert until
// it is given its own rule (docs/adr/0016 section 7).
std::vector<QubitId> touched_qubits(const Operation& operation) {
    return std::visit(
        [](const auto& op) -> std::vector<QubitId> {
            using T = std::decay_t<decltype(op)>;
            if constexpr (std::is_same_v<T, GateOp> || std::is_same_v<T, UnitaryOp> ||
                          std::is_same_v<T, ChannelOp>) {
                return op.qubits;
            } else {
                static_assert(std::is_same_v<T, MeasureOp>);
                return {op.qubit};
            }
        },
        operation);
}

}  // namespace

int depth(const Circuit& circuit) {
    std::unordered_map<QubitId, int> depth_at_qubit;
    int max_depth = 0;

    for (const Operation& operation : circuit.operations()) {
        int chain_depth = 0;
        for (QubitId qubit : touched_qubits(operation)) {
            const auto it = depth_at_qubit.find(qubit);
            const int qubit_depth = it == depth_at_qubit.end() ? 0 : it->second;
            chain_depth = std::max(chain_depth, qubit_depth);
        }
        chain_depth += 1;
        for (QubitId qubit : touched_qubits(operation)) {
            depth_at_qubit[qubit] = chain_depth;
        }
        max_depth = std::max(max_depth, chain_depth);
    }

    return max_depth;
}

}  // namespace menos::ir
