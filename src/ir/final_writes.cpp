#include "ir/final_writes.h"

#include <variant>

namespace menos::ir {

std::map<ClbitId, QubitId> compute_final_writes(const Circuit& circuit) {
    std::map<ClbitId, QubitId> final_writes;
    for (const Operation& operation : circuit.operations()) {
        if (const auto* measure = std::get_if<MeasureOp>(&operation)) {
            final_writes[measure->clbit] = measure->qubit;
        }
    }
    return final_writes;
}

}  // namespace menos::ir
