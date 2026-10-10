#include "ir/wire_cut_basis.h"

#include <array>

#include "common/error.h"

namespace menos::ir {
namespace {

// docs/adr/0013 section 2.1, reproducing the paper's own table. The order is
// fixed and load-bearing (see the header's note on summation order).
constexpr std::array<WireCutTerm, kWireCutTermCount> kTerms = {{
    {CutObservable::I, CutPrepState::Zero, 0.5},
    {CutObservable::I, CutPrepState::One, 0.5},
    {CutObservable::X, CutPrepState::Plus, 0.5},
    {CutObservable::X, CutPrepState::Minus, -0.5},
    {CutObservable::Y, CutPrepState::PlusI, 0.5},
    {CutObservable::Y, CutPrepState::MinusI, -0.5},
    {CutObservable::Z, CutPrepState::Zero, 0.5},
    {CutObservable::Z, CutPrepState::One, -0.5},
}};

}  // namespace

std::span<const WireCutTerm> wire_cut_terms() {
    return std::span<const WireCutTerm>(kTerms.data(), kTerms.size());
}

PauliBits observable_bits(CutObservable observable) {
    switch (observable) {
        case CutObservable::I:
            return PauliBits{false, false};
        case CutObservable::X:
            return PauliBits{true, false};
        case CutObservable::Y:
            return PauliBits{true, true};
        case CutObservable::Z:
            return PauliBits{false, true};
    }
    throw common::InternalError("observable_bits: unknown CutObservable");
}

std::vector<Opcode> preparation_gates(CutPrepState state) {
    // Each sequence is applied to |0> in order. Verified by direct state
    // computation in tests/common/wire_cut_basis_test.cpp against the
    // eigenvector each one is supposed to produce, rather than asserted here.
    switch (state) {
        case CutPrepState::Zero:
            return {};
        case CutPrepState::One:
            return {Opcode::X};
        case CutPrepState::Plus:
            return {Opcode::H};
        case CutPrepState::Minus:
            return {Opcode::X, Opcode::H};
        case CutPrepState::PlusI:
            return {Opcode::H, Opcode::S};
        case CutPrepState::MinusI:
            return {Opcode::X, Opcode::H, Opcode::S};
    }
    throw common::InternalError("preparation_gates: unknown CutPrepState");
}

}  // namespace menos::ir
