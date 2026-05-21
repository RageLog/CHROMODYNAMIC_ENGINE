// =============================================================================
// CHROMODYNAMIC — cd/log/ConsoleLogger.hpp
// Simple stderr/stdout logger; no dependencies. Sprint S2.2 baseline; richer
// backends (JSONL, spdlog wrapper, telemetry sink) land in S2.x as needed.
// =============================================================================
#pragma once

#include <cd/log/ILogger.hpp>

#include <atomic>
#include <cstdio>
#include <mutex>
#include <vector>

namespace cd::log
{

class ConsoleLogger final : public ILogger
{
public:
    explicit ConsoleLogger(LogLevel min_level = LogLevel::Info) noexcept
        : level_ { min_level }
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
        std::fflush(stdout);
        std::fflush(stderr);
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
        auto* stream = (l >= LogLevel::Error) ? stderr : stdout;
        // Write each segment with fwrite to avoid `printf` width-int truncation
        // for messages longer than INT_MAX bytes. fwrite takes size_t natively.
        const auto tag = to_short_string(l);
        std::fputc('[', stream);
        std::fwrite(tag.data(), 1, tag.size(), stream);
        std::fputs("] ", stream);
        if (loc != nullptr)
        {
            // source_location::line() already returns std::uint_least32_t; pass it
            // straight to %u. (No static_cast — GCC -Wuseless-cast catches it.)
            std::fprintf(stream, "%s:%u ", loc->file_name(), loc->line());
        }
        std::fwrite(msg.data(), 1, msg.size(), stream);
        std::fputc('\n', stream);
        if (l >= LogLevel::Warning)
            std::fflush(stream);

        // Observers
        std::lock_guard guard { observers_lock_ };
        if (!observers_.empty())
        {
            LogRecord rec;
            rec.observed_at = std::chrono::steady_clock::now();
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
    std::atomic<LogLevel> level_;
    std::mutex observers_lock_;
    std::vector<ILogObserver*> observers_;
};

}  // namespace cd::log
