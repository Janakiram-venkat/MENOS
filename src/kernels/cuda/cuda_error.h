#pragma once

#include <cuda_runtime.h>

#include <string>

#include "common/error.h"

namespace menos::kernels::cuda {

// Translates a cudaError_t into a menos typed exception, so no caller above
// the kernels layer ever sees a CUDA error code (CLAUDE.md error handling: no
// error codes, typed exceptions only).
//
// A failed CUDA call is an InternalError rather than a ResourceExceeded: the
// one resource failure this codebase must report precisely -- not enough device
// memory -- is checked against the measured device budget BEFORE cudaMalloc is
// reached (DeviceBuffer), so a cudaError_t arriving here means an invariant
// was already violated, not that a budget was exceeded.
//
// `what` names the operation that failed, so the message says what failed and
// where, per CLAUDE.md.
inline void ensure_cuda(cudaError_t status, const char* what) {
    if (status != cudaSuccess) {
        throw common::InternalError(std::string("cuda: ") + what +
                                    " failed: " + cudaGetErrorString(status) + " (cudaError_t " +
                                    std::to_string(static_cast<int>(status)) + ")");
    }
}

}  // namespace menos::kernels::cuda
