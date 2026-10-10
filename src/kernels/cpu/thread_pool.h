#pragma once

#include <cstddef>
#include <cstdint>

namespace menos::kernels::cpu {

// The CPU kernels' thread pool (Phase 15, docs/adr/0019 section 3.1).
//
// MENOS owns its pool rather than using OpenMP because the waiting policy is a
// correctness-of-performance property it must control. OpenMP's default lets
// idle workers spin between parallel regions, and on the gate machine (22
// virtual CPUs under WSL) that stalled every gate for 5-30 ms while the host
// descheduled spinning vCPUs; only the OMP_WAIT_POLICY environment variable,
// read once when the runtime loads, could change it (measured, section 10).
// These workers block on a C++20 atomic wait (a futex on Linux) and use no
// CPU while idle.
//
// parallel_for splits [0, count) into `threads` contiguous chunks,
//   chunk t = [count * t / threads, count * (t + 1) / threads),
// runs chunk 0 on the calling thread and chunk t on worker t, and returns
// when all have finished. The split depends only on count and threads, and
// each index is processed by exactly one chunk, so a body whose iterations are
// independent produces the same result for every thread count.
//
// Falls back to running the whole range on the calling thread (same result,
// less speed) when threads <= 1, when another thread is already using the
// pool, or in a process forked from the one that started the pool. An
// exception thrown by a chunk is rethrown on the calling thread after every
// chunk has finished; if several throw, the lowest-numbered chunk's is kept.
using ChunkFunction = void (*)(void* context, std::size_t begin, std::size_t end);

void parallel_for(std::size_t count, std::uint32_t threads, ChunkFunction function, void* context);

// Convenience wrapper for a callable `body(std::size_t begin, std::size_t end)`.
template <typename Body>
void parallel_for(std::size_t count, std::uint32_t threads, Body& body) {
    parallel_for(
        count,
        threads,
        [](void* context, std::size_t begin, std::size_t end) {
            (*static_cast<Body*>(context))(begin, end);
        },
        &body);
}

}  // namespace menos::kernels::cpu
