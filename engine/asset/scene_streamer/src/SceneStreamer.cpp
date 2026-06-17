// =============================================================================
// CHROMODYNAMIC — cd/asset/scene_streamer/SceneStreamer.cpp
// Phase 586 — cd::asset::scene_streamer implementation (Sprint-1: synchronous)
// Phase 755 — opt-in async path via AsyncScenePool
// Band 6   — real glTF parse wired into BOTH the sync and async paths.
//
// Sync mode (use_async == false, default):
//   * On tick(), pick the highest-priority pending entry via O(n) scan.
//   * Parse via cd::asset::gltf::load_scene(); failure → silently dropped.
//
// Async mode (use_async == true):
//   * AsyncScenePool workers run cd::asset::gltf::load_scene() per file and
//     hand back a real owning LoadedScene.
//   * tick() submits all pending requests to the pool, then drains the
//     completed scenes into completed_ (real LoadedScene data, real SceneIds).
//   * GPU/ECS ingest of the LoadedScene remains the render-side consumer's job.
// =============================================================================

#include <cd/asset/scene_streamer/SceneStreamer.hpp>

#include <algorithm>
#include <utility>

namespace cd::asset::scene_streamer
{

// ---------------------------------------------------------------------------
// Constructor / Destructor
// ---------------------------------------------------------------------------

SceneStreamer::SceneStreamer(SceneStreamerConfig cfg)
    : config_ { cfg }
{
    if (config_.use_async)
    {
        async_pool_ = std::make_unique<AsyncScenePool>();
        async_pool_->configure(config_.worker_count);
    }
}

SceneStreamer::~SceneStreamer() = default;

// ---------------------------------------------------------------------------
// enqueue
// ---------------------------------------------------------------------------

void SceneStreamer::enqueue(StreamRequest request)
{
    // Idempotent: already loaded → no-op.
    if (completed_paths_.contains(request.asset_path))
    {
        return;
    }

    // Idempotent: already pending → no-op (do not update priority).
    if (pending_map_.contains(request.asset_path))
    {
        return;
    }

    const std::uint8_t prio = request.priority;
    const std::string  path = std::move(request.asset_path);

    pending_map_.emplace(path, PendingEntry{ path, prio });
}

// ---------------------------------------------------------------------------
// cancel
// ---------------------------------------------------------------------------

void SceneStreamer::cancel(std::string_view asset_path)
{
    // Only removes from the local dedup set — completed entries are immutable.
    // In async mode a request already in the pool will still complete; its
    // result is accepted on the next poll in tick().
    pending_map_.erase(std::string{ asset_path });
}

// ---------------------------------------------------------------------------
// tick — sync path implementation (Sprint-1 default)
// ---------------------------------------------------------------------------

void SceneStreamer::tick_sync_impl()
{
    if (pending_map_.empty())
    {
        return;
    }

    // Select the highest-priority pending entry.
    // std::max_element over an unordered_map range — O(n) per tick,
    // acceptable for Sprint-1. Sprint-2 uses the worker pool.
    const auto best = std::ranges::max_element(
        pending_map_,
        [](const auto& lhs, const auto& rhs) noexcept {
            return lhs.second.priority < rhs.second.priority;
        });

    const std::string path = best->first;
    pending_map_.erase(best);

    // Synchronous load.
    auto result = cd::asset::gltf::load_scene(path);
    if (!result.has_value())
    {
        // Failed load: silently dropped (path not in pending, not in
        // completed). Callers can detect via is_loaded() remaining false.
        return;
    }

    const auto new_id = SceneId{ static_cast<std::uint32_t>(completed_.size()) };
    completed_.push_back(LoadedRecord{ std::move(*result), new_id });
    completed_paths_.emplace(path, new_id);
}

// ---------------------------------------------------------------------------
// tick — async path implementation (Sprint-2)
// ---------------------------------------------------------------------------

void SceneStreamer::tick_async_impl()
{
    // Submit all pending requests to the worker pool.
    for (auto& [path, entry] : pending_map_)
    {
        async_pool_->submit_async(StreamRequest{ entry.path, entry.priority });
    }
    pending_map_.clear();
}

// ---------------------------------------------------------------------------
// accept_async_completions (private helper)
// ---------------------------------------------------------------------------

void SceneStreamer::accept_async_completions(std::vector<CompletedScene> done)
{
    // Each completion already carries a fully-parsed LoadedScene from a worker.
    // We register the real data + a real SceneId on the owner thread (the GPU/
    // ECS ingest of the scene is the render-side consumer's job).
    for (auto& item : done)
    {
        if (completed_paths_.contains(item.path))
        {
            continue;  // async pool may complete a path that was already cancelled
        }
        const auto new_id = SceneId{ static_cast<std::uint32_t>(completed_.size()) };
        completed_.push_back(LoadedRecord{ std::move(item.scene), new_id });
        completed_paths_.emplace(item.path, new_id);
    }
}

// ---------------------------------------------------------------------------
// tick (public)
// ---------------------------------------------------------------------------

void SceneStreamer::tick(float /*dt*/)
{
    if (config_.use_async)
    {
        // Submit all pending requests.
        tick_async_impl();

        // Drain whatever has already completed on worker threads.
        auto done = async_pool_->poll_completed();
        accept_async_completions(std::move(done));
    }
    else
    {
        tick_sync_impl();
    }
}

// ---------------------------------------------------------------------------
// join_pending
// ---------------------------------------------------------------------------

void SceneStreamer::join_pending()
{
    if (!config_.use_async || !async_pool_)
    {
        return;
    }
    async_pool_->join_all();

    // Drain any remaining completed paths into our table.
    auto done = async_pool_->poll_completed();
    accept_async_completions(std::move(done));
}

// ---------------------------------------------------------------------------
// Queries
// ---------------------------------------------------------------------------

bool SceneStreamer::is_loaded(std::string_view asset_path) const
{
    return completed_paths_.contains(std::string{ asset_path });
}

std::optional<SceneId>
SceneStreamer::get_loaded(std::string_view asset_path) const
{
    const auto it = completed_paths_.find(std::string{ asset_path });
    if (it == completed_paths_.cend())
    {
        return std::nullopt;
    }
    return it->second;
}

const cd::asset::gltf::LoadedScene*
SceneStreamer::get_scene(std::string_view asset_path) const
{
    const auto it = completed_paths_.find(std::string{ asset_path });
    if (it == completed_paths_.cend())
    {
        return nullptr;
    }
    return &completed_.at(it->second.index).scene;
}

std::size_t SceneStreamer::pending_count() const noexcept
{
    return pending_map_.size();
}

std::size_t SceneStreamer::completed_count() const noexcept
{
    if (config_.use_async && async_pool_)
    {
        // In async mode, completed_ may lag behind the pool's counter until
        // the next poll; report completed_paths_ size for consistency with
        // what the owner thread can actually observe.
        return completed_paths_.size();
    }
    return completed_.size();
}

}  // namespace cd::asset::scene_streamer
