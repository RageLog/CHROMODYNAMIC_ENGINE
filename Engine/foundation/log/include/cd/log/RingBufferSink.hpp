// =============================================================================
// CHROMODYNAMIC — cd/log/RingBufferSink.hpp
// Phase 10 / Sprint 1 / Wave 110 — bounded in-memory log mirror for triage.
//
// A small, thread-safe ILogObserver that keeps the most recent N log
// records in a circular buffer. The intended consumer is a panic /
// crash handler: when the engine is about to die, flush the ring to
// the crash report so the last N lines of context survive even when
// a file sink hadn't flushed yet.
//
// Design:
//   * Bounded — never grows past `capacity`. Old records are dropped
//     once the ring wraps; this is exactly the triage behaviour you
//     want (you care about *recent* context).
//   * Owning copy — LogRecord stores std::string for message/file_path
//     anyway, so the sink simply moves a LogRecord into the slot.
//   * Mutex-guarded write/snapshot. Logging is not on the hot frame
//     path; a mutex is the right tool. snapshot() builds the
//     oldest-first chronological view by walking from `head_` forward.
//
// Why header-only: cd::log is an INTERFACE library; a TU here would
// pull a .cpp into the foundation layer for no gain.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <cd/log/ILogger.hpp>
#include <cd/log/LogRecord.hpp>

#include <cstddef>
#include <mutex>
#include <utility>
#include <vector>

namespace cd::log
{

class RingBufferSink final : public ILogObserver
{
public:
    /// Construct with a fixed capacity (records). Capacity is clamped
    /// to at least 1; passing 0 would otherwise make every push silently
    /// drop, which is a footgun.
    explicit RingBufferSink(std::size_t capacity) noexcept
        : capacity_ { capacity == 0U ? 1U : capacity }
    {
        slots_.reserve(capacity_);
    }

    void on_log_record(const LogRecord& record) override
    {
        std::lock_guard lk { mu_ };
        if (slots_.size() < capacity_)
        {
            slots_.push_back(record);
            return;
        }
        slots_[head_] = record;
        head_ = (head_ + 1U) % capacity_;
        wrapped_ = true;
    }

    /// Snapshot of the buffer, oldest record first.
    [[nodiscard]] std::vector<LogRecord> snapshot() const
    {
        std::lock_guard lk { mu_ };
        std::vector<LogRecord> out;
        out.reserve(slots_.size());
        if (!wrapped_)
        {
            out.insert(out.end(), slots_.begin(), slots_.end());
            return out;
        }
        // After wrapping, the oldest record is the one head_ points at.
        for (std::size_t i = 0; i < capacity_; ++i)
        {
            const std::size_t idx = (head_ + i) % capacity_;
            out.push_back(slots_[idx]);
        }
        return out;
    }

    [[nodiscard]] std::size_t size() const noexcept
    {
        std::lock_guard lk { mu_ };
        return slots_.size();
    }

    [[nodiscard]] std::size_t capacity() const noexcept
    {
        return capacity_;
    }

    [[nodiscard]] bool wrapped() const noexcept
    {
        std::lock_guard lk { mu_ };
        return wrapped_;
    }

    void clear() noexcept
    {
        std::lock_guard lk { mu_ };
        slots_.clear();
        head_ = 0;
        wrapped_ = false;
    }

private:
    mutable std::mutex mu_;
    std::vector<LogRecord> slots_;
    std::size_t capacity_;
    std::size_t head_ { 0 };
    bool wrapped_ { false };
};

}  // namespace cd::log
