#pragma once

#include <complex>
#include <cstdint>
#include <span>
#include <vector>

#include "engines/mps/mps_state.h"
#include "menos/accuracy_policy.h"
#include "menos/circuit.h"

namespace menos::engines::mps {

// Accumulates the discarded weight (sum of squared discarded singular
// values) from every truncation step across a whole circuit's
// execution -- the raw material for the a-posteriori accumulated error
// bound the engine reports (docs/adr/0009). Empty iff nothing was ever
// truncated (which, under AccuracyPolicy::Exact, is every run: Exact
// never discards a singular value, per docs/BUILD_PLAN.md Phase 6).
struct TruncationStats {
    std::vector<double> discarded_weight_per_step;

    [[nodiscard]] bool any_truncation() const {
        return !discarded_weight_per_step.empty();
    }
};

// Applies a k-qubit gate (matrix, a 2^k x 2^k row-major matrix, bit j
// of its row/column index corresponding to operand qubits[j] --
// CLAUDE.md's operand-order rule) to state. qubits need not be
// adjacent or sorted: an internal SWAP network (site-adjacent-pair
// applications of the same primitive this function itself uses)
// brings them together, the gate is applied at the now-consecutive
// block, and the network is undone -- the qubit-to-site mapping (site
// i is always qubit i) is unchanged once this call returns.
//
// Under AccuracyPolicy::Exact, every internal bond split (including
// the SWAP network's own splits) keeps full Schmidt rank -- no
// truncation ever occurs, matching MPSState::move_orthogonality_center_to's
// own losslessness. Under AccuracyPolicy::Controlled, a split keeps
// singular value sigma_i iff sigma_i > policy.tolerance * sigma_max
// (sigma_max = that split's own largest singular value) and the kept
// count is additionally capped at policy.max_bond_dimension when set;
// every dropped singular value's squared magnitude is added to stats.
//
// Precondition: qubits.size() == the operand count implied by matrix
// (matrix.size() == (1 << qubits.size())^2); every entry of qubits is
// a distinct, in-range site index. Not re-checked here -- validated
// once at the KIR boundary (menos::ir::validate), like
// engines::dense::apply_matrix's identical precondition.
void apply_gate(MPSState& state,
                std::span<const std::complex<double>> matrix,
                std::span<const QubitId> qubits,
                const AccuracyPolicy& policy,
                TruncationStats& stats);

}  // namespace menos::engines::mps
