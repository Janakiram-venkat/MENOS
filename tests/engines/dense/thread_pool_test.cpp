// The CPU kernels' thread pool (Phase 15, docs/adr/0019 section 3.1): every
// index runs exactly once, in the documented static split, an exception from
// any chunk reaches the caller, a nested call cannot deadlock, and callers on
// different threads each get a complete result.

#include "kernels/cpu/thread_pool.h"

#include <gtest/gtest.h>

#include <atomic>
#include <cstdint>
#include <stdexcept>
#include <thread>
#include <vector>

namespace menos::kernels::cpu {
namespace {

TEST(ThreadPoolTest, EveryIndexRunsExactlyOnceForEveryTeamSize) {
    for (const std::size_t count : {1UL, 2UL, 7UL, 64UL, 1000UL, 100003UL}) {
        for (const std::uint32_t threads : {1U, 2U, 3U, 8U, 64U}) {
            std::vector<std::atomic<int>> hits(count);
            auto body = [&](std::size_t begin, std::size_t end) {
                for (std::size_t i = begin; i < end; ++i) {
                    hits[i].fetch_add(1, std::memory_order_relaxed);
                }
            };
            parallel_for(count, threads, body);
            for (std::size_t i = 0; i < count; ++i) {
                ASSERT_EQ(hits[i].load(), 1)
                    << "count=" << count << " threads=" << threads << " index=" << i;
            }
        }
    }
}

TEST(ThreadPoolTest, AnExceptionFromAnyChunkReachesTheCaller) {
    const std::size_t count = 1000;
    for (const std::size_t throwing_index : {0UL, 499UL, 999UL}) {
        auto body = [&](std::size_t begin, std::size_t end) {
            if (begin <= throwing_index && throwing_index < end) {
                throw std::runtime_error("chunk failed");
            }
        };
        EXPECT_THROW(parallel_for(count, 4, body), std::runtime_error) << throwing_index;
    }
    // The pool is still usable afterwards.
    std::atomic<std::size_t> total{0};
    auto sum = [&](std::size_t begin, std::size_t end) {
        total.fetch_add(end - begin, std::memory_order_relaxed);
    };
    parallel_for(count, 4, sum);
    EXPECT_EQ(total.load(), count);
}

TEST(ThreadPoolTest, ANestedCallRunsSeriallyInsteadOfDeadlocking) {
    std::atomic<std::size_t> inner_total{0};
    auto outer = [&](std::size_t begin, std::size_t end) {
        for (std::size_t i = begin; i < end; ++i) {
            auto inner = [&](std::size_t b, std::size_t e) {
                inner_total.fetch_add(e - b, std::memory_order_relaxed);
            };
            parallel_for(10, 4, inner);
        }
    };
    parallel_for(8, 4, outer);
    EXPECT_EQ(inner_total.load(), 80U);
}

TEST(ThreadPoolTest, ConcurrentCallersEachGetACompleteResult) {
    // One caller holds the pool; the other falls back to running serially.
    // Either way both ranges are covered exactly once.
    const std::size_t count = 200000;
    std::vector<std::atomic<int>> a(count);
    std::vector<std::atomic<int>> b(count);
    auto fill = [](std::vector<std::atomic<int>>& hits) {
        auto body = [&](std::size_t begin, std::size_t end) {
            for (std::size_t i = begin; i < end; ++i) {
                hits[i].fetch_add(1, std::memory_order_relaxed);
            }
        };
        for (int repeat = 0; repeat < 20; ++repeat) {
            parallel_for(hits.size(), 4, body);
        }
    };
    std::thread first([&] { fill(a); });
    std::thread second([&] { fill(b); });
    first.join();
    second.join();
    for (std::size_t i = 0; i < count; ++i) {
        ASSERT_EQ(a[i].load(), 20);
        ASSERT_EQ(b[i].load(), 20);
    }
}

}  // namespace
}  // namespace menos::kernels::cpu
