#include "menos/batch.h"

#include "api/batch_impl.h"
#include "common/error.h"
#include "common/splitmix64.h"

namespace menos {
namespace api {

std::vector<Result> run_batch_with_options(std::span<const Circuit> circuits,
                                           std::span<const RunOptions> options,
                                           std::uint64_t seed) {
    common::ensure(circuits.size() == options.size(),
                   "run_batch_with_options: one options set per circuit");
    if (circuits.empty()) {
        throw common::InvalidCircuit("run_batch needs at least one circuit");
    }
    common::SplitMix64 seed_mixer(seed);
    std::vector<Result> results;
    results.reserve(circuits.size());
    for (std::size_t i = 0; i < circuits.size(); ++i) {
        RunOptions circuit_options = options[i];
        circuit_options.seed = seed_mixer.next();
        results.push_back(run(circuits[i], circuit_options));
    }
    return results;
}

}  // namespace api

std::vector<Result> run_batch(std::span<const Circuit> circuits, const RunOptions& options) {
    const std::vector<RunOptions> per_circuit(circuits.size(), options);
    return api::run_batch_with_options(circuits, per_circuit, options.seed);
}

}  // namespace menos
