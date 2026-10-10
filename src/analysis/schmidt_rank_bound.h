#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include "menos/circuit.h"

namespace menos::analysis {

// A rigorous, per-cut upper bound on the Schmidt rank (MPS bond
// dimension) a fixed qubit order (site i is always qubit i) would need
// to represent circuit's output exactly. Phase 6 (docs/BUILD_PLAN.md):
// this makes an exact-mode DirectMPS candidate admissible only when the
// bound itself fits the memory budget, without ever running the
// circuit.
struct SchmidtRankBound {
    // bound_per_cut[i] is the bound at the cut between qubit i and
    // qubit i+1, for i in [0, num_qubits - 2]. Empty for a
    // circuit with fewer than 2 qubits (no cut exists).
    std::vector<std::uint64_t> bound_per_cut;

    // The largest bound across every cut -- what a whole-circuit
    // DirectMPS candidate's peak bytes are checked against. Returns 1
    // (trivial, no possible entanglement) for a circuit with fewer than
    // 2 qubits.
    [[nodiscard]] std::uint64_t max_bound() const;
};

// For each cut i, the bound is the product of every crossing
// operation's own operator-Schmidt-rank with respect to the
// bipartition of ITS OWN operands induced by cut i (an operand belongs
// to the left side iff its qubit id is <= i) -- which can differ per
// cut for an operation spanning 3+ qubits with non-adjacent operands,
// since which of its own operands fall on which side changes as the
// cut moves. Capped at 2^min(left_size, right_size), the physical
// dimension limit of the smaller subsystem at that cut.
//
// A proven upper bound, not an estimate, by elementary Schmidt-rank
// submultiplicativity: write a crossing gate's own operator-Schmidt
// decomposition (restricted to its support, extended as identity
// elsewhere) as G = sum_a A_a (x) B_a, chi_G terms, A_a acting only on
// G's operands left of the cut and B_a only on those right of it; and
// the pre-gate state's own Schmidt decomposition across the cut as
// |psi> = sum_b c_b |l_b> |r_b>, chi terms. Then
// G|psi> = sum_{a,b} c_b (A_a|l_b>) (x) (B_a|r_b>), a sum of at most
// chi_G * chi product-across-the-cut terms, so its Schmidt rank is at
// most chi_G * chi (a Schmidt-rank-generating set of size N always
// bounds the rank by N). Applying this gate by gate down the circuit
// gives the running product; the physical dimension cap is the
// separate, always-valid bound that no cut's Schmidt rank can exceed
// the smaller subsystem's Hilbert space dimension. MeasureOp entries
// are skipped: a terminal, single-qubit measurement (CLAUDE.md's
// terminal-measurement invariant) never crosses a cut and carries no
// matrix.
//
// Takes num_qubits and operations directly, matching
// EngineEstimateRequest's own shape (its one caller,
// engines::mps::MPSEngine::estimate(), has an EngineEstimateRequest,
// never a full Circuit).
[[nodiscard]] SchmidtRankBound compute_schmidt_rank_bound(std::uint32_t num_qubits,
                                                          std::span<const Operation> operations);

}  // namespace menos::analysis
