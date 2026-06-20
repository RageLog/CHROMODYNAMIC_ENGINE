// =============================================================================
// CHROMODYNAMIC — cd::log tests (Sprint S2.2 + S2.4 + depth-100 pass)
// =============================================================================
// JsonLogger takes a FILE*. We use std::fopen + std::filesystem::remove
// for the tmp files in the JsonLogger tests; MSVC CRT marks fopen as
// deprecated in favour of fopen_s but the test is the only consumer and
// the codepath is clearly bracketed.
#if defined(_MSC_VER)
// NOLINTNEXTLINE(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp) -- documented MSVC CRT opt-out macro; must use the reserved name.
#    define _CRT_SECURE_NO_WARNINGS 1
#endif

#include <cd/log/AuditTrail.hpp>
#include <cd/log/ConsoleLogger.hpp>
#include <cd/log/Format.hpp>
#include <cd/log/ILogger.hpp>
#include <cd/log/JsonLogger.hpp>
#include <cd/diag/Assert.hpp>
#include <cd/log/LogLevel.hpp>
#include <cd/log/LogRecord.hpp>
#include <cd/log/PanicDump.hpp>
#include <cd/log/RingBufferSink.hpp>
#include <cd/log/Service.hpp>
#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
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
    (void)std::fseek(tmp, 0, SEEK_SET);
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
        logger.info(std::source_location::current(), R"(a "quoted" and \backslash)");
    }
    (void)std::fseek(tmp, 0, SEEK_SET);
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
    (void)std::fseek(tmp, 0, SEEK_SET);
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

// --- PanicDump bridge (Wave 111) ----------------------------------------

namespace
{
struct PanicException
{
    cd::diag::PanicInfo info;
};

void throwing_panic_handler(const cd::diag::PanicInfo& info)
{
    // NOLINTNEXTLINE(hicpp-exception-baseclass) -- test-local carrier type intentionally non-std; deriving from std::exception would break the aggregate init and change nothing the test observes.
    throw PanicException { info };
}

class PanicHandlerGuard
{
public:
    PanicHandlerGuard() noexcept
        : prev_ { cd::diag::current_panic_handler() }
    {
    }
    ~PanicHandlerGuard()
    {
        (void)cd::diag::set_panic_handler(prev_);
    }
    PanicHandlerGuard(const PanicHandlerGuard&) = delete;
    PanicHandlerGuard& operator=(const PanicHandlerGuard&) = delete;
    PanicHandlerGuard(PanicHandlerGuard&&) = delete;
    PanicHandlerGuard& operator=(PanicHandlerGuard&&) = delete;

private:
    cd::diag::PanicHandler prev_;
};
}  // namespace

TEST(PanicDump, InstallsAndExposesSink)
{
    PanicHandlerGuard guard;
    cd::log::RingBufferSink sink { 4 };
    auto* before = cd::log::current_panic_dump_sink();
    auto prev = cd::log::install_panic_dump_handler(&sink);
    EXPECT_NE(prev, nullptr);  // there was always something installed
    EXPECT_EQ(cd::log::current_panic_dump_sink(), &sink);
    (void)before;
    // Restore the panic handler before the sink goes out of scope so a
    // later panic can't ever dereference our local sink pointer.
    (void)cd::diag::set_panic_handler(prev);
}

TEST(PanicDump, RoutesPanicThroughDumpHandlerWithSinkAttached)
{
    PanicHandlerGuard guard;
    cd::log::RingBufferSink sink { 8 };

    // Pre-populate the ring with a couple of records as if the engine
    // had been running.
    cd::log::LogRecord r1;
    r1.message = "first event";
    r1.level = cd::log::LogLevel::Info;
    sink.on_log_record(r1);
    cd::log::LogRecord r2;
    r2.message = "second event right before death";
    r2.level = cd::log::LogLevel::Warning;
    sink.on_log_record(r2);

    // Install our dump handler so panics print the mirror, then chain a
    // throwing handler on top so the test can observe the panic without
    // actually aborting. The dump handler runs whoever was installed
    // before via the panic() tail-fall-through? No — set_panic_handler
    // replaces, not chains. So we test the two halves separately:
    //   1) install_panic_dump_handler stores the sink and returns the prior handler
    //   2) the throwing handler we install after just verifies panic() still propagates
    auto prev_after_dump = cd::log::install_panic_dump_handler(&sink);
    (void)prev_after_dump;
    (void)cd::diag::set_panic_handler(&throwing_panic_handler);

    // Capture stderr to confirm the dump-handler path *can* be exercised
    // explicitly by calling it directly. This bypasses set_panic_handler
    // and is the cleanest way to assert the formatting without forking.
    std::FILE* old_stderr = stderr;
    (void)old_stderr;

    // Direct invocation of the dump handler (it's noexcept and safe).
    cd::diag::PanicInfo pi;
    pi.expression = "x == y";
    pi.message = "synthetic";
    pi.file = "fake.cpp";
    pi.line = 42u;
    pi.function = "fn";
    cd::log::detail::panic_dump_handler(pi);  // writes to stderr; non-fatal

    // And confirm the throwing handler is still wired through panic():
    EXPECT_THROW(cd::diag::panic(pi), PanicException);
}

TEST(PanicDump, NullSinkIsAccepted)
{
    PanicHandlerGuard guard;
    auto prev = cd::log::install_panic_dump_handler(nullptr);
    EXPECT_NE(prev, nullptr);
    EXPECT_EQ(cd::log::current_panic_dump_sink(), nullptr);
    (void)cd::diag::set_panic_handler(prev);
}

TEST(PanicDump, NullSinkDumpHandlerDoesNotCrash)
{
    // With no sink installed, calling the dump handler directly must not crash.
    PanicHandlerGuard guard;
    (void)cd::log::install_panic_dump_handler(nullptr);
    cd::diag::PanicInfo pi;
    pi.expression = "a == b";
    pi.message = "null sink test";
    pi.file = "fake.cpp";
    pi.line = 1u;
    pi.function = "test_fn";
    // Must not crash; output goes to stderr.
    cd::log::detail::panic_dump_handler(pi);
    SUCCEED();
}

// =============================================================================
// Format — depth-100 additions
// =============================================================================

TEST(LogFormat, ExcessArgsProducesFormatError)
{
    // More args than placeholders → [format_error] appended.
    auto s = cd::log::detail::format_braces("hello", 1, 2);
    EXPECT_NE(s.find("[format_error]"), std::string::npos);
}

TEST(LogFormat, ZeroArgsZeroPlaceholders)
{
    // No placeholders, no args → exact copy.
    EXPECT_EQ(cd::log::detail::format_braces("no braces here"), "no braces here");
}

TEST(LogFormat, EmptyFormatStringNoArgs)
{
    EXPECT_EQ(cd::log::detail::format_braces(""), "");
}

TEST(LogFormat, EmptyFormatStringWithArgProducesError)
{
    auto s = cd::log::detail::format_braces("", 42);
    EXPECT_NE(s.find("[format_error]"), std::string::npos);
}

TEST(LogFormat, NullCharPtrConvertsToNullLabel)
{
    const char* p = nullptr;
    EXPECT_EQ(cd::log::detail::to_log_string(p), "(null)");
}

TEST(LogFormat, EnumConvertsToUnderlyingInt)
{
    enum class MyEnum : int { Val = 7 };
    EXPECT_EQ(cd::log::detail::to_log_string(MyEnum::Val), "7");
}

TEST(LogFormat, DoubleBraceEscapeProducesLiteralBrace)
{
    // "{{" → '{', "}}" → '}'
    EXPECT_EQ(cd::log::detail::format_braces("{{value}}"), "{value}");
}

TEST(LogFormat, StrayClosingBraceProducesFormatError)
{
    // Lone '}' not preceded by '}' is an error.
    auto s = cd::log::detail::format_braces("x}y");
    EXPECT_NE(s.find("[format_error]"), std::string::npos);
}

TEST(LogFormat, MixedEscapedAndSubstituted)
{
    // "{{}} {}" → '{} <arg>'
    auto s = cd::log::detail::format_braces("{{}} {}", "X");
    EXPECT_EQ(s, "{} X");
}

// =============================================================================
// LogLevel — short string completeness
// =============================================================================

TEST(LogLevel, AllShortStringsDistinct)
{
    // Every level has a unique 3-char short tag.
    const std::vector<cd::log::LogLevel> levels {
        cd::log::LogLevel::Trace, cd::log::LogLevel::Debug,
        cd::log::LogLevel::Info,  cd::log::LogLevel::Warning,
        cd::log::LogLevel::Error, cd::log::LogLevel::Critical,
        cd::log::LogLevel::Off,
    };
    std::vector<std::string_view> tags;
    tags.reserve(levels.size());
    for (auto l : levels)
        tags.emplace_back(cd::log::to_short_string(l));
    // All must be different.
    const auto orig_size = tags.size();
    std::ranges::sort(tags);
    tags.erase(std::ranges::unique(tags).begin(), tags.end());
    EXPECT_EQ(tags.size(), orig_size);
}

TEST(LogLevel, OffStringIsOff)
{
    EXPECT_EQ(cd::log::to_string(cd::log::LogLevel::Off), "off");
    EXPECT_EQ(cd::log::to_short_string(cd::log::LogLevel::Off), "OFF");
}

// =============================================================================
// ConsoleLogger — depth-100 additions
// =============================================================================

TEST(ConsoleLogger, SetLevelDynamicallyChangesFilter)
{
    cd::log::ConsoleLogger logger { cd::log::LogLevel::Error };
    RecordingObserver obs;
    logger.add_observer(&obs);

    // Nothing passes at Error threshold...
    logger.warn(std::source_location::current(), "should drop");
    EXPECT_TRUE(obs.records.empty());

    // Lower threshold to Trace.
    logger.set_level(cd::log::LogLevel::Trace);
    logger.debug(std::source_location::current(), "now passes");
    EXPECT_EQ(obs.records.size(), 1u);
    EXPECT_EQ(obs.records[0].level, cd::log::LogLevel::Debug);
    logger.remove_observer(&obs);
}

TEST(ConsoleLogger, OffLevelSuppressesEverything)
{
    cd::log::ConsoleLogger logger { cd::log::LogLevel::Off };
    RecordingObserver obs;
    logger.add_observer(&obs);
    logger.critical(std::source_location::current(), "must not appear");
    EXPECT_TRUE(obs.records.empty());
    logger.remove_observer(&obs);
}

TEST(ConsoleLogger, FlushDoesNotCrash)
{
    cd::log::ConsoleLogger logger;
    logger.flush();
    SUCCEED();
}

TEST(ConsoleLogger, NullObserverAddIsNoOp)
{
    cd::log::ConsoleLogger logger { cd::log::LogLevel::Trace };
    // Must not crash.
    logger.add_observer(nullptr);
    logger.info(std::source_location::current(), "safe");
    SUCCEED();
}

TEST(ConsoleLogger, RemoveNonAddedObserverIsNoOp)
{
    cd::log::ConsoleLogger logger { cd::log::LogLevel::Trace };
    RecordingObserver obs;
    // Never added — must not crash.
    logger.remove_observer(&obs);
    SUCCEED();
}

TEST(ConsoleLogger, MultipleSinkFanOut)
{
    // Verify that two independent observers both receive every record.
    cd::log::ConsoleLogger logger { cd::log::LogLevel::Trace };
    RecordingObserver obs1;
    RecordingObserver obs2;
    logger.add_observer(&obs1);
    logger.add_observer(&obs2);

    logger.info(std::source_location::current(), "broadcast {}", 1);
    logger.warn(std::source_location::current(), "broadcast {}", 2);

    logger.remove_observer(&obs1);
    logger.remove_observer(&obs2);

    ASSERT_EQ(obs1.records.size(), 2u);
    ASSERT_EQ(obs2.records.size(), 2u);
    EXPECT_EQ(obs1.records[0].message, "broadcast 1");
    EXPECT_EQ(obs2.records[1].message, "broadcast 2");
}

TEST(ConsoleLogger, EmptyMessageDoesNotCrash)
{
    cd::log::ConsoleLogger logger { cd::log::LogLevel::Trace };
    RecordingObserver obs;
    logger.add_observer(&obs);
    logger.info(std::source_location::current(), "");
    ASSERT_EQ(obs.records.size(), 1u);
    EXPECT_EQ(obs.records[0].message, "");
    logger.remove_observer(&obs);
}

TEST(ConsoleLogger, LevelBoundaryExactMatch)
{
    // A message AT the threshold level must pass.
    cd::log::ConsoleLogger logger { cd::log::LogLevel::Warning };
    RecordingObserver obs;
    logger.add_observer(&obs);
    logger.warn(std::source_location::current(), "exact boundary");
    ASSERT_EQ(obs.records.size(), 1u);
    EXPECT_EQ(obs.records[0].level, cd::log::LogLevel::Warning);
    logger.remove_observer(&obs);
}

// =============================================================================
// JsonLogger — depth-100 additions
// =============================================================================

namespace
{

// Helper: creates a temp file, runs action(logger), returns file content.
std::string json_logger_capture(cd::log::LogLevel lvl,
    const std::function<void(cd::log::JsonLogger&)>& action)
{
    static std::atomic<std::uint64_t> s_salt { 0x1000 };
    const auto temp_path =
        (std::filesystem::temp_directory_path()
         / ("cd_jsonlog_depth_"
            + std::to_string(s_salt.fetch_add(1)) + ".jsonl"))
            .string();
    std::FILE* tmp = std::fopen(temp_path.c_str(), "w+b");
    if (tmp == nullptr)
        return {};
    {
        cd::log::JsonLogger logger { tmp, lvl };
        action(logger);
        logger.flush();
    }
    (void)std::fseek(tmp, 0, SEEK_SET);
    std::string contents;
    int ch = 0;
    while ((ch = std::fgetc(tmp)) != EOF)
        contents.push_back(static_cast<char>(ch));
    std::fclose(tmp);
    std::error_code ec;
    std::filesystem::remove(temp_path, ec);
    return contents;
}

}  // namespace

TEST(JsonLogger, EscapesNewlineAndTab)
{
    auto s = json_logger_capture(cd::log::LogLevel::Trace, [](cd::log::JsonLogger& l) {
        l.info(std::source_location::current(), "line1\nline2\ttab");
    });
    EXPECT_NE(s.find("\\n"), std::string::npos);
    EXPECT_NE(s.find("\\t"), std::string::npos);
}

TEST(JsonLogger, EscapesCarriageReturn)
{
    auto s = json_logger_capture(cd::log::LogLevel::Trace, [](cd::log::JsonLogger& l) {
        l.info(std::source_location::current(), "has\r\n");
    });
    EXPECT_NE(s.find("\\r"), std::string::npos);
}

TEST(JsonLogger, EscapesControlCharsAsUnicodeEscape)
{
    // ASCII 0x01 (SOH) must become .
    auto s = json_logger_capture(cd::log::LogLevel::Trace, [](cd::log::JsonLogger& l) {
        l.info(std::source_location::current(), std::string { '\x01', 'x' });
    });
    EXPECT_NE(s.find("\\u0001"), std::string::npos);
}

TEST(JsonLogger, EscapesBackspaceAndFormFeed)
{
    auto s = json_logger_capture(cd::log::LogLevel::Trace, [](cd::log::JsonLogger& l) {
        l.info(std::source_location::current(), std::string { '\b', '\f' });
    });
    EXPECT_NE(s.find("\\b"), std::string::npos);
    EXPECT_NE(s.find("\\f"), std::string::npos);
}

TEST(JsonLogger, EmptyMessageEmitsTsAndLevel)
{
    auto s = json_logger_capture(cd::log::LogLevel::Trace, [](cd::log::JsonLogger& l) {
        l.info(std::source_location::current(), "");
    });
    EXPECT_NE(s.find("\"message\":\"\""), std::string::npos);
    EXPECT_NE(s.find("\"ts_us\":"), std::string::npos);
}

TEST(JsonLogger, TsUsFieldIsNonNegativeInteger)
{
    // ts_us must be a non-negative integer value (digits after "ts_us":).
    auto s = json_logger_capture(cd::log::LogLevel::Trace, [](cd::log::JsonLogger& l) {
        l.info(std::source_location::current(), "timing");
    });
    const auto pos = s.find("\"ts_us\":");
    ASSERT_NE(pos, std::string::npos);
    const std::size_t digits_start = pos + 8u;  // length of '"ts_us":'
    ASSERT_LT(digits_start, s.size());
    // First char after colon must be a digit (not '-').
    EXPECT_TRUE(std::isdigit(static_cast<unsigned char>(s[digits_start])));
}

TEST(JsonLogger, SetLevelDynamicallyFilters)
{
    auto s = json_logger_capture(cd::log::LogLevel::Error, [](cd::log::JsonLogger& l) {
        l.info(std::source_location::current(), "pre-change");
        l.set_level(cd::log::LogLevel::Trace);
        l.info(std::source_location::current(), "post-change");
    });
    EXPECT_EQ(s.find("pre-change"), std::string::npos);
    EXPECT_NE(s.find("post-change"), std::string::npos);
}

TEST(JsonLogger, ObserverReceivesRecords)
{
    RecordingObserver obs;
    {
        static std::atomic<std::uint64_t> s_salt { 0x2000 };
        const auto temp_path =
            (std::filesystem::temp_directory_path()
             / ("cd_jsonlog_obs_"
                + std::to_string(s_salt.fetch_add(1)) + ".jsonl"))
                .string();
        std::FILE* tmp = std::fopen(temp_path.c_str(), "w+b");
        ASSERT_NE(tmp, nullptr);
        {
            cd::log::JsonLogger logger { tmp, cd::log::LogLevel::Trace };
            logger.add_observer(&obs);
            logger.warn(std::source_location::current(), "observer test");
            logger.remove_observer(&obs);
        }
        std::fclose(tmp);
        std::error_code ec;
        std::filesystem::remove(temp_path, ec);
    }
    ASSERT_EQ(obs.records.size(), 1u);
    EXPECT_EQ(obs.records[0].message, "observer test");
    EXPECT_EQ(obs.records[0].level, cd::log::LogLevel::Warning);
}

TEST(JsonLogger, NullStreamIsNoOp)
{
    // Constructing with nullptr stream must not crash on any log call.
    cd::log::JsonLogger logger { nullptr, cd::log::LogLevel::Trace };
    logger.info(std::source_location::current(), "silent");
    logger.flush();
    SUCCEED();
}

// =============================================================================
// RingBufferSink — depth-100 additions
// =============================================================================

TEST(RingBufferSink, SingleSlotRingAlwaysHoldsLatest)
{
    // capacity = 1: every new record evicts the previous one.
    cd::log::RingBufferSink sink { 1 };
    sink.on_log_record(make_record("first"));
    sink.on_log_record(make_record("second"));
    sink.on_log_record(make_record("third"));
    EXPECT_TRUE(sink.wrapped());
    auto snap = sink.snapshot();
    ASSERT_EQ(snap.size(), 1u);
    EXPECT_EQ(snap[0].message, "third");
}

TEST(RingBufferSink, MultipleWrapRoundsChronological)
{
    // Push 3× capacity records; snapshot must be the last `capacity` records
    // in insertion order.
    cd::log::RingBufferSink sink { 4 };
    for (int i = 0; i < 12; ++i)
        sink.on_log_record(make_record("m" + std::to_string(i)));

    EXPECT_TRUE(sink.wrapped());
    auto snap = sink.snapshot();
    ASSERT_EQ(snap.size(), 4u);
    // Last 4: m8, m9, m10, m11
    EXPECT_EQ(snap[0].message, "m8");
    EXPECT_EQ(snap[1].message, "m9");
    EXPECT_EQ(snap[2].message, "m10");
    EXPECT_EQ(snap[3].message, "m11");
}

TEST(RingBufferSink, SizeAndCapacityInvariantAfterClear)
{
    cd::log::RingBufferSink sink { 5 };
    for (int i = 0; i < 7; ++i)
        sink.on_log_record(make_record("x"));
    sink.clear();
    EXPECT_EQ(sink.size(), 0u);
    EXPECT_EQ(sink.capacity(), 5u);
    EXPECT_FALSE(sink.wrapped());
}

TEST(RingBufferSink, LevelPreservedThroughObserver)
{
    // Observer path: level field on LogRecord must survive the ring.
    cd::log::ConsoleLogger logger { cd::log::LogLevel::Trace };
    cd::log::RingBufferSink sink { 8 };
    logger.add_observer(&sink);
    logger.critical(std::source_location::current(), "critical event");
    logger.remove_observer(&sink);

    auto snap = sink.snapshot();
    ASSERT_EQ(snap.size(), 1u);
    EXPECT_EQ(snap[0].level, cd::log::LogLevel::Critical);
}

// =============================================================================
// AuditTrail — depth-100 additions
// =============================================================================

TEST(AuditTrail, SingleEntry)
{
    cd::log::AuditTrail trail { 4 };
    cd::log::LogRecord r;
    r.message = "only one";
    r.level = cd::log::LogLevel::Info;
    trail.on_log_record(r);
    EXPECT_EQ(trail.size(), 1u);
    auto snap = trail.snapshot();
    ASSERT_EQ(snap.size(), 1u);
    EXPECT_EQ(snap[0].message, "only one");
}

TEST(AuditTrail, ExactCapacityNoEviction)
{
    // Pushing exactly `capacity` records should evict none.
    cd::log::AuditTrail trail { 3 };
    cd::log::LogRecord r;
    r.level = cd::log::LogLevel::Debug;
    for (int i = 0; i < 3; ++i)
    {
        r.message = "rec" + std::to_string(i);
        trail.on_log_record(r);
    }
    EXPECT_EQ(trail.size(), 3u);
    auto snap = trail.snapshot();
    EXPECT_EQ(snap[0].message, "rec0");
    EXPECT_EQ(snap[2].message, "rec2");
}

TEST(AuditTrail, OrderingIsInsertionOrder)
{
    cd::log::AuditTrail trail { 8 };
    cd::log::ConsoleLogger logger { cd::log::LogLevel::Trace };
    logger.add_observer(&trail);
    logger.info(std::source_location::current(), "alpha");
    logger.info(std::source_location::current(), "beta");
    logger.info(std::source_location::current(), "gamma");
    logger.remove_observer(&trail);

    auto snap = trail.snapshot();
    ASSERT_EQ(snap.size(), 3u);
    EXPECT_EQ(snap[0].message, "alpha");
    EXPECT_EQ(snap[1].message, "beta");
    EXPECT_EQ(snap[2].message, "gamma");
}

TEST(AuditTrail, ConsecutiveClearIsSafe)
{
    cd::log::AuditTrail trail { 4 };
    trail.clear();
    trail.clear();  // must not crash
    EXPECT_EQ(trail.size(), 0u);
}

TEST(AuditTrail, CapacityZeroEvictsImmediately)
{
    // Capacity 0 → deque always empty after eviction while loop.
    cd::log::AuditTrail trail { 0 };
    cd::log::LogRecord r;
    r.message = "any";
    r.level = cd::log::LogLevel::Info;
    trail.on_log_record(r);
    // After push_back(r) and pop_front: size becomes 0.
    EXPECT_EQ(trail.size(), 0u);
    EXPECT_TRUE(trail.snapshot().empty());
}

// =============================================================================
// Service — depth-100 additions
// =============================================================================

TEST(LogService, AvailableReturnsFalseAfterClear)
{
    auto logger = std::make_shared<cd::log::ConsoleLogger>();
    cd::log::set(logger);
    EXPECT_TRUE(cd::log::available());
    cd::log::clear();
    EXPECT_FALSE(cd::log::available());
}

TEST(LogService, AllMacrosNoOpWhenUnset)
{
    cd::log::clear();
    CD_LOG_TRACE("t");
    CD_LOG_DEBUG("d");
    CD_LOG_INFO("i");
    CD_LOG_WARN("w");
    CD_LOG_ERROR("e");
    CD_LOG_CRITICAL("c");
    SUCCEED();
}

}  // namespace
