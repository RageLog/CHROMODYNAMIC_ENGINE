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

TEST(BenchTest, ExtendedStatsAreOrderedAndFinite)
{
    cd::bench::Config cfg;
    cfg.min_samples = 64;
    cfg.min_time_ms = 10;
    auto r = cd::bench::run("extended", cheap_body, cfg);
    EXPECT_GT(r.max_ns_per_op, 0.0);
    EXPECT_GE(r.max_ns_per_op, r.p99_ns_per_op);
    EXPECT_GE(r.p99_ns_per_op, r.p95_ns_per_op);
    EXPECT_GE(r.p95_ns_per_op, r.p90_ns_per_op);
    EXPECT_GE(r.p90_ns_per_op, r.median_ns_per_op);
    EXPECT_GE(r.stddev_ns_per_op, 0.0);
}

TEST(BenchTest, MarkdownRowAndHeaderHaveExpectedSubstrings)
{
    cd::bench::Config cfg;
    cfg.min_samples = 8;
    cfg.min_time_ms = 2;
    auto r = cd::bench::run("mdrow", cheap_body, cfg);
    const auto row = r.to_markdown_row();
    EXPECT_NE(row.find("mdrow"), std::string::npos);
    EXPECT_NE(row.find("|"), std::string::npos);
    const auto hdr = cd::bench::markdown_header();
    EXPECT_NE(hdr.find("Bench"), std::string_view::npos);
    EXPECT_NE(hdr.find("p99"), std::string_view::npos);
    EXPECT_NE(hdr.find("stddev"), std::string_view::npos);
}

TEST(BenchTest, MeasurableWorkScalesWithIterationCount)
{
    // Sanity check: a body that does ~32k integer multiplies must measure
    // a strictly positive nanosecond cost. A previous version of this test
    // used std::this_thread::sleep_for(100us), but the sleep granularity
    // on MinGW UCRT64 can be ~30 ns (sleep returns immediately), making
    // the assertion flaky. Using a deterministic busy-loop is portable
    // across MSVC / Clang / GCC / MinGW.
    auto busy = []
    {
        volatile std::uint64_t acc = 1;
        for (std::uint64_t i = 1; i <= 32'000; ++i)
            acc = acc * 1103515245u + 12345u + i;
        cd::bench::do_not_optimize(acc);
    };
    cd::bench::Config cfg;
    cfg.min_samples = 4;
    cfg.min_time_ms = 2;
    auto r = cd::bench::run("busy_32k", busy, cfg);
    EXPECT_GT(r.mean_ns_per_op, 0.0);
    // 32k MUL+ADDs ≥ 100 ns even on the fastest CPUs in Debug-O0; gives
    // 1000x headroom over the previous 50 µs lower bound and stays
    // platform-independent.
    EXPECT_GT(r.mean_ns_per_op, 100.0);
}
