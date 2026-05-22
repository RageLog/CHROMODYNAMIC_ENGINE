// =============================================================================
// CHROMODYNAMIC — cd/bench/Benchmark.hpp
//
// Header-only microbenchmark utility. Keeps the surface tiny on purpose:
// the goal is "fast feedback on hot paths" not "google-benchmark clone".
//
// Usage:
//   auto r = cd::bench::run("ecs query", [&]() {
//       for (auto e : world.query<Pos, Vel>()) { ... }
//   });
//   r.print(std::cout);
//
// Statistics:
//   * Warmup: 3 iterations discarded (warm caches, instruction decode).
//   * Sampling: runs the body until either Config::min_samples is reached
//     OR Config::min_time_ms wallclock has elapsed (whichever comes
//     later). For very fast bodies the loop calls the body N times per
//     sample, N chosen so each sample is at least 1µs (otherwise
//     steady_clock resolution dominates).
//   * Reports: mean, median, min, p99, total wallclock.
//   * Stable across runs: report contains zero RNG; mean / median are
//     deterministic on identical timing — useful when the user does
//     A/B comparisons.
//
// Anti-flakiness:
//   * Caller is expected to keep results in a `volatile` sink or with
//     do_not_optimize_(...) below so the compiler doesn't constant-fold
//     the body. The helper is the standard "barrier" trick that
//     google-benchmark uses, sized for both MSVC and Clang/GCC.
// =============================================================================
#pragma once

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <iosfwd>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

namespace cd::bench
{

struct Config
{
    /// Number of warmup iterations whose timings are discarded.
    std::uint32_t warmup_iterations { 3 };
    /// Minimum number of timed samples collected.
    std::uint32_t min_samples { 32 };
    /// Minimum total wallclock spent sampling (whichever ends later).
    std::uint32_t min_time_ms { 50 };
    /// Optional label printed alongside the name.
    std::string label;
};

struct Report
{
    std::string name;
    std::string label;
    std::uint64_t samples { 0 };
    /// One sample is `inner_calls` body invocations. The stats below are
    /// PER-CALL nanoseconds, so a higher inner_calls means more averaging
    /// per sample.
    std::uint32_t inner_calls { 1 };
    double mean_ns_per_op { 0.0 };
    double median_ns_per_op { 0.0 };
    double min_ns_per_op { 0.0 };
    double max_ns_per_op { 0.0 };
    /// Sample-population standard deviation in ns/op. Useful as a
    /// "noise floor" indicator — a benchmark with stddev > 0.5*mean is
    /// not telling you much yet.
    double stddev_ns_per_op { 0.0 };
    double p90_ns_per_op { 0.0 };
    double p95_ns_per_op { 0.0 };
    double p99_ns_per_op { 0.0 };
    double total_seconds { 0.0 };

    void print(std::ostream& out) const
    {
        char buf[256];
        std::snprintf(
            buf,
            sizeof(buf),
            "[bench] %-32s mean=%10.1f ns/op  med=%10.1f  min=%10.1f  p99=%10.1f  N=%llu*%u  wall=%.3fs\n",
            name.c_str(),
            mean_ns_per_op,
            median_ns_per_op,
            min_ns_per_op,
            p99_ns_per_op,
            static_cast<unsigned long long>(samples),
            inner_calls,
            total_seconds
        );
        out << buf;
    }

    /// Single-line CSV: name,label,samples,inner_calls,mean,median,min,p99,wall_s
    [[nodiscard]] std::string to_csv() const
    {
        char buf[256];
        std::snprintf(
            buf,
            sizeof(buf),
            "%s,%s,%llu,%u,%.3f,%.3f,%.3f,%.3f,%.6f",
            name.c_str(),
            label.c_str(),
            static_cast<unsigned long long>(samples),
            inner_calls,
            mean_ns_per_op,
            median_ns_per_op,
            min_ns_per_op,
            p99_ns_per_op,
            total_seconds
        );
        return buf;
    }

    /// One Markdown table row matching `markdown_header()`. Pair them
    /// when emitting a report from a multi-bench harness.
    [[nodiscard]] std::string to_markdown_row() const
    {
        char buf[512];
        std::snprintf(
            buf,
            sizeof(buf),
            "| %s | %llu | %.1f | %.1f | %.1f | %.1f | %.1f | %.1f |",
            name.c_str(),
            static_cast<unsigned long long>(samples),
            min_ns_per_op,
            mean_ns_per_op,
            median_ns_per_op,
            p90_ns_per_op,
            p99_ns_per_op,
            stddev_ns_per_op
        );
        return buf;
    }
};

/// Two-line Markdown table header. Concatenate with `Report::to_markdown_row()`
/// snippets for a complete table:
///
///   std::ostringstream o;
///   o << cd::bench::markdown_header() << '\n';
///   for (auto& r : reports) o << r.to_markdown_row() << '\n';
[[nodiscard]] inline std::string_view markdown_header() noexcept
{
    return "| Bench | n | min (ns/op) | mean | median | p90 | p99 | stddev |\n"
           "|---|---:|---:|---:|---:|---:|---:|---:|";
}

/// Optimization barrier — see google/benchmark::DoNotOptimize. Tells the
/// compiler that `value`'s address has been "taken" by inline asm so it
/// must be materialised in memory; on Clang/GCC `"r,m"` lets the value
/// stay in a register or memory. On MSVC there is no equivalent, so we
/// fall back to a volatile atomic xor that the optimizer can't eliminate.
template <class T>
inline void do_not_optimize(const T& value) noexcept
{
#if defined(__clang__) || defined(__GNUC__)
    asm volatile("" : : "r,m"(value) : "memory");
#else
    static volatile std::atomic<std::uintptr_t> sink { 0 };
    sink.store(reinterpret_cast<std::uintptr_t>(&value), std::memory_order_relaxed);
#endif
}

/// Run `body` per the policy in `cfg`, return statistics.
///
/// Body must be re-entrant (called many times with no setup/teardown).
/// For benchmarks that need per-iteration setup, wrap the body in a
/// lambda that performs both setup and the measured work — the setup
/// cost is averaged into the per-call timing.
template <class Body>
[[nodiscard]] Report run(std::string_view name, Body&& body, const Config& cfg = Config {})
{
    using Clock = std::chrono::steady_clock;
    using Ns = std::chrono::nanoseconds;

    Report rep;
    rep.name = std::string { name };
    rep.label = cfg.label;

    // --- Warmup ----------------------------------------------------------
    for (std::uint32_t i = 0; i < cfg.warmup_iterations; ++i)
        body();

    // --- Calibrate inner_calls so each sample is at least 1us -----------
    std::uint32_t inner = 1;
    {
        const auto t0 = Clock::now();
        for (std::uint32_t i = 0; i < inner; ++i)
            body();
        auto elapsed_ns = std::chrono::duration_cast<Ns>(Clock::now() - t0).count();
        while (elapsed_ns < 1000 && inner < (1u << 24))
        {
            inner *= 2;
            const auto t1 = Clock::now();
            for (std::uint32_t i = 0; i < inner; ++i)
                body();
            elapsed_ns = std::chrono::duration_cast<Ns>(Clock::now() - t1).count();
        }
    }
    rep.inner_calls = inner;

    // --- Sample collection ----------------------------------------------
    std::vector<double> per_call_ns;
    per_call_ns.reserve(static_cast<std::size_t>(cfg.min_samples) * 2);
    const auto deadline = Clock::now() + std::chrono::milliseconds { cfg.min_time_ms };
    const auto t_start = Clock::now();

    while (per_call_ns.size() < cfg.min_samples || Clock::now() < deadline)
    {
        const auto a = Clock::now();
        for (std::uint32_t i = 0; i < inner; ++i)
            body();
        const auto b = Clock::now();
        const double ns = static_cast<double>(std::chrono::duration_cast<Ns>(b - a).count());
        per_call_ns.push_back(ns / static_cast<double>(inner));
    }
    const auto t_end = Clock::now();
    rep.total_seconds = std::chrono::duration<double>(t_end - t_start).count();
    rep.samples = per_call_ns.size();

    // --- Statistics -----------------------------------------------------
    if (!per_call_ns.empty())
    {
        std::vector<double> sorted = per_call_ns;
        std::sort(sorted.begin(), sorted.end());
        rep.min_ns_per_op = sorted.front();
        rep.max_ns_per_op = sorted.back();
        // Median: middle element for odd, mean of middle two for even.
        const auto n = sorted.size();
        if ((n & 1u) != 0u)
            rep.median_ns_per_op = sorted[n / 2];
        else
            rep.median_ns_per_op = (sorted[n / 2 - 1] + sorted[n / 2]) * 0.5;
        const auto pick = [&](double q) {
            auto idx = static_cast<std::size_t>(static_cast<double>(n) * q);
            if (idx >= n)
                idx = n - 1;
            return sorted[idx];
        };
        rep.p90_ns_per_op = pick(0.90);
        rep.p95_ns_per_op = pick(0.95);
        rep.p99_ns_per_op = pick(0.99);
        // Mean over the unsorted list (identical sum either way).
        double sum = 0.0;
        for (double v : per_call_ns)
            sum += v;
        rep.mean_ns_per_op = sum / static_cast<double>(n);
        // Sample-population standard deviation.
        double sq = 0.0;
        for (double v : per_call_ns)
            sq += (v - rep.mean_ns_per_op) * (v - rep.mean_ns_per_op);
        rep.stddev_ns_per_op = std::sqrt(sq / static_cast<double>(n));
    }

    return rep;
}

}  // namespace cd::bench
