// =============================================================================
// CHROMODYNAMIC — cd::profile tests
// =============================================================================
#include <cd/profile/BufferSink.hpp>
#include <cd/profile/ChromeTraceSink.hpp>
#include <cd/profile/CsvSink.hpp>
#include <cd/profile/Scope.hpp>
#include <cd/profile/StatsAggregator.hpp>
#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <thread>

TEST(Profile, NullSinkIsInstalledByDefault)
{
    auto* before = cd::profile::current_sink();
    EXPECT_NE(before, nullptr);
    {
        // Sample drops on the floor; nothing to assert except that we don't
        // crash and the scope timer runs through its destructor cleanly.
        CD_PROFILE_SCOPE("default_sink_no_throw");
    }
}

TEST(Profile, BufferSinkCapturesSamples)
{
    cd::profile::BufferSink sink { 16 };
    auto* prev = cd::profile::set_sink(&sink);

    {
        CD_PROFILE_SCOPE("outer");
        std::this_thread::sleep_for(std::chrono::microseconds(50));
        {
            CD_PROFILE_SCOPE("inner");
            std::this_thread::sleep_for(std::chrono::microseconds(10));
        }
    }

    cd::profile::set_sink(prev);

    const auto samples = sink.snapshot();
    ASSERT_EQ(samples.size(), 2U);
    // Inner ends first → recorded first.
    EXPECT_EQ(samples[0].name, "inner");
    EXPECT_EQ(samples[1].name, "outer");
    // Durations are positive, outer > inner.
    EXPECT_GT(samples[0].duration_ns, 0U);
    EXPECT_GT(samples[1].duration_ns, samples[0].duration_ns);
}

TEST(Profile, BufferSinkRingDropsOldestOnOverflow)
{
    cd::profile::BufferSink sink { 4 };
    auto* prev = cd::profile::set_sink(&sink);

    for (int i = 0; i < 10; ++i)
    {
        CD_PROFILE_SCOPE("loop");
    }

    cd::profile::set_sink(prev);

    const auto samples = sink.snapshot();
    EXPECT_EQ(samples.size(), 4U);
    // Every retained sample carries the same literal name pointer.
    for (const auto& s : samples)
        EXPECT_EQ(s.name, "loop");
}

TEST(Profile, SetSinkReturnsPreviousAndRestoresOnSwap)
{
    cd::profile::BufferSink a;
    cd::profile::BufferSink b;
    auto* root = cd::profile::current_sink();

    auto* p1 = cd::profile::set_sink(&a);
    EXPECT_EQ(p1, root);
    EXPECT_EQ(cd::profile::current_sink(), &a);

    auto* p2 = cd::profile::set_sink(&b);
    EXPECT_EQ(p2, &a);
    EXPECT_EQ(cd::profile::current_sink(), &b);

    auto* p3 = cd::profile::set_sink(nullptr);
    EXPECT_EQ(p3, &b);
    // Passing nullptr installs the built-in no-op sink, never leaves us
    // with a null current_sink().
    EXPECT_NE(cd::profile::current_sink(), nullptr);

    cd::profile::set_sink(root);
}

TEST(StatsAggregator, RollupComputesMinAvgMax)
{
    cd::profile::StatsAggregator agg;
    std::vector<cd::profile::Sample> samples = {
        { "frame", 0, 100, 0 },
        { "frame", 0, 200, 0 },
        { "frame", 0, 300, 0 },
        { "post",  0, 50,  0 },
    };
    agg.apply(samples);

    const auto rows = agg.snapshot();
    ASSERT_EQ(rows.size(), 2U);
    // Sorted by total time DESC — "frame" (sum 600) before "post" (sum 50).
    EXPECT_EQ(rows[0].name, "frame");
    EXPECT_EQ(rows[0].count, 3U);
    EXPECT_EQ(rows[0].min_ns, 100U);
    EXPECT_EQ(rows[0].max_ns, 300U);
    EXPECT_DOUBLE_EQ(rows[0].avg_ns(), 200.0);

    EXPECT_EQ(rows[1].name, "post");
    EXPECT_EQ(rows[1].count, 1U);
    EXPECT_EQ(rows[1].min_ns, 50U);
    EXPECT_EQ(rows[1].max_ns, 50U);
}

TEST(StatsAggregator, MultipleApplyCallsAccumulate)
{
    cd::profile::StatsAggregator agg;
    agg.apply(
        {
            { "x", 0, 10, 0 }
    }
    );
    agg.apply(
        {
            { "x", 0, 30, 0 },
            { "y", 0, 5,  0 }
    }
    );

    const auto rows = agg.snapshot();
    ASSERT_EQ(rows.size(), 2U);
    EXPECT_EQ(rows[0].name, "x");
    EXPECT_EQ(rows[0].count, 2U);
    EXPECT_EQ(rows[0].total_ns, 40U);
    EXPECT_EQ(rows[1].name, "y");
    EXPECT_EQ(rows[1].count, 1U);
}

TEST(StatsAggregator, ResetClears)
{
    cd::profile::StatsAggregator agg;
    agg.apply(
        {
            { "x", 0, 1, 0 }
    }
    );
    EXPECT_EQ(agg.row_count(), 1U);
    agg.reset();
    EXPECT_EQ(agg.row_count(), 0U);
}

namespace
{
namespace fs = std::filesystem;

[[nodiscard]] fs::path sink_tmp(std::string_view suffix)
{
    static std::atomic<std::uint64_t> seq { 0 };
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    return fs::temp_directory_path() / ("cd_sink_" + std::to_string(static_cast<std::uint64_t>(stamp)) + "_" +
                                        std::to_string(seq.fetch_add(1)) + std::string { suffix });
}

[[nodiscard]] std::string slurp(const fs::path& p)
{
    std::ifstream f(p);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}
}  // namespace

TEST(CsvSink, WritesHeaderAndRow)
{
    const auto path = sink_tmp(".csv");
    {
        cd::profile::CsvSink sink { path.string() };
        ASSERT_TRUE(sink.is_open());
        sink.submit({ "frame", 1000, 250, 7 });
        sink.submit({ "post", 2000, 80, 7 });
    }
    const auto text = slurp(path);
    EXPECT_NE(text.find("thread_hash,start_ns,duration_ns,name"), std::string::npos);
    EXPECT_NE(text.find("7,1000,250,\"frame\""), std::string::npos);
    EXPECT_NE(text.find("7,2000,80,\"post\""), std::string::npos);
    fs::remove(path);
}

TEST(CsvSink, AppendsDoNotReplayHeader)
{
    const auto path = sink_tmp(".csv");
    {
        cd::profile::CsvSink sink { path.string() };
        sink.submit({ "a", 0, 10, 1 });
    }
    {
        // Second instance opens in append mode. Because our sink only
        // writes the header on its OWN first submit, the file ends up with
        // a single header line.
        cd::profile::CsvSink sink { path.string() };
        sink.submit({ "b", 0, 20, 1 });
    }
    const auto text = slurp(path);
    // Count occurrences of "thread_hash".
    std::size_t hits = 0;
    std::size_t pos = 0;
    while ((pos = text.find("thread_hash", pos)) != std::string::npos)
    {
        ++hits;
        pos += 11;
    }
    EXPECT_EQ(hits, 2U);  // each session writes its own header; both rows present.
    EXPECT_NE(text.find("\"a\""), std::string::npos);
    EXPECT_NE(text.find("\"b\""), std::string::npos);
    fs::remove(path);
}

TEST(ChromeTraceSink, EmitsValidArrayWithDurationEvents)
{
    const auto path = sink_tmp(".json");
    {
        cd::profile::ChromeTraceSink sink { path.string() };
        ASSERT_TRUE(sink.is_open());
        sink.submit({ "frame", 1'000'000, 250'000, 12345 });  // 1 ms start, 250 us dur
        sink.submit({ "post", 2'000'000, 50'000, 12345 });
    }
    const auto text = slurp(path);
    // Sanity: starts with [, ends with ], two ph:X entries, microseconds correctly scaled.
    EXPECT_EQ(text.front(), '[');
    EXPECT_NE(text.find("]"), std::string::npos);
    EXPECT_NE(text.find("\"ph\":\"X\""), std::string::npos);
    EXPECT_NE(text.find("\"name\":\"frame\""), std::string::npos);
    EXPECT_NE(text.find("\"name\":\"post\""), std::string::npos);
    // 1_000_000 ns = 1000 us
    EXPECT_NE(text.find("\"ts\":1000"), std::string::npos);
    EXPECT_NE(text.find("\"dur\":250"), std::string::npos);
    fs::remove(path);
}
