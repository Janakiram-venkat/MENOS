#pragma once

#include <map>

#include "menos/circuit.h"

namespace menos::ir {

// Global last-write-wins clbit resolution (CLAUDE.md: "a later write to
// the same bit wins"), computed once from the whole circuit -- not from
// any per-region operation list -- because two different qubits in two
// different regions can legally write the same clbit at different
// points in program order, and only the temporally-last write survives,
// regardless of which region it belongs to. Shared by
// src/runtime/runtime.cpp (resolving which region's measurement
// survives) and src/analysis/light_cone.cpp (seeding the light cone
// with only the qubits whose measurement actually survives, not a
// stale overwritten one).
[[nodiscard]] std::map<ClbitId, QubitId> compute_final_writes(const Circuit& circuit);

}  // namespace menos::ir
