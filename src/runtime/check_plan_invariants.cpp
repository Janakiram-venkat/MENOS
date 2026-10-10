#include "runtime/check_plan_invariants.h"

#include <limits>
#include <type_traits>
#include <variant>
#include <vector>

#include "common/error.h"

namespace menos::runtime {

void check_plan_invariants(const Circuit& circuit, const Plan& plan) {
    const std::uint32_t n = circuit.num_qubits();
    constexpr std::uint32_t kUnassigned = std::numeric_limits<std::uint32_t>::max();
    std::vector<std::uint32_t> qubit_to_region(n, kUnassigned);
    for (std::size_t r = 0; r < plan.regions.size(); ++r) {
        for (const QubitId q : plan.regions[r].local_to_global) {
            qubit_to_region[q] = static_cast<std::uint32_t>(r);
        }
    }
    for (std::uint32_t q = 0; q < n; ++q) {
        common::ensure(qubit_to_region[q] != kUnassigned,
                       "check_plan_invariants: qubit not covered by any region");
    }

    for (const Operation& op : circuit.operations()) {
        // Exhaustive over Operation: a new alternative fails the static_assert
        // until it is given its own rule (docs/adr/0016 section 7).
        const std::vector<QubitId>* operands = std::visit(
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
        if (operands == nullptr || operands->size() < 2) {
            continue;
        }
        const std::uint32_t region = qubit_to_region[(*operands)[0]];
        for (const QubitId q : *operands) {
            common::ensure(qubit_to_region[q] == region,
                           "check_plan_invariants: a multi-qubit operation spans more than one "
                           "region -- violated planning invariant");
        }
    }
}

}  // namespace menos::runtime
