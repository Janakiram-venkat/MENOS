#include "kernels/cpu/thread_pool.h"

#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <exception>
#include <mutex>
#include <thread>
#include <vector>

#include "common/thread_count.h"

namespace menos::kernels::cpu {
namespace {

// Set while this thread is running a chunk, so a body that itself calls
// parallel_for runs serially instead of re-entering the pool.
thread_local bool tls_inside_pool = false;

// Chunk t of [0, count) split into `threads` parts: the first count % threads
// chunks have one extra element. Depends only on (count, threads, t), and
// cannot overflow.
// count, team size and chunk index: three roles, one call site.
// NOLINTBEGIN(bugprone-easily-swappable-parameters)
std::pair<std::size_t, std::size_t> chunk_bounds(std::size_t count,
                                                 std::uint32_t threads,
                                                 std::uint32_t t) {
    // NOLINTEND(bugprone-easily-swappable-parameters)
    const std::size_t base = count / threads;
    const std::size_t extra = count % threads;
    const std::size_t begin = t * base + std::min<std::size_t>(t, extra);
    const std::size_t end = begin + base + (t < extra ? 1 : 0);
    return {begin, end};
}

class ThreadPool {
public:
    ThreadPool()
        : owner_pid_(::getpid()),
          num_workers_(common::default_thread_count() - 1),
          errors_(num_workers_ + 1) {
        workers_.reserve(num_workers_);
        for (std::uint32_t index = 1; index <= num_workers_; ++index) {
            workers_.emplace_back([this, index] { work(index); });
        }
    }

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;
    ThreadPool(ThreadPool&&) = delete;
    ThreadPool& operator=(ThreadPool&&) = delete;
    // Never destroyed: see pool().
    ~ThreadPool() = default;

    // Runs the job on min(threads, workers + 1) chunks and returns true, or
    // returns false without running anything if the pool cannot be used here.
    // Mirrors parallel_for's documented (count, threads, function, context) order.
    // NOLINTBEGIN(bugprone-easily-swappable-parameters)
    bool try_run(std::size_t count, std::uint32_t threads, ChunkFunction function, void* context) {
        // NOLINTEND(bugprone-easily-swappable-parameters)
        if (::getpid() != owner_pid_) {
            return false;  // a forked child: the workers do not exist in it
        }
        const std::unique_lock<std::mutex> lock(mutex_, std::try_to_lock);
        if (!lock.owns_lock()) {
            return false;  // another thread is using the pool
        }
        const std::uint32_t team = std::min(threads, num_workers_ + 1);
        count_ = count;
        function_ = function;
        context_ = context;
        std::fill(errors_.begin(), errors_.end(), nullptr);
        remaining_.store(team - 1, std::memory_order_relaxed);

        // One word carries both the job's generation and its team size, and one
        // notify_all wakes every worker with a single system call. A worker reads
        // the team size from the word it woke on, so a worker outside the team
        // never reads the job fields above; the workers inside it are exactly the
        // ones the caller waits for, so those fields are not rewritten until
        // they have finished.
        ++generation_;
        job_word_.store((generation_ << kTeamBits) | team, std::memory_order_release);
        job_word_.notify_all();

        run_chunk(0, team);
        for (std::uint32_t left = remaining_.load(std::memory_order_acquire); left != 0;
             left = remaining_.load(std::memory_order_acquire)) {
            remaining_.wait(left, std::memory_order_acquire);
        }
        for (const std::exception_ptr& error : errors_) {
            if (error) {
                std::rethrow_exception(error);
            }
        }
        return true;
    }

private:
    // The low kTeamBits bits of job_word_ are the team size, the rest the
    // generation. A team never exceeds the hardware thread count.
    static constexpr unsigned kTeamBits = 16;

    // Workers never spin: they sleep in the atomic wait until a job arrives
    // (docs/adr/0019 section 3.1, why OpenMP's spinning default was rejected).
    void work(std::uint32_t index) {
        std::uint64_t seen = 0;
        for (;;) {
            job_word_.wait(seen, std::memory_order_acquire);
            seen = job_word_.load(std::memory_order_acquire);
            const auto team = static_cast<std::uint32_t>(seen & ((1U << kTeamBits) - 1));
            if (index >= team) {
                continue;  // not in this job's team
            }
            run_chunk(index, team);
            if (remaining_.fetch_sub(1, std::memory_order_acq_rel) == 1) {
                remaining_.notify_one();
            }
        }
    }

    void run_chunk(std::uint32_t t, std::uint32_t team) {
        const auto [begin, end] = chunk_bounds(count_, team, t);
        tls_inside_pool = true;
        try {
            function_(context_, begin, end);
        } catch (...) {
            errors_[t] = std::current_exception();
        }
        tls_inside_pool = false;
    }

    const pid_t owner_pid_;
    const std::uint32_t num_workers_;
    std::vector<std::exception_ptr> errors_;
    std::vector<std::thread> workers_;
    std::mutex mutex_;
    std::uint64_t generation_ = 0;  // under mutex_
    std::atomic<std::uint64_t> job_word_{0};

    // The current job. Written by the caller under mutex_ before job_word_ is
    // published, read only by that job's team, unchanged until they finish.
    std::size_t count_ = 0;
    ChunkFunction function_ = nullptr;
    void* context_ = nullptr;
    std::atomic<std::uint32_t> remaining_{0};
};

// Created on first use and deliberately never destroyed: its workers block in
// an atomic wait for the life of the process, and a destructor that joined them
// would have to run during static destruction (after Python has started tearing
// down, for the extension module) or in a forked child that has no workers to
// join. The pointer stays reachable, so it is not a leak to LeakSanitizer.
ThreadPool& pool() {
    static ThreadPool* const kInstance = new ThreadPool();
    return *kInstance;
}

}  // namespace

void parallel_for(std::size_t count, std::uint32_t threads, ChunkFunction function, void* context) {
    // Never more chunks than elements, so no chunk is empty.
    const auto team = static_cast<std::uint32_t>(std::min<std::size_t>(threads, count));
    if (team > 1 && !tls_inside_pool && pool().try_run(count, team, function, context)) {
        return;
    }
    function(context, 0, count);
}

}  // namespace menos::kernels::cpu
