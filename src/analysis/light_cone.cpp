#include "analysis/light_cone.h"

#include <cstddef>
#include <type_traits>
#include <variant>

namespace menos::analysis {
namespace {

// Qubits an operation touches, including a MeasureOp's own qubit
// (unlike src/analysis/interaction_graph.cpp's operands_of, which
// excludes MeasureOp since it creates no interaction-graph edge -- a
// measurement is still causally relevant to the light cone of its own
// clbit). A ChannelOp is in the cone exactly like a gate: it is validated as
// trace preserving, so one acting entirely outside the causal past of the
// requested outputs cannot change their reduced state and is dropped, while
// one touching the cone is kept and pulls its qubits in (docs/adr/0016
// section 5). Exhaustive over Operation, like append_remapped below: a new
// alternative fails the static_assert until it is given its own rule.
std::vector<QubitId> touched_qubits(const Operation& op) {
    return std::visit(
        [](const auto& o) -> std::vector<QubitId> {
            using T = std::decay_t<decltype(o)>;
            if constexpr (std::is_same_v<T, GateOp> || std::is_same_v<T, UnitaryOp> ||
                          std::is_same_v<T, ChannelOp>) {
                return o.qubits;
            } else {
                static_assert(std::is_same_v<T, MeasureOp>);
                return {o.qubit};
            }
        },
        op);
}

std::vector<QubitId> remapped(const std::vector<QubitId>& qubits,
                              const std::vector<QubitId>& original_to_reduced) {
    std::vector<QubitId> out(qubits.size());
    for (std::size_t i = 0; i < qubits.size(); ++i) {
        out[i] = original_to_reduced[qubits[i]];
    }
    return out;
}

void append_remapped(Circuit& reduced,
                     const Operation& op,
                     const std::vector<QubitId>& original_to_reduced) {
    std::visit(
        [&](const auto& o) {
            using T = std::decay_t<decltype(o)>;
            if constexpr (std::is_same_v<T, GateOp>) {
                reduced.append(o.opcode, remapped(o.qubits, original_to_reduced), o.params);
            } else if constexpr (std::is_same_v<T, UnitaryOp>) {
                reduced.append_unitary(remapped(o.qubits, original_to_reduced), o.matrix);
            } else if constexpr (std::is_same_v<T, ChannelOp>) {
                reduced.append_channel(remapped(o.qubits, original_to_reduced), o.kraus);
            } else {
                static_assert(std::is_same_v<T, MeasureOp>);
                reduced.append_measure(original_to_reduced[o.qubit], o.clbit, o.readout_error);
            }
        },
        op);
}

}  // namespace

LightConeResult compute_light_cone(const Circuit& circuit, const std::vector<QubitId>& support) {
    const std::uint32_t n = circuit.num_qubits();
    std::vector<bool> in_cone(n, false);
    for (QubitId q : support) {
        in_cone[q] = true;
    }

    const std::vector<Operation>& ops = circuit.operations();
    std::vector<bool> op_in_cone(ops.size(), false);
    for (std::size_t i = ops.size(); i-- > 0;) {
        const std::vector<QubitId> touched = touched_qubits(ops[i]);
        bool any_in_cone = false;
        for (QubitId q : touched) {
            if (in_cone[q]) {
                any_in_cone = true;
                break;
            }
        }
        if (any_in_cone) {
            op_in_cone[i] = true;
            for (QubitId q : touched) {
                in_cone[q] = true;
            }
        }
    }

    std::vector<QubitId> reduced_to_original;
    std::vector<QubitId> original_to_reduced(n, 0);
    for (QubitId q = 0; q < n; ++q) {
        if (in_cone[q]) {
            original_to_reduced[q] = static_cast<QubitId>(reduced_to_original.size());
            reduced_to_original.push_back(q);
        }
    }

    Circuit reduced(static_cast<std::uint32_t>(reduced_to_original.size()), circuit.num_clbits());
    for (std::size_t i = 0; i < ops.size(); ++i) {
        if (op_in_cone[i]) {
            append_remapped(reduced, ops[i], original_to_reduced);
        }
    }

    return LightConeResult{std::move(reduced), std::move(reduced_to_original)};
}

}  // namespace menos::analysis
