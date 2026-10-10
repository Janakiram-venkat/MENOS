#pragma once

#include <cstdint>
#include <vector>

#include "menos/circuit.h"

namespace menos::analysis {

struct LightConeResult {
    // global_phase dropped (it cancels in <psi|P|psi> and does not
    // affect the probabilities of a measured qubit subset,
    // docs/BUILD_PLAN.md Phase 5), qubits remapped ascending by which
    // survived. num_clbits is unchanged from the original circuit --
    // clbits are a global, unpartitioned namespace, matching
    // src/plan/region_builder.cpp's own MeasureOp::clbit convention.
    Circuit reduced_circuit;
    // reduced_to_original[i] is the original circuit's qubit id for the
    // reduced circuit's local qubit i, ascending.
    std::vector<QubitId> reduced_to_original;
};

// The backward light-cone of `support` (docs/BUILD_PLAN.md Phase 5): walk
// circuit.operations() in reverse; an operation is in the cone if any of
// its qubits (its own qubit, for a MeasureOp) is already in the current
// cone, and it then adds all its qubits to the cone. The reduced circuit
// keeps only in-cone operations, in original program order, with qubits
// remapped. Exact and lossless: an operation outside the union of every
// requested output's causal past cannot affect that output, so dropping
// it changes no result, only estimated/actual cost.
[[nodiscard]] LightConeResult compute_light_cone(const Circuit& circuit,
                                                 const std::vector<QubitId>& support);

}  // namespace menos::analysis
