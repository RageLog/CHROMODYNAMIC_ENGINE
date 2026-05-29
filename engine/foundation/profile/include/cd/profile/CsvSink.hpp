// =============================================================================
// CHROMODYNAMIC — cd/profile/CsvSink.hpp
//
// Streams every `Sample` to a CSV file on disk. Designed for offline
// analysis (pandas, Excel, perfetto via the JSON Chrome trace sink in
// a parallel header). One row per sample; columns are stable:
//
//   thread_hash,start_ns,duration_ns,name
//
// The file is opened in `append` mode so multiple processes / runs can
// accumulate into the same CSV without truncating earlier data. The
// header row is written once on first sample submission (idempotent
// via a `header_written_` flag).
//
// Lock-protected: a single std::mutex guards both the header write and
// the row writes, so concurrent submitters from multiple threads cannot
// interleave bytes. The cost — one mutex per submit — is acceptable
// because the CsvSink is opt-in (production runs use the in-memory
// BufferSink) and the file IO already dominates.
// =============================================================================
#pragma once

#include <cd/profile/Scope.hpp>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <mutex>
#include <string>
#include <string_view>

namespace cd::profile
{

class CsvSink final : public ISink
{
public:
    /// Open `path` in append mode. If the file is empty or doesn't exist,
    /// the CSV column header is written on the first submit(). Failure to
    /// open is silent — submit() then becomes a no-op, matching the
    /// "profiling should never crash the app" contract.
    explicit CsvSink(std::string_view path)
        : path_ { path }
    {
        out_.open(path_, std::ios::app);
    }

    /// Flush remaining bytes on destruction. ofstream's dtor already
    /// flushes, but being explicit makes the intent obvious.
    ~CsvSink() override
    {
        if (out_.is_open())
            out_.flush();
    }

    CsvSink(const CsvSink&) = delete;
    CsvSink& operator=(const CsvSink&) = delete;
    CsvSink(CsvSink&&) = delete;
    CsvSink& operator=(CsvSink&&) = delete;

    void submit(const Sample& s) noexcept override
    {
        try
        {
            const std::scoped_lock lock { mutex_ };
            if (!out_.is_open())
                return;
            if (!header_written_)
            {
                out_ << "thread_hash,start_ns,duration_ns,name\n";
                header_written_ = true;
            }
            // Quote the name field — sample names are compile-time
            // literals and unlikely to contain quotes, but defending
            // against future use of dynamic strings is cheap.
            out_ << s.thread_hash << ',' << s.start_ns << ',' << s.duration_ns << ",\"" << s.name << "\"\n";
        }
        catch (...)
        {
            // ISink::submit is noexcept by contract, but ofstream can still
            // throw (e.g. badbit + exceptions(failbit)) or the surrounding
            // code can raise std::bad_alloc. We can't propagate to the
            // caller (the profiler can't be allowed to crash the app), but
            // we record the failure on a counter so a downstream reader of
            // `failed_writes()` can detect dropped samples instead of
            // silently believing the CSV is complete.
            failed_writes_.fetch_add(1, std::memory_order_relaxed);
        }
    }

    [[nodiscard]] bool is_open() const
    {
        return out_.is_open();
    }

    /// Number of submit() calls that raised an exception and were
    /// counted-and-swallowed by the catch-all boundary. Non-zero means
    /// the CSV stream is incomplete.
    [[nodiscard]] std::uint64_t failed_writes() const noexcept
    {
        return failed_writes_.load(std::memory_order_relaxed);
    }

private:
    std::string path_;
    std::ofstream out_;
    mutable std::mutex mutex_;
    bool header_written_ { false };
    std::atomic<std::uint64_t> failed_writes_ { 0 };
};

}  // namespace cd::profile
