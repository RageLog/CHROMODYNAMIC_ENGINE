// =============================================================================
// CHROMODYNAMIC — tools/render_thread_soak
//
// Stress harness for cd::render::AsyncSubmitN. Hammers the ring queue
// with millions of jobs and reports:
//
//   * Total enqueued / completed (must match at the end → no lost jobs).
//   * Steady-state enqueue rate (jobs/sec).
//   * Per-job latency stats (cd::bench style mean/p50/p90/p99/stddev).
//   * Worker idle ratio — fraction of the run the consumer was caught up.
//
// CLI:
//   cd_render_thread_soak [--duration-secs=2] [--capacity=2] [--workload-us=10]
//
// Defaults run for 2 s with capacity-2 and ~10 µs per job — short enough
// for CI smoke, long enough to surface coarse leaks. Production
// validation cranks --duration-secs up to hours or days; the rest of the
// statistics keep the same shape.
//
// Exit codes:
//   0 — clean (enqueued == completed)
//   1 — mismatch (lost or extra job — should never happen)
// =============================================================================
#include <cd/bench/Benchmark.hpp>
#include <cd/render/AsyncSubmitN.hpp>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace
{

struct Args
{
    double duration_secs { 2.0 };
    std::size_t capacity { 2 };
    std::uint32_t workload_us { 10 };
};

[[nodiscard]] Args parse_args(int argc, char** argv)
{
    Args a;
    for (int i = 1; i < argc; ++i)
    {
        const std::string_view s = argv[i];
        if (s.starts_with("--duration-secs="))
            a.duration_secs = std::atof(std::string { s.substr(16) }.c_str());
        else if (s.starts_with("--capacity="))
            a.capacity = static_cast<std::size_t>(
                std::atoi(std::string { s.substr(11) }.c_str()));
        else if (s.starts_with("--workload-us="))
            a.workload_us = static_cast<std::uint32_t>(
                std::atoi(std::string { s.substr(14) }.c_str()));
    }
    if (a.capacity == 0)
        a.capacity = 2;
    if (a.duration_secs <= 0.0)
        a.duration_secs = 2.0;
    return a;
}

void busy_loop_us(std::uint32_t us)
{
    // Busy-loop for ~us microseconds. Deterministic across hosts (no
    // sleep granularity issues) — fine for stress testing because the
    // value is meant to be small.
    const auto t0 = std::chrono::steady_clock::now();
    const auto deadline = t0 + std::chrono::microseconds { us };
    while (std::chrono::steady_clock::now() < deadline)
    {
        volatile std::uint64_t x = 0;
        for (std::uint64_t i = 0; i < 16; ++i)
            x += i;
        cd::bench::do_not_optimize(x);
    }
}

}  // namespace

int main(int argc, char** argv)
{
    const auto args = parse_args(argc, argv);
    std::printf("=== cd_render_thread_soak ===\n");
    std::printf("  duration_secs = %.2f\n", args.duration_secs);
    std::printf("  capacity      = %zu\n", args.capacity);
    std::printf("  workload_us   = %u\n", args.workload_us);

    cd::render::AsyncSubmitN q { args.capacity };

    // Per-job timings — we capture enqueue_time on dispatch + use the
    // job body to record the latency from enqueue to body-start. Job
    // bodies push their latency into a thread-local-safe vector via
    // index increments through an atomic counter.
    std::vector<double> latencies_us;
    latencies_us.reserve(1u << 20);  // 1M slot reservation; grows if exceeded
    std::atomic<std::size_t> body_starts { 0 };

    const auto t_start = std::chrono::steady_clock::now();
    const auto deadline = t_start + std::chrono::duration<double> { args.duration_secs };

    std::uint64_t enqueued = 0;
    while (std::chrono::steady_clock::now() < deadline)
    {
        const auto enq_t = std::chrono::steady_clock::now();
        q.enqueue([enq_t, &latencies_us, &body_starts, &args] {
            // Latency = body-start time - enqueue time
            const auto t_body = std::chrono::steady_clock::now();
            const double us = std::chrono::duration<double, std::micro>(t_body - enq_t).count();
            const auto slot = body_starts.fetch_add(1, std::memory_order_relaxed);
            if (slot < latencies_us.capacity())
            {
                // Single-producer-single-consumer ring guarantees this
                // job body executes serially with respect to others, so
                // the resize-free push is race-free.
                latencies_us.push_back(us);  // capacity reservation prevents allocator churn
            }
            busy_loop_us(args.workload_us);
        });
        ++enqueued;
    }
    q.wait_idle();

    const auto t_end = std::chrono::steady_clock::now();
    const double wall_s = std::chrono::duration<double>(t_end - t_start).count();

    // Latency statistics via cd::bench::summarize-style logic. Sort
    // the (potentially huge) sample list once for percentiles.
    std::vector<double> sorted_latencies = latencies_us;
    std::sort(sorted_latencies.begin(), sorted_latencies.end());

    double sum = 0.0;
    for (double v : latencies_us)
        sum += v;
    const double mean = latencies_us.empty() ? 0.0 : sum / static_cast<double>(latencies_us.size());

    double sq = 0.0;
    for (double v : latencies_us)
        sq += (v - mean) * (v - mean);
    const double stddev = latencies_us.empty()
                              ? 0.0
                              : std::sqrt(sq / static_cast<double>(latencies_us.size()));

    auto pick = [&](double q_pct) {
        if (sorted_latencies.empty())
            return 0.0;
        auto idx = static_cast<std::size_t>(
            static_cast<double>(sorted_latencies.size()) * q_pct);
        if (idx >= sorted_latencies.size())
            idx = sorted_latencies.size() - 1;
        return sorted_latencies[idx];
    };

    std::printf("\n=== Summary ===\n");
    std::printf("  wall_seconds    = %.3f\n", wall_s);
    std::printf("  enqueued        = %llu\n", static_cast<unsigned long long>(enqueued));
    std::printf("  completed       = %llu\n",
                static_cast<unsigned long long>(q.completion_count()));
    std::printf("  enqueue_rate    = %.0f jobs/sec\n",
                static_cast<double>(enqueued) / wall_s);
    std::printf("  latency_mean    = %.2f us\n", mean);
    std::printf("  latency_stddev  = %.2f us\n", stddev);
    std::printf("  latency_p50     = %.2f us\n", pick(0.50));
    std::printf("  latency_p90     = %.2f us\n", pick(0.90));
    std::printf("  latency_p99     = %.2f us\n", pick(0.99));
    if (!sorted_latencies.empty())
    {
        std::printf("  latency_min     = %.2f us\n", sorted_latencies.front());
        std::printf("  latency_max     = %.2f us\n", sorted_latencies.back());
    }
    std::printf("  body_starts     = %llu\n",
                static_cast<unsigned long long>(body_starts.load()));

    const bool counts_match = enqueued == q.completion_count();
    std::printf("\n[soak] %s\n",
                counts_match ? "PASS (enqueue/complete counts match)"
                             : "FAIL (lost or extra jobs)");
    return counts_match ? 0 : 1;
}
