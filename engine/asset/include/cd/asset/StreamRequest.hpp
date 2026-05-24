// =============================================================================
// CHROMODYNAMIC — cd/asset/StreamRequest.hpp
// Phase 45.B / Wave 213 — priority queue request for async streaming.
//
// Open-world levels stream assets at runtime: textures, audio clips,
// gltf chunks. The streaming scheduler needs each pending request to
// carry:
//   * `id`      — what to load
//   * `priority`— small integer; HIGHER = more urgent
//   * `state`   — pending/inflight/complete/failed (for status query)
//   * `deadline`— optional frame-budget hint
//
// `StreamQueue` is a min-heap by negative priority (so std::priority_queue
// pops the most-urgent first). `pop()` returns the most-urgent pending
// request; `mark_inflight` / `mark_complete` update state in-place via
// the parallel `id → state` map.
// =============================================================================
#pragma once

#include <cd/asset/AssetId.hpp>
#include <cd/core/Defines.hpp>

#include <algorithm>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace cd::asset
{

enum class StreamState : std::uint8_t
{
    kPending   = 0,
    kInflight  = 1,
    kComplete  = 2,
    kFailed    = 3,
};

struct StreamRequest
{
    AssetId       id;
    std::int32_t  priority { 0 };   // higher = more urgent
    std::uint32_t deadline_frame { 0 };
};

class StreamQueue
{
public:
    void push(StreamRequest r)
    {
        states_[r.id.value()] = StreamState::kPending;
        heap_.push_back(r);
        std::push_heap(heap_.begin(), heap_.end(),
                       [](const StreamRequest& a, const StreamRequest& b)
                       { return a.priority < b.priority; });
    }

    [[nodiscard]] bool empty() const noexcept { return heap_.empty(); }

    [[nodiscard]] std::size_t size() const noexcept { return heap_.size(); }

    /// Pop the most-urgent pending request. Returns std::nullopt if
    /// the queue is empty.
    [[nodiscard]] StreamRequest pop_top()
    {
        std::pop_heap(heap_.begin(), heap_.end(),
                      [](const StreamRequest& a, const StreamRequest& b)
                      { return a.priority < b.priority; });
        auto r = heap_.back();
        heap_.pop_back();
        states_[r.id.value()] = StreamState::kInflight;
        return r;
    }

    void mark_complete(AssetId id) noexcept { states_[id.value()] = StreamState::kComplete; }
    void mark_failed(AssetId id) noexcept   { states_[id.value()] = StreamState::kFailed; }

    [[nodiscard]] StreamState state_of(AssetId id) const noexcept
    {
        auto it = states_.find(id.value());
        return (it != states_.end()) ? it->second : StreamState::kPending;
    }

private:
    std::vector<StreamRequest> heap_;
    std::unordered_map<std::uint64_t, StreamState> states_;
};

}  // namespace cd::asset
