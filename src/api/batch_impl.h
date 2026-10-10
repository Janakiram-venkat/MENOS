#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include "menos/menos.h"

namespace menos::api {

// run_batch with per-circuit options: circuit i runs under options[i], with its
// seed replaced by the (i+1)-th output of SplitMix64(seed). run_batch passes
// one options set for every circuit; readout mitigation passes different shot
// counts for its target and calibration circuits under the same seed rule.
[[nodiscard]] std::vector<Result> run_batch_with_options(std::span<const Circuit> circuits,
                                                         std::span<const RunOptions> options,
                                                         std::uint64_t seed);

}  // namespace menos::api
