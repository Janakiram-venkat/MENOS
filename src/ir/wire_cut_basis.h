#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include "menos/circuit.h"

namespace menos::ir {

// The wire-cut decomposition of the identity channel (docs/adr/0013 section
// 2.1, from Peng, Harrow, Ozols & Wu, Phys. Rev. Lett. 125, 150504 (2020),
// arXiv:1904.00102, Lemma 1 and Eq. (2)).
//
// The underlying identity, from that paper's proof (Eqs. 12-19), is the
// Pauli-basis expansion of any single-qubit operator:
//
//     A = [ tr(A.I).I + tr(A.X).X + tr(A.Y).Y + tr(A.Z).Z ] / 2
//
// Expanding each Pauli on the preparation side into its own eigenprojectors
// gives eight (observable, prepared state, coefficient) triples with
// coefficients +-1/2, so that the identity channel on a cut wire is
//
//     id(rho) = sum_{i=1}^{8} c_i . tr(O_i . rho) . rho_i
//
// The 1/2 is 1/dim, from tr(sigma_i sigma_j) = 2 delta_ij. It is not a
// convention: dropping it scales every reconstructed value by 2^K for K cuts.
//
// The 1-norm of this decomposition is sum|c_i| = 8 * 1/2 = 4, which is
// independently the optimal gamma-factor for a wire cut without classical
// communication (Brenner, Piveteau & Sutter, arXiv:2302.03366,
// Proposition 4.1), and gamma^{2K} = 16^K = 2^{4K} reproduces the exponent in
// Peng's Theorem 1. Two independent sources agree on the same constant; that
// cross-check is asserted in tests/common/wire_cut_basis_test.cpp rather than
// trusted, because Phase 8 shipped a cost constant that nothing compared
// against anything.

// Which Pauli is measured on the upstream fragment at a cut.
enum class CutObservable : std::uint8_t { I, X, Y, Z };

// Which eigenstate the downstream fragment's cut wire is prepared in.
enum class CutPrepState : std::uint8_t { Zero, One, Plus, Minus, PlusI, MinusI };

struct WireCutTerm {
    CutObservable observable;
    CutPrepState prepared;
    double coefficient;  // always +1/2 or -1/2
};

// Exactly eight terms per cut, in the fixed order of docs/adr/0013's table.
// The order is part of the determinism contract: floating-point addition is
// not associative, so the order in which the reconstruction sums its branches
// must not vary between runs (CLAUDE.md's determinism domain invariant).
inline constexpr std::size_t kWireCutTermCount = 8;

// The number of DISTINCT values each side actually needs, which is smaller
// than kWireCutTermCount because terms share operands: four observables
// (I, X, Y, Z) and six prepared states. These drive the cost model's
// distinct-evaluation count (docs/adr/0013 Eq. 4.1) and are the reason a K-cut
// plan does not need 8^K fragment executions.
inline constexpr std::size_t kDistinctCutObservables = 4;
inline constexpr std::size_t kDistinctCutPrepStates = 6;

[[nodiscard]] std::span<const WireCutTerm> wire_cut_terms();

// The (x, z) bit pair identifying this observable in a PauliString, using the
// convention already fixed in include/menos/observable.h: I/X/Z/Y for
// (0,0)/(1,0)/(0,1)/(1,1), with Y = i*X*Z.
struct PauliBits {
    bool x;
    bool z;
};
[[nodiscard]] PauliBits observable_bits(CutObservable observable);

// The gates that take |0> to the given eigenstate, in application order.
// Every one is Clifford, so a stabilizer fragment stays stabilizer-eligible
// and a Pauli-propagation fragment stays within its supported set.
[[nodiscard]] std::vector<Opcode> preparation_gates(CutPrepState state);

}  // namespace menos::ir
