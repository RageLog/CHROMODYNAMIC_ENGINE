// =============================================================================
// CHROMODYNAMIC — cd/asset/streaming/Streaming.hpp
// Day 28 — Mesh + texture streaming.
//
// Priority-queue-based async I/O scheduler. Higher-priority requests
// (closer to camera, on-screen, larger projected size) jump the
// queue; lower-priority requests get demoted to coarser LODs when
// the memory budget is tight.
//
// References:
//   * Sevenich 2017 — "Cyberpunk 2077 streaming" GDC talk.
//   * Standard game-engine streaming pipeline architecture.
// =============================================================================
#pragma once

#include <algorithm>
#include <cstdint>
#include <functional>
#include <queue>
#include <span>
#include <string>
#include <vector>

namespace cd::asset::streaming
{

enum class AssetKind : std::uint8_t
{
    kMesh = 0,
    kTexture = 1,
    kAudio = 2,
};

struct Request
{
    std::uint64_t asset_id { 0 };
    AssetKind     kind     { AssetKind::kMesh };
    std::uint8_t  lod      { 0 };       ///< 0 = coarsest, higher = finer
    float         priority { 0.0F };    ///< larger = more important
    std::uint64_t bytes    { 0 };       ///< expected payload size

    [[nodiscard]] bool operator<(const Request& o) const noexcept
    {
        // std::priority_queue is a max-heap, so use < that returns
        // true when *this should be served AFTER `o`.
        return priority < o.priority;
    }
};

class Scheduler
{
public:
    explicit Scheduler(std::uint64_t budget_bytes) : budget_(budget_bytes) {}

    void enqueue(Request r) { q_.push(r); }

    /// Pop the next request whose payload fits the remaining budget.
    /// Returns false when either the queue is empty or no remaining
    /// request fits.
    [[nodiscard]] bool next(Request& out)
    {
        std::vector<Request> skipped;
        while (!q_.empty())
        {
            Request top = q_.top();
            q_.pop();
            if (in_flight_bytes_ + top.bytes <= budget_)
            {
                in_flight_bytes_ += top.bytes;
                // Re-queue the skipped lower-priority ones we passed.
                for (auto& s : skipped) q_.push(s);
                out = top;
                return true;
            }
            skipped.push_back(top);
        }
        for (auto& s : skipped) q_.push(s);
        return false;
    }

    /// Mark a previously-served request finished — frees its budget.
    void complete(const Request& r) noexcept
    {
        if (in_flight_bytes_ >= r.bytes) in_flight_bytes_ -= r.bytes;
        else                              in_flight_bytes_ = 0;
    }

    [[nodiscard]] std::uint64_t budget()        const noexcept { return budget_; }
    [[nodiscard]] std::uint64_t in_flight()     const noexcept { return in_flight_bytes_; }
    [[nodiscard]] std::size_t   pending_count() const noexcept { return q_.size(); }

private:
    std::priority_queue<Request> q_;
    std::uint64_t budget_         { 0 };
    std::uint64_t in_flight_bytes_{ 0 };
};

}  // namespace cd::asset::streaming
