#pragma once

#include <optional>
#include <span>
#include <string>

#include "menos/circuit.h"

namespace menos::ir {

// Why an engine that simulates pure states cannot run `operations`, or
// nullopt if it can: the first ChannelOp (a noise channel) or the first
// MeasureOp carrying a readout error, by index, formatted for explain()
// like every other infeasible reason. Every pure-state engine calls this
// first in its estimate(), so a noisy job is refused with a reason by each
// of them and is never run with its noise dropped (docs/adr/0016 sections 5
// and 7).
[[nodiscard]] std::optional<std::string> first_noise_reason(std::span<const Operation> operations);

}  // namespace menos::ir
