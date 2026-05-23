// =============================================================================
// CHROMODYNAMIC — cd/events/EventRecorder.hpp
// ADR-017 P1 (DfH common/event/eventrecorder.hpp salvage)
//
// In-memory ring buffer of recent events for replay / regression tests / crash
// diagnosis. Type-erased entries stored as user-provided `description` strings
// + monotonic timestamp + sequence id.
//
// Full JSONL persistence + rotation policy lives in cd::log JsonlBackend
// (Sprint S2.3+); this class is the in-memory front for the editor live view.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

namespace cd::events
{

struct RecordedEvent
{
    std::uint64_t sequence { 0 };
    std::chrono::steady_clock::time_point timestamp {};
    std::string type {};         // typically typeid(EventT).name() or a logical tag
    std::string description {};  // free-form caller-provided description
};

class EventRecorder
{
public:
    /// `capacity` is the maximum number of events retained. The oldest are
    /// discarded once exceeded (ring-style retention).
    explicit EventRecorder(std::size_t capacity = 1024) noexcept
        : capacity_ { capacity }
    {
    }

    EventRecorder(const EventRecorder&) = delete;
    EventRecorder& operator=(const EventRecorder&) = delete;
    EventRecorder(EventRecorder&&) = delete;
    EventRecorder& operator=(EventRecorder&&) = delete;

    void record(std::string type, std::string description)
    {
        std::lock_guard guard { mutex_ };
        RecordedEvent e;
        e.sequence = ++next_sequence_;
        e.timestamp = std::chrono::steady_clock::now();
        e.type = std::move(type);
        e.description = std::move(description);
        entries_.push_back(std::move(e));
        while (entries_.size() > capacity_)
            entries_.pop_front();
    }

    /// Snapshot of the current ring contents (chronological order).
    [[nodiscard]] std::vector<RecordedEvent> snapshot() const
    {
        std::lock_guard guard { mutex_ };
        return { entries_.begin(), entries_.end() };
    }

    [[nodiscard]] std::size_t size() const noexcept
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
    std::uint64_t next_sequence_ { 0 };
    std::deque<RecordedEvent> entries_;
    mutable std::mutex mutex_;
};

}  // namespace cd::events
