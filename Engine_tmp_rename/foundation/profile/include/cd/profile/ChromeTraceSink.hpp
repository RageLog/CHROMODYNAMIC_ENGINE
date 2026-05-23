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

#include <fstream>
#include <mutex>
#include <string>
#include <string_view>

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
            const std::lock_guard<std::mutex> lock { mutex_ };
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
            out_ << R"({"name":")" << s.name << R"(","cat":"cpu","ph":"X","ts":)" << ts_us << R"(,"dur":)" << dur_us
                 << R"(,"pid":1,"tid":)" << s.thread_hash << '}';
        }
        catch (...)
        {
        }
    }

    [[nodiscard]] bool is_open() const
    {
        return out_.is_open();
    }

private:
    std::string path_;
    std::ofstream out_;
    mutable std::mutex mutex_;
    bool first_ { true };
};

}  // namespace cd::profile
