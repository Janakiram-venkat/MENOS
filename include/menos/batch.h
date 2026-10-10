#pragma once

#include <span>
#include <vector>

#include "menos/circuit.h"
#include "menos/menos.h"
#include "menos/result.h"

namespace menos {

// Runs several circuits under one set of options and returns their Results in
// input order (Phase 14, docs/adr/0018 section 2).
//
// Not a second planner: each circuit is validated, planned by plan_job and
// executed exactly as run() does, including its own time budget and replans.
//
// SEEDS. Circuit i runs with seed_i, the (i+1)-th output of
// SplitMix64(options.seed), not with options.seed itself, so the circuits draw
// independent random streams. Readout mitigation depends on this: its
// calibration and target runs must not share readout flips. Consequently
// run_batch({c}, o)[0] differs from run(c, o); call run() for a single circuit
// that should keep its Phase 1-13 seed.
//
// Throws InvalidCircuit for an empty batch, and propagates the first exception
// any circuit throws; no partial batch is returned.
[[nodiscard]] std::vector<Result> run_batch(std::span<const Circuit> circuits,
                                            const RunOptions& options);

}  // namespace menos
