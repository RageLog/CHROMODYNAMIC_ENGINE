// =============================================================================
// CHROMODYNAMIC — cd/log/AuditTrail.hpp
// ADR-013 + ADR-017 P2 (DfH safety_monitor/auditlogger.hpp salvage)
//
// In-memory ring of the last N log records, captured for crash dumps and
// post-mortem analysis. Distinct from EventRecorder: AuditTrail is fed by the
// logger via an ILogObserver hook; EventRecorder records user events.
// =============================================================================
#pragma once

#include <cd/log/ILogger.hpp>
#include <cd/log/LogRecord.hpp>

#include <cstddef>
#include <deque>
#include <mutex>
#include <vector>

namespace cd::log
{

class AuditTrail final : public ILogObserver
{
public:
    explicit AuditTrail(std::size_t capacity = 256) noexcept
        : capacity_ { capacity }
    {
    }

    void on_log_record(const LogRecord& r) override
    {
        std::lock_guard guard { mutex_ };
        entries_.push_back(r);
        while (entries_.size() > capacity_)
            entries_.pop_front();
    }

    [[nodiscard]] std::vector<LogRecord> snapshot() const
    {
        std::lock_guard guard { mutex_ };
        return { entries_.begin(), entries_.end() };
    }

    [[nodiscard]] std::size_t size() const
    {
        std::lock_guard guard { mutex_ };
        return entries_.size();
    }

    [[nodiscard]] std::size_t capacity() const noexcept
    {
        return capacity_;
    }

    void clear() noexcept
    {
        std::lock_guard guard { mutex_ };
        entries_.clear();
    }

private:
    std::size_t capacity_;
    std::deque<LogRecord> entries_;
    mutable std::mutex mutex_;
};

}  // namespace cd::log
