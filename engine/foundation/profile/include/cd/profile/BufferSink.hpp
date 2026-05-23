// =============================================================================
// CHROMODYNAMIC — cd/profile/BufferSink.hpp
//
// In-memory ring buffer sink. Holds the last N samples; an ImGui HUD,
// CSV exporter, or unit test pulls them out via `snapshot()`. Cheap
// enough to be the default sink in development builds.
// =============================================================================
#pragma once

#include <cd/profile/Scope.hpp>

#include <cstddef>
#include <mutex>
#include <vector>

namespace cd::profile
{

class BufferSink final : public ISink
{
public:
    /// `capacity` is the max number of samples kept; older entries are
    /// dropped FIFO. 4096 is a comfortable default for a 60 Hz frame loop
    /// with ~60 scopes per frame (≈ 1 minute of history).
    explicit BufferSink(std::size_t capacity = 4096) noexcept
        : capacity_ { capacity == 0 ? 1U : capacity }
    {
    }

    void submit(const Sample& s) noexcept override
    {
        const std::lock_guard<std::mutex> lock { mutex_ };
        if (samples_.size() < capacity_)
        {
            samples_.push_back(s);
        }
        else
        {
            samples_[next_] = s;
            next_ = (next_ + 1) % capacity_;
        }
    }

    /// Returns a copy of the buffered samples in oldest-to-newest order.
    /// Safe to call from any thread; under contention the snapshot is a
    /// consistent point-in-time view.
    [[nodiscard]] std::vector<Sample> snapshot() const
    {
        const std::lock_guard<std::mutex> lock { mutex_ };
        if (samples_.size() < capacity_)
            return samples_;
        // Ring is full; rotate so the oldest sample comes first.
        std::vector<Sample> out;
        out.reserve(samples_.size());
        out.insert(out.end(), samples_.begin() + static_cast<std::ptrdiff_t>(next_), samples_.end());
        out.insert(out.end(), samples_.begin(), samples_.begin() + static_cast<std::ptrdiff_t>(next_));
        return out;
    }

    /// Drop all buffered samples without resetting capacity.
    void clear()
    {
        const std::lock_guard<std::mutex> lock { mutex_ };
        samples_.clear();
        next_ = 0;
    }

    [[nodiscard]] std::size_t size() const
    {
        const std::lock_guard<std::mutex> lock { mutex_ };
        return samples_.size();
    }

    [[nodiscard]] std::size_t capacity() const noexcept
    {
        return capacity_;
    }

private:
    mutable std::mutex mutex_;
    std::vector<Sample> samples_;
    std::size_t capacity_;
    std::size_t next_ { 0 };
};

}  // namespace cd::profile
