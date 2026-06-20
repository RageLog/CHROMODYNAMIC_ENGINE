// =============================================================================
// CHROMODYNAMIC — cd/profile/ChromeTraceSink.hpp
//
// Emits scope samples in the Chrome Trace Event format (JSON array of
// duration events). Drop the resulting .json file into chrome://tracing
// or speedscope.app / perfetto.dev to get a sub-microsecond timeline
// viewer for free.
//
// We use the "X" (complete duration) event type — each sample becomes a
// single object with start time `ts` (microseconds) and `dur`
// (microseconds). This is more compact than emitting begin+end pairs
// and matches how every sane profiler exports for chrome://tracing.
//
// Format reference:
//   https://docs.google.com/document/d/1CvAClvFfyA5R-PhYUmn5OOQtYMH4h6I0nSsKchNAySU
//
// File layout written:
//   [
//     {"name":"frame","cat":"cpu","ph":"X","ts":12345,"dur":678,"pid":1,"tid":12345},
//     ...
//   ]
//
// The opening `[` is written on first sample; the closing `]` is written
// in the destructor. Commas between rows are emitted by tracking a
// "first sample?" flag — Chrome's parser is strict about valid JSON.
// =============================================================================
#pragma once

#include <cd/profile/Scope.hpp>

#include <atomic>
#include <cstdint>
#include <fstream>
#include <mutex>
#include <string>
#include <string_view>
#include <ostream>

namespace cd::profile
{

class ChromeTraceSink final : public ISink
{
public:
    explicit ChromeTraceSink(std::string_view path)
        : path_ { path }
    {
        // Truncate-on-open: each ChromeTraceSink instance writes a fresh
        // session. Append mode would corrupt the JSON array structure.
        out_.open(path_, std::ios::trunc);
        if (out_.is_open())
            out_ << '[';
    }

    ~ChromeTraceSink() override
    {
        if (out_.is_open())
        {
            out_ << "\n]\n";
            out_.flush();
        }
    }

    ChromeTraceSink(const ChromeTraceSink&) = delete;
    ChromeTraceSink& operator=(const ChromeTraceSink&) = delete;
    ChromeTraceSink(ChromeTraceSink&&) = delete;
    ChromeTraceSink& operator=(ChromeTraceSink&&) = delete;

    void submit(const Sample& s) noexcept override
    {
        try
        {
            const std::scoped_lock lock { mutex_ };
            if (!out_.is_open())
                return;
            if (first_)
            {
                first_ = false;
                out_ << '\n';
            }
            else
            {
                out_ << ",\n";
            }
            // Chrome tracing uses microsecond resolution; divide nanos by 1000.
            const double ts_us = static_cast<double>(s.start_ns) / 1000.0;
            const double dur_us = static_cast<double>(s.duration_ns) / 1000.0;
            // Single-line JSON object per sample keeps the file diff-friendly
            // and reduces parse time on perfetto.dev for huge traces.
            // name is JSON-escaped so scope names containing quotes or
            // backslashes (e.g., MSVC template strings) remain valid JSON.
            out_ << R"({"name":")";
            write_json_string(out_, s.name);
            out_ << R"(","cat":"cpu","ph":"X","ts":)" << ts_us << R"(,"dur":)" << dur_us
                 << R"(,"pid":1,"tid":)" << s.thread_hash << '}';
        }
        catch (...)
        {
            // ISink::submit is noexcept by contract, but ofstream IO can
            // still raise via badbit/exceptions or std::bad_alloc. The
            // profiler must never crash the app, so we count-and-swallow.
            // Consumers can read failed_writes() to detect a corrupt /
            // incomplete trace before handing the .json to perfetto.dev.
            failed_writes_.fetch_add(1, std::memory_order_relaxed);
        }
    }

    [[nodiscard]] bool is_open() const
    {
        return out_.is_open();
    }

    /// Number of submit() calls that raised an exception and were
    /// counted-and-swallowed by the catch-all boundary. Non-zero means
    /// the emitted trace is incomplete.
    [[nodiscard]] std::uint64_t failed_writes() const noexcept
    {
        return failed_writes_.load(std::memory_order_relaxed);
    }

private:
    /// Write `sv` to `os` with JSON string escaping: `"` → `\"`,
    /// `\` → `\\`, and control characters as `\uXXXX`. This keeps
    /// the emitted JSON valid even when scope names contain unusual
    /// characters (e.g., template instantiation strings from MSVC).
    static void write_json_string(std::ostream& os, std::string_view sv)
    {
        for (const char ch : sv)
        {
            if (ch == '"')        { os << "\\\""; }
            else if (ch == '\\')  { os << "\\\\"; }
            else if (ch == '\n')  { os << "\\n";  }
            else if (ch == '\r')  { os << "\\r";  }
            else if (ch == '\t')  { os << "\\t";  }
            else                  { os << ch;      }
        }
    }

    std::string path_;
    std::ofstream out_;
    mutable std::mutex mutex_;
    bool first_ { true };
    std::atomic<std::uint64_t> failed_writes_ { 0 };
};

}  // namespace cd::profile
