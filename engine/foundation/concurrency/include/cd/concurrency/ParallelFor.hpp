// =============================================================================
// CHROMODYNAMIC — cd/concurrency/ParallelFor.hpp
// Phase 53.B / Wave 221 — header-only parallel_for via std::thread.
//
// Divides `[begin, end)` into `worker_count` contiguous chunks, spawns
// that many threads, each running `fn(i)` for its slice. Joins all
// threads before returning.
//
// Trade-offs vs ThreadPool:
//   * Pro: zero setup — fits in a header, no library state.
//   * Con: thread creation cost amortized only across the loop body,
//          so per-call overhead is high. Use ThreadPool for hot loops.
//
// Use for occasional bulk work (asset cooking, batch import, content
// validation) that doesn't justify keeping a pool alive.
// =============================================================================
#pragma once

#include <algorithm>
#include <cd/core/Defines.hpp>

#include <algorithm>
#include <cstddef>
#include <thread>
#include <vector>

namespace cd::concurrency
{

template <class F>
inline void parallel_for(std::size_t begin, std::size_t end, F fn,
                         std::size_t worker_count = 0)
{
    if (begin >= end) return;
    if (worker_count == 0)
        worker_count = std::max<std::size_t>(1u, std::thread::hardware_concurrency());

    const std::size_t total = end - begin;
    worker_count = std::min(worker_count, total);
    const std::size_t chunk = (total + worker_count - 1) / worker_count;

    std::vector<std::thread> threads;
    threads.reserve(worker_count);

    for (std::size_t w = 0; w < worker_count; ++w)
    {
        const std::size_t lo = begin + w * chunk;
        const std::size_t hi = std::min(lo + chunk, end);
        if (lo >= hi) break;
        threads.emplace_back([lo, hi, &fn]
        {
            for (std::size_t i = lo; i < hi; ++i) fn(i);
        });
    }
    for (auto& t : threads) t.join();
}

}  // namespace cd::concurrency
