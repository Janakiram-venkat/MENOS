#include "common/rng.h"

namespace menos::common {
namespace {

// 2^-53: the number of mantissa bits of a double, so (x >> 11) spans
// exactly the representable range [0, 2^53).
constexpr double kTwoPow53Inv = 1.0 / 9007199254740992.0;

}  // namespace

Rng::Rng(std::uint64_t seed) : engine_(seed) {}

double Rng::next_uniform() {
    return word_to_uniform(engine_());
}

std::uint64_t Rng::next_word() {
    return engine_();
}

double Rng::word_to_uniform(std::uint64_t word) {
    return static_cast<double>(word >> 11) * kTwoPow53Inv;
}

}  // namespace menos::common
