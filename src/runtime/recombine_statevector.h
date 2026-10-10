#pragma once

#include <complex>
#include <cstdint>
#include <span>
#include <vector>

#include "menos/plan.h"

namespace menos::runtime {

// Allocates the global statevector (16 * 2^num_qubits bytes), initialized
// to all-1.0 (the multiplicative identity for the per-region multiply
// pass below). Throws ResourceExceeded before allocating if the size
// exceeds budget_bytes (checked here even though the planner's own
// feasibility check already guarantees this fits, per
// docs/BUILD_PLAN.md 3.3's literal requirement that this routine itself
// reject an impossible size before allocating).
[[nodiscard]] std::vector<std::complex<double>> allocate_global_statevector(
    std::uint32_t num_qubits, std::uint64_t budget_bytes);

// Multiplies region r's local amplitudes into the global statevector in
// place (docs/BUILD_PLAN.md Phase 3, 3.3's exact tensor-product
// recombination): for every global amplitude index a, global[a] *=
// psi_r[gather(a, region)], where gather places region r's local bits at
// their global positions (region.local_to_global). Called once per
// region, in component order, by the caller's own per-region execution
// loop -- kept as a standalone step (not owning that loop itself) so the
// caller can execute each region's gates exactly once and feed the same
// amplitudes into both this recombination and (when also requested)
// counts sampling, rather than executing a region twice.
void multiply_region_into_global(std::span<std::complex<double>> global,
                                 const Region& region,
                                 std::span<const std::complex<double>> psi_r);

}  // namespace menos::runtime
