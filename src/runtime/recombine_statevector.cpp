#include "runtime/recombine_statevector.h"

#include <string>

#include "common/checked_math.h"
#include "common/error.h"

namespace menos::runtime {
namespace {

// Gathers region r's local amplitude index from a global amplitude index:
// bit i of the local index is bit local_to_global[i] of the global index.
std::size_t gather(std::size_t global_index, const std::vector<QubitId>& local_to_global) {
    std::size_t local = 0;
    for (std::size_t i = 0; i < local_to_global.size(); ++i) {
        if ((global_index >> local_to_global[i]) & std::size_t{1}) {
            local |= (std::size_t{1} << i);
        }
    }
    return local;
}

}  // namespace

// num_qubits (a small qubit count) and budget_bytes (a byte count, always
// vastly larger) are not realistically confusable at any call site,
// matching engines::dense::StateVector's constructor precedent.
// NOLINTBEGIN(bugprone-easily-swappable-parameters)
std::vector<std::complex<double>> allocate_global_statevector(std::uint32_t num_qubits,
                                                              std::uint64_t budget_bytes) {
    // NOLINTEND(bugprone-easily-swappable-parameters)
    const std::uint64_t num_amplitudes = common::checked_pow2(num_qubits);
    const std::uint64_t bytes = common::checked_mul(num_amplitudes, sizeof(std::complex<double>));
    if (bytes > budget_bytes) {
        throw common::ResourceExceeded("allocate_global_statevector: global statevector requires " +
                                       std::to_string(bytes) + " bytes, budget is " +
                                       std::to_string(budget_bytes) + " bytes");
    }
    return std::vector<std::complex<double>>(num_amplitudes, std::complex<double>{1.0, 0.0});
}

void multiply_region_into_global(std::span<std::complex<double>> global,
                                 const Region& region,
                                 std::span<const std::complex<double>> psi_r) {
    for (std::size_t a = 0; a < global.size(); ++a) {
        const std::size_t local = gather(a, region.local_to_global);
        global[a] *= psi_r[local];
    }
}

}  // namespace menos::runtime
