#pragma once

#include <complex>

#include "engines/mps/mps_state.h"
#include "menos/observable.h"

namespace menos::engines::mps {

// <psi|P|psi> (the raw term value, coefficient NOT applied --
// EngineExecutionResult::expectation_terms's own convention, matching
// docs/adr/0007) by contracting P's per-qubit Pauli matrices against
// state's own canonical tensors directly, sweeping site 0 to
// num_qubits() - 1, never materializing the full statevector
// (docs/BUILD_PLAN.md Phase 6: MPS's capabilities() never advertises
// Statevector, and this is why it does not need to). Precondition:
// pauli.num_qubits == state.num_qubits() (the caller has already
// restricted the term's bits to this region's local qubits, the same
// convention src/engines/stabilizer and src/engines/pauli already
// follow).
[[nodiscard]] std::complex<double> compute_expectation(const MPSState& state,
                                                       const PauliString& pauli);

}  // namespace menos::engines::mps
