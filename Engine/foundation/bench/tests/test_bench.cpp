// =============================================================================
// CHROMODYNAMIC — test_bench.cpp
// Unit tests for cd::bench::run/Report semantics.
// =============================================================================
#include <cd/bench/Benchmark.hpp>

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <sstream>
#include <thread>

namespace
{

// A body that does measurable but small work. Volatile prevents the
// compiler from constant-folding the loop down to nothing under -O3.
volatile std::uint64_t g_sink = 0;
void cheap_body()
{
    std::uint64_t acc = 0;
    for (std::uint64_t i = 0; i < 32; ++i)
        acc += i * i;
    g_sink = acc;
}

}  // namespace

TEST(BenchTest, RunProducesNonZeroStats)
{
    cd::bench::Config cfg;
    cfg.min_samples = 16;
    cfg.min_time_ms = 5;
    auto r = cd::bench::run("cheap", cheap_body, cfg);

    EXPECT_GE(r.samples, cfg.min_samples);
    EXPECT_GT(r.mean_ns_per_op, 0.0);
    EXPECT_GT(r.median_ns_per_op, 0.0);
    EXPECT_GT(r.min_ns_per_op, 0.0);
    EXPECT_GT(r.p99_ns_per_op, 0.0);
    EXPECT_GE(r.inner_calls, 1u);
    EXPECT_GT(r.total_seconds, 0.0);
}

TEST(BenchTest, MinIsLeOrEqMedianLeOrEqP99)
{
    cd::bench::Config cfg;
    cfg.min_samples = 64;
    cfg.min_time_ms = 10;
    auto r = cd::bench::run("ordering", cheap_body, cfg);

    EXPECT_LE(r.min_ns_per_op, r.median_ns_per_op);
    EXPECT_LE(r.median_ns_per_op, r.p99_ns_per_op);
    // Mean lives within [min, max]; max ≥ p99 by construction.
    EXPECT_LE(r.min_ns_per_op, r.mean_ns_per_op);
    EXPECT_LE(r.mean_ns_per_op, r.p99_ns_per_op + 1.0);  // tiny slack for rounding
}

TEST(BenchTest, NoopBodyHasInnerCallsScaledUp)
{
    // A truly empty body must inflate inner_calls past 1 to escape clock noise.
    cd::bench::Config cfg;
    cfg.min_samples = 16;
    cfg.min_time_ms = 5;
    auto r = cd::bench::run("noop", [] { /* nothing */ }, cfg);
    EXPECT_GT(r.inner_calls, 1u);
}

TEST(BenchTest, ReportPrintsHumanReadable)
{
    cd::bench::Config cfg;
    cfg.min_samples = 8;
    cfg.min_time_ms = 2;
    auto r = cd::bench::run("printable", cheap_body, cfg);

    std::ostringstream oss;
    r.print(oss);
    const auto s = oss.str();
    EXPECT_NE(s.find("[bench]"), std::string::npos);
    EXPECT_NE(s.find("printable"), std::string::npos);
    EXPECT_NE(s.find("ns/op"), std::string::npos);
}

TEST(BenchTest, CsvHasNineFields)
{
    cd::bench::Config cfg;
    cfg.min_samples = 8;
    cfg.min_time_ms = 2;
    cfg.label = "tier1";
    auto r = cd::bench::run("csv", cheap_body, cfg);

    const auto csv = r.to_csv();
    // Count commas: 8 commas → 9 fields.
    std::size_t commas = 0;
    for (char c : csv)
    {
        if (c == ',')
            ++commas;
    }
    EXPECT_EQ(commas, 8u);
    EXPECT_NE(csv.find("csv,"), std::string::npos);
    EXPECT_NE(csv.find("tier1,"), std::string::npos);
}

TEST(BenchTest, MeasurableWorkScalesWithIterationCount)
{
    // A body that sleeps for ~100 µs. Its measured ns/op should be at
    // least 50 µs (giving slack for thread scheduling jitter). This is
    // really a sanity check that the timer machinery isn't dropping the
    // measured time on the floor.
    auto sleepy = [] {
        std::this_thread::sleep_for(std::chrono::microseconds { 100 });
    };
    cd::bench::Config cfg;
    cfg.min_samples = 4;
    cfg.min_time_ms = 2;
    auto r = cd::bench::run("sleepy", sleepy, cfg);
    EXPECT_GT(r.mean_ns_per_op, 50'000.0);
}
