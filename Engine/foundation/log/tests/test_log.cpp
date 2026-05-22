// =============================================================================
// CHROMODYNAMIC — cd::log tests (Sprint S2.2 + S2.4)
// =============================================================================
#include <cd/log/AuditTrail.hpp>
#include <cd/log/ConsoleLogger.hpp>
#include <cd/log/Format.hpp>
#include <cd/log/ILogger.hpp>
#include <cd/log/LogLevel.hpp>
#include <cd/log/LogRecord.hpp>
#include <cd/log/Service.hpp>
#include <gtest/gtest.h>

#include <memory>
#include <source_location>
#include <string>
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

}  // namespace
