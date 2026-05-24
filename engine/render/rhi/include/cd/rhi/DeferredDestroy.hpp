// =============================================================================
// CHROMODYNAMIC — cd/rhi/DeferredDestroy.hpp
// Phase 131 / Wave 293 — frame-fence-keyed deferred destroy queue.
//
// Several future cmd-buffer features (transient scratch buffers for
// acceleration-structure builds, staging buffers for uploads issued
// inline, query pools released after readback) need to outlive the
// command buffer that records them but die once the GPU has finished
// with that frame. Vulkan's documented pattern is "track the frame
// fence at submit, drop the resource only after the fence completes".
//
// This header provides the cross-backend type — a queue of
// `(frame_id, destroy_fn)` pairs. The renderer's per-frame epilogue
// (after the frame's wait/reset) walks the queue and invokes
// callbacks whose `frame_id <= last_completed_frame`.
//
// Header-only because it's a tiny std::deque wrapper with two
// callbacks. Backend code constructs `DeferredDestroy` once on the
// device, all command buffers push into it via a reference.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>

#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <utility>

namespace cd::rhi
{

class DeferredDestroy
{
public:
    using FrameId = std::uint64_t;
    using Action  = std::function<void()>;

    /// Queue `action` to run when the renderer reports
    /// `flush_completed(f >= retire_after_frame)`.
    void enqueue(FrameId retire_after_frame, Action action)
    {
        queue_.push_back(Entry { retire_after_frame, std::move(action) });
    }

    /// Run + drop every queued action whose frame_id is <=
    /// `last_completed_frame`. Returns the number of actions fired.
    std::size_t flush_completed(FrameId last_completed_frame)
    {
        std::size_t fired = 0;
        // Queue is appended in monotonic frame order in typical use
        // (every submit pushes the same frame_id); but cross-frame
        // pushes interleave possible. Walk + drop in place rather
        // than assuming sortedness.
        for (auto it = queue_.begin(); it != queue_.end(); )
        {
            if (it->frame_id <= last_completed_frame)
            {
                if (it->action) it->action();
                it = queue_.erase(it);
                ++fired;
            }
            else
            {
                ++it;
            }
        }
        return fired;
    }

    /// Flush everything immediately — used at device shutdown when the
    /// GPU has already idled.
    std::size_t flush_all()
    {
        std::size_t fired = 0;
        for (auto& e : queue_)
        {
            if (e.action) e.action();
            ++fired;
        }
        queue_.clear();
        return fired;
    }

    [[nodiscard]] std::size_t pending() const noexcept { return queue_.size(); }

private:
    struct Entry
    {
        FrameId frame_id { 0 };
        Action  action;
    };
    std::deque<Entry> queue_;
};

}  // namespace cd::rhi
