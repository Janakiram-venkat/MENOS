#include "ir/noise.h"

#include <cstddef>
#include <type_traits>
#include <variant>

namespace menos::ir {

std::optional<std::string> first_noise_reason(std::span<const Operation> operations) {
    for (std::size_t i = 0; i < operations.size(); ++i) {
        std::optional<std::string> reason = std::visit(
            [i](const auto& o) -> std::optional<std::string> {
                using T = std::decay_t<decltype(o)>;
                if constexpr (std::is_same_v<T, ChannelOp>) {
                    return "operation " + std::to_string(i) +
                           " is a noise channel; this engine simulates pure states only";
                } else if constexpr (std::is_same_v<T, MeasureOp>) {
                    if (o.readout_error.has_value()) {
                        return "operation " + std::to_string(i) +
                               " is a measurement with a readout error; this engine does not "
                               "model readout error";
                    }
                    return std::nullopt;
                } else {
                    static_assert(std::is_same_v<T, GateOp> || std::is_same_v<T, UnitaryOp>);
                    return std::nullopt;
                }
            },
            operations[i]);
        if (reason.has_value()) {
            return reason;
        }
    }
    return std::nullopt;
}

}  // namespace menos::ir
