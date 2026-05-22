// =============================================================================
// CHROMODYNAMIC — cd/log/JsonLogger.hpp
// Phase 6 / Wave 55 — JSONL log sink.
//
// JSON-Lines (https://jsonlines.org) logger: one JSON object per log
// record, separated by '\n'. The format is the standard interchange
// for log shippers (Loki, Vector, Filebeat, Splunk HEC, etc.) so
// piping a CHROMODYNAMIC process through `... | curl … -d @-` or
// `... | jq …` "just works" without an adapter.
//
// Output stream is configurable — pass any FILE* (stdout/stderr/fopen
// result/tmpfile). Caller owns the FILE*; the logger does not close
// it on destruction (consistent with the ConsoleLogger contract).
//
// JSON schema per record:
//   {
//     "ts_us":    <int>,             // microseconds since process start
//     "level":    "trace|debug|info|warn|error|critical",
//     "file":     "path/to/file.cpp", // omitted if no source_location
//     "line":     <int>,             // omitted if no source_location
//     "message":  "the formatted text"
//   }
//
// String escaping covers ASCII control + double-quote + backslash. The
// formatted message is expected to be UTF-8; non-ASCII bytes are
// emitted verbatim (RFC 8259 §8.1 permits this when the carrier
// stream is also UTF-8, which any modern log shipper expects).
// =============================================================================
#pragma once

#include <cd/log/ILogger.hpp>
#include <cd/log/LogLevel.hpp>
#include <cd/log/LogRecord.hpp>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace cd::log
{

class JsonLogger final : public ILogger
{
public:
    explicit JsonLogger(std::FILE* stream = stdout,
                        LogLevel min_level = LogLevel::Info) noexcept
        : stream_ { stream }, level_ { min_level }, t0_ { Clock::now() }
    {
    }

    void set_level(LogLevel level) noexcept override
    {
        level_.store(level, std::memory_order_relaxed);
    }
    [[nodiscard]] LogLevel level() const noexcept override
    {
        return level_.load(std::memory_order_relaxed);
    }
    void flush() noexcept override
    {
        if (stream_ != nullptr)
            std::fflush(stream_);
    }

    void add_observer(ILogObserver* observer) override
    {
        if (observer == nullptr)
            return;
        std::lock_guard guard { observers_lock_ };
        observers_.push_back(observer);
    }
    void remove_observer(ILogObserver* observer) override
    {
        std::lock_guard guard { observers_lock_ };
        std::erase(observers_, observer);
    }

protected:
    [[nodiscard]] bool should_log(LogLevel l) const noexcept override
    {
        return l >= level_.load(std::memory_order_relaxed) && l != LogLevel::Off;
    }

    void log_impl(LogLevel l, const std::source_location* loc, std::string_view msg) override
    {
        if (stream_ == nullptr)
            return;
        const auto now = Clock::now();
        const auto us = std::chrono::duration_cast<std::chrono::microseconds>(now - t0_).count();
        const auto level_str = to_string(l);

        std::string out;
        out.reserve(msg.size() + 96);
        out.append("{\"ts_us\":");
        out.append(std::to_string(us));
        out.append(",\"level\":\"");
        out.append(level_str);
        out.append("\"");
        if (loc != nullptr)
        {
            out.append(",\"file\":\"");
            append_json_escaped_(out, loc->file_name());
            out.append("\",\"line\":");
            out.append(std::to_string(loc->line()));
        }
        out.append(",\"message\":\"");
        append_json_escaped_(out, msg);
        out.append("\"}\n");

        // One fwrite for the whole record — keeps the per-line atomic
        // on POSIX (line ≤ PIPE_BUF) so concurrent loggers don't tear.
        std::lock_guard guard { write_lock_ };
        std::fwrite(out.data(), 1, out.size(), stream_);
        if (l >= LogLevel::Warning)
            std::fflush(stream_);

        // Observers — same hook as ConsoleLogger.
        std::lock_guard obs_guard { observers_lock_ };
        if (!observers_.empty())
        {
            LogRecord rec;
            rec.observed_at = now;
            rec.level = l;
            rec.message.assign(msg);
            if (loc != nullptr)
            {
                rec.file_path = std::string { loc->file_name() };
                rec.line = loc->line();
            }
            for (auto* o : observers_)
                o->on_log_record(rec);
        }
    }

private:
    using Clock = std::chrono::steady_clock;

    static void append_json_escaped_(std::string& out, std::string_view s)
    {
        out.reserve(out.size() + s.size() + 2);
        for (const char ch : s)
        {
            const auto c = static_cast<unsigned char>(ch);
            switch (c)
            {
                case '"': out += "\\\""; break;
                case '\\': out += "\\\\"; break;
                case '\b': out += "\\b"; break;
                case '\f': out += "\\f"; break;
                case '\n': out += "\\n"; break;
                case '\r': out += "\\r"; break;
                case '\t': out += "\\t"; break;
                default:
                    if (c < 0x20)
                    {
                        char buf[8];
                        std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                        out += buf;
                    }
                    else
                    {
                        out += ch;
                    }
                    break;
            }
        }
    }

    std::FILE* stream_;
    std::atomic<LogLevel> level_;
    Clock::time_point t0_;
    std::mutex write_lock_;
    std::mutex observers_lock_;
    std::vector<ILogObserver*> observers_;
};

}  // namespace cd::log
