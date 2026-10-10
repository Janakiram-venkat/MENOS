#pragma once

#include <optional>
#include <vector>

#include "menos/circuit.h"
#include "menos/observable.h"

namespace menos::common {

// Splits observable's terms by which qubits they touch, restricted to a
// region: for every term, projects the bits belonging to
// region_qubits (ascending, e.g. a Region::local_to_global) onto local
// indices [0, region_qubits.size()), dropping the rest. A term whose
// full support lies entirely outside region_qubits becomes the identity
// (both masks all-zero) for this region -- its own contribution to that
// region's expectation product is then trivially 1 (docs/BUILD_PLAN.md
// Phase 5, "a component with no qubits in the term's support contributes
// an implicit identity factor"). Term order and coefficients are
// preserved unchanged; coefficients are not consulted here (the caller
// combines them, see src/runtime/combine_expectation.h) -- restriction
// only ever narrows which qubits a term's own operator acts on.
[[nodiscard]] Observable restrict_observable(const Observable& observable,
                                             const std::vector<QubitId>& region_qubits,
                                             const std::vector<QubitId>& global_to_local);

}  // namespace menos::common
