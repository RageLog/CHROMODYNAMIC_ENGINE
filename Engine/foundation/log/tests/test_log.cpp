// =============================================================================
// CHROMODYNAMIC — cd::log tests (Sprint S2.2 + S2.4)
// =============================================================================
// JsonLogger takes a FILE*. We use std::fopen + std::filesystem::remove
// for the tmp files in the JsonLogger tests; MSVC CRT marks fopen as
// deprecated in favour of fopen_s but the test is the only consumer and
// the codepath is clearly bracketed.
#if defined(_MSC_VER)
#    define _CRT_SECURE_NO_WARNINGS 1
#endif

#include <cd/log/AuditTrail.hpp>
#include <cd/log/ConsoleLogger.hpp>
#include <cd/log/Format.hpp>
#include <cd/log/ILogger.hpp>
#include <cd/log/JsonLogger.hpp>
#include <cd/log/LogLevel.hpp>
#include <cd/log/LogRecord.hpp>
#include <cd/log/RingBufferSink.hpp>
#include <cd/log/Service.hpp>
#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <source_location>
#include <string>
#include <system_error>
#include <vector>

namespace
{

class RecordingObserver final : public cd::log::ILogObserver
{
public:
    void on_log_record(const cd::log::LogRecord& r) override
    {
        records.push_back(r);
    }

    std::vector<cd::log::LogRecord> records;
};

// --- LogLevel ---------------------------------------------------------------
TEST(LogLevel, StringMaps)
{
    EXPECT_EQ(cd::log::to_string(cd::log::LogLevel::Trace), "trace");
    EXPECT_EQ(cd::log::to_string(cd::log::LogLevel::Critical), "critical");
    EXPECT_EQ(cd::log::to_short_string(cd::log::LogLevel::Error), "ERR");
}

// --- Format -----------------------------------------------------------------
TEST(LogFormat, BraceSubstitution)
{
    EXPECT_EQ(cd::log::detail::format_braces("hello"), "hello");
    EXPECT_EQ(cd::log::detail::format_braces("hello {}", "world"), "hello world");
    EXPECT_EQ(cd::log::detail::format_braces("{} + {} = {}", 1, 2, 3), "1 + 2 = 3");
    EXPECT_EQ(cd::log::detail::format_braces("{}", true), "true");
    EXPECT_EQ(cd::log::detail::format_braces("{}", false), "false");
}

TEST(LogFormat, EscapedBraces)
{
    EXPECT_EQ(cd::log::detail::format_braces("{{}}"), "{}");
    EXPECT_EQ(cd::log::detail::format_braces("plain"), "plain");
}

TEST(LogFormat, ArityMismatchProducesError)
{
    auto s = cd::log::detail::format_braces("{} {}", 1);
    EXPECT_NE(s.find("[format_error]"), std::string::npos);
}

// --- ConsoleLogger ---------------------------------------------------------
TEST(ConsoleLogger, LevelFiltering)
{
    cd::log::ConsoleLogger logger { cd::log::LogLevel::Warning };
    EXPECT_EQ(logger.level(), cd::log::LogLevel::Warning);
    RecordingObserver obs;
    logger.add_observer(&obs);
    logger.info(std::source_location::current(), "should be filtered");
    logger.warn(std::source_location::current(), "should pass");
    logger.error(std::source_location::current(), "should pass");
    EXPECT_EQ(obs.records.size(), 2u);
    EXPECT_EQ(obs.records[0].level, cd::log::LogLevel::Warning);
    EXPECT_EQ(obs.records[1].level, cd::log::LogLevel::Error);
    logger.remove_observer(&obs);
}

TEST(ConsoleLogger, ObserverCapturesMessage)
{
    cd::log::ConsoleLogger logger { cd::log::LogLevel::Trace };
    RecordingObserver obs;
    logger.add_observer(&obs);
    logger.info(std::source_location::current(), "Hello {}", "World");
    ASSERT_EQ(obs.records.size(), 1u);
    EXPECT_EQ(obs.records[0].message, "Hello World");
    EXPECT_TRUE(obs.records[0].file_path.has_value());
    EXPECT_TRUE(obs.records[0].line.has_value());
    logger.remove_observer(&obs);
}

// --- Service (global accessor) ---------------------------------------------
TEST(LogService, SetGetClear)
{
    EXPECT_FALSE(cd::log::available());
    auto logger = std::make_shared<cd::log::ConsoleLogger>(cd::log::LogLevel::Info);
    cd::log::set(logger);
    EXPECT_TRUE(cd::log::available());
    EXPECT_EQ(cd::log::get(), logger.get());
    cd::log::clear();
    EXPECT_FALSE(cd::log::available());
}

TEST(LogService, MacrosNoOpWhenUnset)
{
    cd::log::clear();
    CD_LOG_INFO("unobserved");  // must not crash
    SUCCEED();
}

TEST(LogService, MacrosDispatchWhenSet)
{
    auto logger = std::make_shared<cd::log::ConsoleLogger>(cd::log::LogLevel::Trace);
    RecordingObserver obs;
    logger->add_observer(&obs);
    cd::log::set(logger);
    CD_LOG_INFO("count = {}", 42);
    CD_LOG_WARN("hello {}", "warn");
    EXPECT_EQ(obs.records.size(), 2u);
    EXPECT_EQ(obs.records[0].message, "count = 42");
    EXPECT_EQ(obs.records[1].level, cd::log::LogLevel::Warning);
    logger->remove_observer(&obs);
    cd::log::clear();
}

// --- AuditTrail (Sprint S2.4 P2 salvage) ------------------------------------
TEST(AuditTrail, RetainsRecordsUpToCapacity)
{
    cd::log::AuditTrail trail { 4 };
    EXPECT_EQ(trail.capacity(), 4u);
    EXPECT_EQ(trail.size(), 0u);

    cd::log::ConsoleLogger logger { cd::log::LogLevel::Trace };
    logger.add_observer(&trail);
    for (int i = 0; i < 7; ++i)
    {
        logger.info(std::source_location::current(), "msg {}", i);
    }
    logger.remove_observer(&trail);

    EXPECT_EQ(trail.size(), 4u);
    auto snap = trail.snapshot();
    ASSERT_EQ(snap.size(), 4u);
    EXPECT_EQ(snap.front().message, "msg 3");
    EXPECT_EQ(snap.back().message, "msg 6");
}

TEST(AuditTrail, ClearEmptiesTrail)
{
    cd::log::AuditTrail trail { 8 };
    cd::log::ConsoleLogger logger { cd::log::LogLevel::Trace };
    logger.add_observer(&trail);
    logger.info(std::source_location::current(), "first");
    logger.warn(std::source_location::current(), "second");
    logger.remove_observer(&trail);
    EXPECT_EQ(trail.size(), 2u);
    trail.clear();
    EXPECT_EQ(trail.size(), 0u);
    EXPECT_TRUE(trail.snapshot().empty());
}

TEST(AuditTrail, ObserverHookRespectsLevelFilter)
{
    cd::log::AuditTrail trail { 16 };
    cd::log::ConsoleLogger logger { cd::log::LogLevel::Warning };
    logger.add_observer(&trail);
    logger.trace(std::source_location::current(), "below threshold");
    logger.info(std::source_location::current(), "below threshold");
    logger.warn(std::source_location::current(), "captured");
    logger.error(std::source_location::current(), "captured");
    logger.remove_observer(&trail);
    EXPECT_EQ(trail.size(), 2u);
    auto snap = trail.snapshot();
    EXPECT_EQ(snap[0].level, cd::log::LogLevel::Warning);
    EXPECT_EQ(snap[1].level, cd::log::LogLevel::Error);
}

// --- JsonLogger ------------------------------------------------------------- Wave 55

TEST(JsonLogger, EmitsOneJsonObjectPerCall)
{
    // tmpfile() returns a stream that's auto-deleted on close; we rewind and
    // read back to inspect the JSON output.
    // tmpfile() is deprecated on MSVC CRT; use a regular fopen on a
    // path under the system temp dir. We delete it ourselves. Per-call
    // salt = current steady-clock nanoseconds so concurrent tests
    // don't collide.
    static std::atomic<std::uint64_t> s_salt { 1 };
    const auto temp_path = (std::filesystem::temp_directory_path()
                            / ("cd_jsonlog_"
                               + std::to_string(s_salt.fetch_add(1)) + ".jsonl"))
                             .string();
    std::FILE* tmp = std::fopen(temp_path.c_str(), "w+b");
    ASSERT_NE(tmp, nullptr);
    {
        cd::log::JsonLogger logger { tmp, cd::log::LogLevel::Info };
        logger.info(std::source_location::current(), "first");
        logger.warn(std::source_location::current(), "second");
        logger.flush();
    }
    std::rewind(tmp);
    std::string contents;
    int c = 0;
    while ((c = std::fgetc(tmp)) != EOF)
        contents.push_back(static_cast<char>(c));
    std::fclose(tmp);
    std::error_code ec;
    std::filesystem::remove(temp_path, ec);

    // Two JSONL lines.
    EXPECT_NE(contents.find("\"level\":\"info\""), std::string::npos);
    EXPECT_NE(contents.find("\"level\":\"warn\""), std::string::npos);
    EXPECT_NE(contents.find("\"message\":\"first\""), std::string::npos);
    EXPECT_NE(contents.find("\"message\":\"second\""), std::string::npos);
    // Each line ends with a newline.
    EXPECT_EQ(std::count(contents.begin(), contents.end(), '\n'), 2);
}

TEST(JsonLogger, EscapesQuotesAndBackslashes)
{
    // tmpfile() is deprecated on MSVC CRT; use a regular fopen on a
    // path under the system temp dir. We delete it ourselves. Per-call
    // salt = current steady-clock nanoseconds so concurrent tests
    // don't collide.
    static std::atomic<std::uint64_t> s_salt { 1 };
    const auto temp_path = (std::filesystem::temp_directory_path()
                            / ("cd_jsonlog_"
                               + std::to_string(s_salt.fetch_add(1)) + ".jsonl"))
                             .string();
    std::FILE* tmp = std::fopen(temp_path.c_str(), "w+b");
    ASSERT_NE(tmp, nullptr);
    {
        cd::log::JsonLogger logger { tmp, cd::log::LogLevel::Info };
        logger.info(std::source_location::current(), "a \"quoted\" and \\backslash");
    }
    std::rewind(tmp);
    std::string contents;
    int c = 0;
    while ((c = std::fgetc(tmp)) != EOF)
        contents.push_back(static_cast<char>(c));
    std::fclose(tmp);
    std::error_code ec;
    std::filesystem::remove(temp_path, ec);

    EXPECT_NE(contents.find("\\\""), std::string::npos);   // escaped quote
    EXPECT_NE(contents.find("\\\\"), std::string::npos);   // escaped backslash
}

TEST(JsonLogger, RespectsLevelFilter)
{
    // tmpfile() is deprecated on MSVC CRT; use a regular fopen on a
    // path under the system temp dir. We delete it ourselves. Per-call
    // salt = current steady-clock nanoseconds so concurrent tests
    // don't collide.
    static std::atomic<std::uint64_t> s_salt { 1 };
    const auto temp_path = (std::filesystem::temp_directory_path()
                            / ("cd_jsonlog_"
                               + std::to_string(s_salt.fetch_add(1)) + ".jsonl"))
                             .string();
    std::FILE* tmp = std::fopen(temp_path.c_str(), "w+b");
    ASSERT_NE(tmp, nullptr);
    {
        cd::log::JsonLogger logger { tmp, cd::log::LogLevel::Error };
        logger.info(std::source_location::current(), "should drop");
        logger.error(std::source_location::current(), "kept");
    }
    std::rewind(tmp);
    std::string contents;
    int c = 0;
    while ((c = std::fgetc(tmp)) != EOF)
        contents.push_back(static_cast<char>(c));
    std::fclose(tmp);
    std::error_code ec;
    std::filesystem::remove(temp_path, ec);

    EXPECT_EQ(contents.find("should drop"), std::string::npos);
    EXPECT_NE(contents.find("kept"), std::string::npos);
}

// --- RingBufferSink (Wave 110) ------------------------------------------

namespace
{
cd::log::LogRecord make_record(std::string msg, cd::log::LogLevel lvl = cd::log::LogLevel::Info)
{
    cd::log::LogRecord r;
    r.message = std::move(msg);
    r.level = lvl;
    return r;
}
}  // namespace

TEST(RingBufferSink, BelowCapacityKeepsAllInsertionOrder)
{
    cd::log::RingBufferSink sink { 4 };
    sink.on_log_record(make_record("a"));
    sink.on_log_record(make_record("b"));
    sink.on_log_record(make_record("c"));
    EXPECT_EQ(sink.size(), 3u);
    EXPECT_FALSE(sink.wrapped());

    auto snap = sink.snapshot();
    ASSERT_EQ(snap.size(), 3u);
    EXPECT_EQ(snap[0].message, "a");
    EXPECT_EQ(snap[1].message, "b");
    EXPECT_EQ(snap[2].message, "c");
}

TEST(RingBufferSink, AtCapacityFullSnapshot)
{
    cd::log::RingBufferSink sink { 3 };
    sink.on_log_record(make_record("a"));
    sink.on_log_record(make_record("b"));
    sink.on_log_record(make_record("c"));
    EXPECT_EQ(sink.size(), 3u);
    EXPECT_FALSE(sink.wrapped());

    auto snap = sink.snapshot();
    ASSERT_EQ(snap.size(), 3u);
    EXPECT_EQ(snap[0].message, "a");
    EXPECT_EQ(snap[2].message, "c");
}

TEST(RingBufferSink, WrapDropsOldestKeepsRecent)
{
    cd::log::RingBufferSink sink { 3 };
    sink.on_log_record(make_record("a"));
    sink.on_log_record(make_record("b"));
    sink.on_log_record(make_record("c"));
    sink.on_log_record(make_record("d"));  // 'a' dropped
    sink.on_log_record(make_record("e"));  // 'b' dropped
    EXPECT_EQ(sink.size(), 3u);
    EXPECT_TRUE(sink.wrapped());

    auto snap = sink.snapshot();
    ASSERT_EQ(snap.size(), 3u);
    EXPECT_EQ(snap[0].message, "c");
    EXPECT_EQ(snap[1].message, "d");
    EXPECT_EQ(snap[2].message, "e");
}

TEST(RingBufferSink, ClearResetsState)
{
    cd::log::RingBufferSink sink { 2 };
    sink.on_log_record(make_record("a"));
    sink.on_log_record(make_record("b"));
    sink.on_log_record(make_record("c"));  // wrap
    EXPECT_TRUE(sink.wrapped());
    sink.clear();
    EXPECT_EQ(sink.size(), 0u);
    EXPECT_FALSE(sink.wrapped());

    sink.on_log_record(make_record("x"));
    auto snap = sink.snapshot();
    ASSERT_EQ(snap.size(), 1u);
    EXPECT_EQ(snap[0].message, "x");
}

TEST(RingBufferSink, AttachedToConsoleLoggerObserversReceiveRecords)
{
    cd::log::ConsoleLogger logger;
    logger.set_level(cd::log::LogLevel::Trace);
    cd::log::RingBufferSink sink { 16 };
    logger.add_observer(&sink);

    logger.info(std::source_location::current(), "alpha={}", 1);
    logger.warn(std::source_location::current(), "beta={}", 2);

    logger.remove_observer(&sink);

    auto snap = sink.snapshot();
    ASSERT_EQ(snap.size(), 2u);
    EXPECT_NE(snap[0].message.find("alpha=1"), std::string::npos);
    EXPECT_NE(snap[1].message.find("beta=2"), std::string::npos);
    EXPECT_EQ(snap[1].level, cd::log::LogLevel::Warning);
}

TEST(RingBufferSink, CapacityZeroIsClampedToOne)
{
    cd::log::RingBufferSink sink { 0 };
    EXPECT_EQ(sink.capacity(), 1u);
    sink.on_log_record(make_record("only"));
    sink.on_log_record(make_record("survivor"));
    auto snap = sink.snapshot();
    ASSERT_EQ(snap.size(), 1u);
    EXPECT_EQ(snap[0].message, "survivor");
}

}  // namespace
