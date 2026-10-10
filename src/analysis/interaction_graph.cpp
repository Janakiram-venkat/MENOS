#include "analysis/interaction_graph.h"

#include <limits>
#include <numeric>
#include <type_traits>
#include <variant>

namespace menos::analysis {
namespace {

// Plain array-indexed union-find over the dense QubitId domain
// [0, num_qubits) -- no hashing, union-by-size, path compression. Its
// internal parent pointers are never read by a caller (see
// build_interaction_graph's own comment on determinism); only find()'s
// return value (a root id, itself in the same dense domain) is observed.
class UnionFind {
public:
    explicit UnionFind(std::uint32_t n) : parent_(n), size_(n, 1) {
        std::iota(parent_.begin(), parent_.end(), QubitId{0});
    }

    QubitId find(QubitId q) {
        while (parent_[q] != q) {
            parent_[q] = parent_[parent_[q]];  // path halving
            q = parent_[q];
        }
        return q;
    }

    void unite(QubitId a, QubitId b) {
        a = find(a);
        b = find(b);
        if (a == b) {
            return;
        }
        if (size_[a] < size_[b]) {
            std::swap(a, b);
        }
        parent_[b] = a;
        size_[a] += size_[b];
    }

private:
    std::vector<QubitId> parent_;
    std::vector<std::uint32_t> size_;
};

// Returns the quantum operands of an operation, empty for MeasureOp
// (measurements create no interaction edge, docs/BUILD_PLAN.md 3.1). A
// ChannelOp joins its qubits exactly as a gate does: a multi-qubit channel can
// correlate them, so they cannot be simulated as independent components
// (docs/adr/0016 section 5). Exhaustive over Operation: a new alternative
// fails the static_assert until it is given its own rule here.
const std::vector<QubitId>* operands_of(const Operation& op) {
    return std::visit(
        [](const auto& o) -> const std::vector<QubitId>* {
            using T = std::decay_t<decltype(o)>;
            if constexpr (std::is_same_v<T, GateOp> || std::is_same_v<T, UnitaryOp> ||
                          std::is_same_v<T, ChannelOp>) {
                return &o.qubits;
            } else {
                static_assert(std::is_same_v<T, MeasureOp>);
                return nullptr;
            }
        },
        op);
}

}  // namespace

InteractionGraph build_interaction_graph(const Circuit& circuit) {
    const std::uint32_t n = circuit.num_qubits();
    UnionFind uf(n);

    for (const Operation& op : circuit.operations()) {
        const std::vector<QubitId>* operands = operands_of(op);
        if (operands == nullptr || operands->size() < 2) {
            continue;  // 1-qubit gates and measurements add no edge
        }
        // Star-union around operands[0]: same resulting connectivity as
        // unioning every pair (docs/BUILD_PLAN.md 3.1: "add an undirected
        // edge between every pair of operands"), since union-find only
        // cares about connectivity, not which specific edges were used.
        for (std::size_t i = 1; i < operands->size(); ++i) {
            uf.unite((*operands)[0], (*operands)[i]);
        }
    }

    InteractionGraph graph;
    graph.num_qubits = n;
    graph.qubit_component.assign(n, 0);

    // Determinism comes entirely from this ascending scan order, never
    // from UnionFind's internal (path-compression-mutated) parent
    // values: a component's smallest member is necessarily visited
    // first, so the first time a given root is seen assigns the next
    // sequential component index, giving both ascending-smallest-qubit
    // component ordering and ascending-qubit-within-component ordering
    // simultaneously, with zero dependence on find()'s internals.
    constexpr std::uint32_t kUnassigned = std::numeric_limits<std::uint32_t>::max();
    std::vector<std::uint32_t> component_of_root(n, kUnassigned);

    for (QubitId q = 0; q < n; ++q) {
        const QubitId root = uf.find(q);
        if (component_of_root[root] == kUnassigned) {
            component_of_root[root] = static_cast<std::uint32_t>(graph.components.size());
            graph.components.push_back(Component{});
        }
        const std::uint32_t component_index = component_of_root[root];
        graph.components[component_index].qubits.push_back(q);
        graph.qubit_component[q] = component_index;
    }

    return graph;
}

}  // namespace menos::analysis
