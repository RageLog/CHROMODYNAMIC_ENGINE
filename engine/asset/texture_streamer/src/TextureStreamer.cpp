// =============================================================================
// CHROMODYNAMIC — cd/asset/texture_streamer/TextureStreamer.cpp
// Phase 599 — cd::asset::texture_streamer implementation (Sprint-1: synchronous)
// Phase 714 — opt-in async path via AsyncTexturePool
// Band 6   — real cdtex / image CPU decode wired behind the orchestration.
// Phase 1268 — CPU charter complete: mip residency, LRU/budget eviction,
//              screen-coverage priority weighting, mip-upgrade re-enqueue.
//
// Decode dispatch (decode_texture_file):
//   * ".cdtex" → cd::asset::cdtex::load → DecodedTexture { block-compressed,
//     blocks = mip0 BC7 bytes, width/height from the file header }. This is
//     the engine's cooked texture format and the named Band-6 gap.
//   * ".png"/".jpg"/... → SEALED (returns nullopt). cd::asset_image vendors its
//     own STB_IMAGE_IMPLEMENTATION which collides with cd::asset_gltf's copy
//     when streamer_pool links texture+scene streamers in one executable;
//     wiring it needs a shared single-stb-TU first. See ADR-20260616-band6.
//   * decode/IO failure → std::nullopt (entry silently dropped, as before).
//
// Sync mode (use_async == false, default):
//   * Pick the highest-priority pending entry (effective_priority = priority +
//     screen_coverage * 255.0F), run decode_texture_file().
//   * Create a GPU texture sized to the REAL decoded dimensions via
//     IDevice::create_texture(); record carries the real width/height.
//   * After creating, enforce budget: evict LRU if over budget.
//   * A failed decode silently drops the entry (is_loaded stays false).
//
// Async mode (use_async == true):
//   * AsyncTexturePool workers CPU-decode each request and push the decoded
//     payload back. tick() / join_pending() drain those on the owner thread
//     and create the GPU texture there (workers never touch IDevice).
//
// LRU/Budget eviction:
//   loaded_map_ owns all resident records. lru_order_ (std::list<std::string>)
//   tracks MRU→LRU order; lru_iter_ maps path → list iterator for O(1) splice.
//   touch_impl() moves an entry to front. evict_lru_impl() pops from back.
//   enforce_budget() calls evict_lru_impl() in a loop until under budget.
// =============================================================================

#include <cd/asset/texture_streamer/TextureStreamer.hpp>

#include <cd/asset/cdtex/CdTex.hpp>

#include <cd/rhi/Descriptors.hpp>
#include <cd/rhi/Enums.hpp>
#include <cd/rhi/Format.hpp>

#include <algorithm>
#include <string>
#include <utility>

namespace cd::asset::texture_streamer
{

// ---------------------------------------------------------------------------
// decode_texture_file — real CPU decode (shared by sync + worker paths)
// ---------------------------------------------------------------------------

std::optional<DecodedTexture> decode_texture_file(std::string_view path)
{
    // Only the engine's cooked .cdtex BC7 format is decoded here (the named
    // Band-6 gap). PNG/JPG via cd::asset_image is SEALED — its vendored stb
    // copy collides with cd::asset_gltf's in the streamer_pool link closure;
    // see ADR-20260616-band6 for the trigger to lift the seal.
    const bool is_cdtex = path.size() >= 6U && path.substr(path.size() - 6U) == ".cdtex";
    if (!is_cdtex)
    {
        return std::nullopt;
    }

    auto r = cd::asset::cdtex::load(path);
    if (!r.has_value() || r->mips.empty())
    {
        return std::nullopt;
    }
    DecodedTexture out;
    out.width               = r->width;
    out.height              = r->height;
    out.is_block_compressed = true;
    out.blocks              = std::move(r->mips.front().blocks);
    return out;
}

// ---------------------------------------------------------------------------
// Constructor / Destructor
// ---------------------------------------------------------------------------

TextureStreamer::TextureStreamer(TextureStreamerConfig cfg)
    : config_ { cfg }
{
    if (config_.use_async)
    {
        async_pool_ = std::make_unique<AsyncTexturePool>();
        async_pool_->configure(config_.worker_count);
    }
}

TextureStreamer::~TextureStreamer() = default;

// ---------------------------------------------------------------------------
// enqueue
// ---------------------------------------------------------------------------

void TextureStreamer::enqueue(StreamRequest request)
{
    // Idempotent: already loaded → no-op.
    if (loaded_map_.contains(request.asset_path))
    {
        return;
    }

    // Idempotent: already pending → no-op (do not update priority or mip_target).
    if (pending_map_.contains(request.asset_path))
    {
        return;
    }

    const std::uint8_t mip      = request.mip_target;
    const std::uint8_t prio     = request.priority;
    const float        coverage = request.screen_coverage;
    const std::string  path     = std::move(request.asset_path);

    pending_map_.emplace(path, PendingEntry{ path, mip, prio, coverage });
}

// ---------------------------------------------------------------------------
// enqueue_mip_upgrade
// ---------------------------------------------------------------------------

void TextureStreamer::enqueue_mip_upgrade(StreamRequest request)
{
    const auto it = loaded_map_.find(request.asset_path);
    if (it == loaded_map_.end())
    {
        // Not currently loaded — plain enqueue semantics.
        enqueue(std::move(request));
        return;
    }

    // Only upgrade if the new mip_target is strictly higher than what is resident.
    // mip_target == 0 means "base mip only" (== 1 level), so treat 0 as 1 for
    // comparison: effectively_loaded = max(1, current_mip_target).
    const std::uint8_t current = it->second.mip_target;
    const std::uint8_t wanted  = request.mip_target;

    // If the caller wants fewer (or equal) mip levels than already loaded, no-op.
    if (wanted <= current || wanted == 0U)
    {
        return;
    }

    // Evict the stale record so the path re-enters the pending queue.
    // This method has no device reference (to keep its public signature), so we
    // cannot destroy the old GPU handle inline. Instead we hand it to the
    // deferred-destroy queue, which the next tick() drains with a real device —
    // the handle is therefore NOT leaked at the RHI layer.
    pending_destroy_.push_back(it->second.handle);
    lru_iter_.erase(request.asset_path);
    lru_order_.remove(request.asset_path);  // O(n) acceptable for upgrade path
    loaded_map_.erase(it);

    // Now re-enqueue.
    const std::uint8_t mip      = wanted;
    const std::uint8_t prio     = request.priority;
    const float        coverage = request.screen_coverage;
    const std::string  path     = std::move(request.asset_path);
    pending_map_.emplace(path, PendingEntry{ path, mip, prio, coverage });
}

// ---------------------------------------------------------------------------
// cancel
// ---------------------------------------------------------------------------

void TextureStreamer::cancel(std::string_view asset_path)
{
    // Only removes from the local dedup set — loaded entries are immutable.
    // In async mode a request already in the pool will still complete; its
    // result is accepted on the next poll in tick().
    pending_map_.erase(std::string{ asset_path });
}

// ---------------------------------------------------------------------------
// tick — sync path implementation (Sprint-1 default)
// ---------------------------------------------------------------------------

void TextureStreamer::tick_sync_impl(cd::rhi::IDevice& device)
{
    if (pending_map_.empty())
    {
        return;
    }

    // Select the highest-priority pending entry.
    // Effective priority = discrete priority byte + screen_coverage * 255,
    // giving continuous screen-coverage weighting within each priority bucket.
    // (O(n), acceptable for Sprint-1 sync path.)
    const auto best = std::ranges::max_element(
        pending_map_,
        [](const auto& lhs, const auto& rhs) noexcept {
            const float el = static_cast<float>(lhs.second.priority) +
                             lhs.second.screen_coverage * 255.0F;
            const float er = static_cast<float>(rhs.second.priority) +
                             rhs.second.screen_coverage * 255.0F;
            return el < er;
        });

    const std::string  path       = best->first;
    const std::uint8_t mip_target = best->second.mip_target;
    pending_map_.erase(best);

    // Real CPU decode — cdtex BC7 or stb image, sized from the file.
    auto decoded = decode_texture_file(path);
    if (!decoded.has_value())
    {
        // Decode / IO failure — silently drop (is_loaded stays false).
        return;
    }

    create_gpu_record(path, mip_target, *decoded, device);
}

// ---------------------------------------------------------------------------
// create_gpu_record (private helper) — owner-thread GPU texture creation
// ---------------------------------------------------------------------------

void TextureStreamer::create_gpu_record(const std::string&    path,
                                        std::uint8_t          mip_target,
                                        const DecodedTexture& decoded,
                                        cd::rhi::IDevice&     device)
{
    // Enforce budget BEFORE allocating the new handle so that, if the pool is
    // already at capacity, we free one slot first.
    enforce_budget(device);

    // GPU texture sized to the REAL decoded dimensions. Block-compressed cdtex
    // payloads upload as BC7; image payloads as RGBA8. (The actual staged copy
    // of `decoded.blocks` / `decoded.rgba` is the renderer-consumer's job; the
    // streamer guarantees a correctly-sized resident handle + real metadata.)
    cd::rhi::TextureDesc desc {};
    desc.type       = cd::rhi::TextureType::k2D;
    desc.format     = decoded.is_block_compressed ? cd::rhi::Format::kBC7Unorm
                                                  : cd::rhi::Format::kRGBA8Unorm;
    desc.extent     = { decoded.width, decoded.height, 1U };
    desc.mip_levels = (mip_target == 0U) ? 1U : static_cast<std::uint32_t>(mip_target);
    desc.usage      = cd::rhi::TextureUsage::kSampled;
    desc.memory     = cd::rhi::MemoryUsage::kGpuOnly;

    auto result = device.create_texture(desc);
    if (!result.has_value())
    {
        // Device allocation failed — silently drop.
        return;
    }

    loaded_map_.emplace(path, LoadedRecord{ *result, mip_target, decoded.width, decoded.height });

    // Insert at MRU (front) of the LRU list.
    lru_order_.push_front(path);
    lru_iter_.emplace(path, lru_order_.begin());

    ++completed_count_;
}

// ---------------------------------------------------------------------------
// tick — async path implementation (Sprint-2)
// ---------------------------------------------------------------------------

void TextureStreamer::tick_async_impl()
{
    // Submit all pending requests to the worker pool.
    for (auto& [path, entry] : pending_map_)
    {
        async_pool_->submit_async(
            StreamRequest{ entry.path, entry.mip_target, entry.priority, entry.screen_coverage });
    }
    pending_map_.clear();
}

// ---------------------------------------------------------------------------
// accept_async_completions (private helper)
// ---------------------------------------------------------------------------

void TextureStreamer::accept_async_completions(std::vector<CompletedTexture> done,
                                               cd::rhi::IDevice&             device)
{
    // Each completion already carries the REAL decoded payload from a worker.
    // We create the GPU texture here on the owner thread (workers never touch
    // IDevice) sized to the decoded dimensions.
    for (auto& item : done)
    {
        if (loaded_map_.contains(item.path))
        {
            continue;  // async pool may complete a path that was already loaded
        }
        create_gpu_record(item.path, item.mip_target, item.decoded, device);
    }
}

// ---------------------------------------------------------------------------
// tick (public)
// ---------------------------------------------------------------------------

void TextureStreamer::tick(float /*dt*/, cd::rhi::IDevice& device)
{
    // Destroy any handles orphaned by enqueue_mip_upgrade() before any new
    // work — keeps the upgrade path leak-free without a device parameter.
    drain_pending_destroy(device);

    if (config_.use_async)
    {
        last_device_ = &device;

        // Submit all pending requests for background CPU decode.
        tick_async_impl();

        // Drain whatever workers have already decoded; upload on this thread.
        auto done = async_pool_->poll_completed();
        accept_async_completions(std::move(done), device);
    }
    else
    {
        tick_sync_impl(device);
    }
}

// ---------------------------------------------------------------------------
// join_pending
// ---------------------------------------------------------------------------

void TextureStreamer::join_pending()
{
    if (!config_.use_async || !async_pool_ || last_device_ == nullptr)
    {
        return;
    }
    async_pool_->join_all();

    // Drain any remaining decoded textures into our table (reuse the device
    // captured by the most recent tick()).
    auto done = async_pool_->poll_completed();
    accept_async_completions(std::move(done), *last_device_);
}

// ---------------------------------------------------------------------------
// LRU helpers
// ---------------------------------------------------------------------------

void TextureStreamer::touch_impl(const std::string& path)
{
    const auto it = lru_iter_.find(path);
    if (it == lru_iter_.end())
    {
        return;  // not resident
    }
    // Splice to front (O(1) — no allocation).
    lru_order_.splice(lru_order_.begin(), lru_order_, it->second);
}

std::string TextureStreamer::evict_lru_impl(cd::rhi::IDevice& device)
{
    if (lru_order_.empty())
    {
        return {};
    }
    const std::string evicted = lru_order_.back();
    lru_order_.pop_back();

    const auto li = lru_iter_.find(evicted);
    if (li != lru_iter_.end())
    {
        lru_iter_.erase(li);
    }

    const auto mi = loaded_map_.find(evicted);
    if (mi != loaded_map_.end())
    {
        device.destroy_texture(mi->second.handle);
        loaded_map_.erase(mi);
    }

    return evicted;
}

void TextureStreamer::enforce_budget(cd::rhi::IDevice& device)
{
    if (texture_budget_ == 0U)
    {
        return;  // unlimited
    }
    while (loaded_map_.size() >= texture_budget_)
    {
        if (evict_lru_impl(device).empty())
        {
            break;  // nothing to evict
        }
    }
}

void TextureStreamer::drain_pending_destroy(cd::rhi::IDevice& device)
{
    for (const auto handle : pending_destroy_)
    {
        device.destroy_texture(handle);
    }
    pending_destroy_.clear();
}

// ---------------------------------------------------------------------------
// Budget / eviction — public API
// ---------------------------------------------------------------------------

void TextureStreamer::set_texture_budget(std::size_t max_loaded) noexcept
{
    texture_budget_ = max_loaded;
}

std::string TextureStreamer::evict_lru(cd::rhi::IDevice& device)
{
    return evict_lru_impl(device);
}

void TextureStreamer::touch(std::string_view asset_path)
{
    touch_impl(std::string{ asset_path });
}

// ---------------------------------------------------------------------------
// Queries
// ---------------------------------------------------------------------------

bool TextureStreamer::is_loaded(std::string_view asset_path) const
{
    return loaded_map_.contains(std::string{ asset_path });
}

std::optional<cd::rhi::TextureHandle>
TextureStreamer::get_loaded(std::string_view asset_path) const
{
    const auto it = loaded_map_.find(std::string{ asset_path });
    if (it == loaded_map_.cend())
    {
        return std::nullopt;
    }
    return it->second.handle;
}

std::optional<std::pair<std::uint32_t, std::uint32_t>>
TextureStreamer::get_dimensions(std::string_view asset_path) const
{
    const auto it = loaded_map_.find(std::string{ asset_path });
    if (it == loaded_map_.cend())
    {
        return std::nullopt;
    }
    return std::pair<std::uint32_t, std::uint32_t>{ it->second.width, it->second.height };
}

bool TextureStreamer::is_mip_loaded(std::string_view asset_path,
                                    std::uint8_t      mip_level) const
{
    const auto it = loaded_map_.find(std::string{ asset_path });
    if (it == loaded_map_.cend())
    {
        return false;
    }
    const std::uint8_t mt = it->second.mip_target;
    // mip_target == 0 means "base mip only" (1 level → only level 0 is resident).
    if (mt == 0U)
    {
        return mip_level == 0U;
    }
    // mip_target N means N levels: levels 0..(N-1) are resident.
    return mip_level < mt;
}

std::optional<std::uint8_t>
TextureStreamer::resident_mip_target(std::string_view asset_path) const
{
    const auto it = loaded_map_.find(std::string{ asset_path });
    if (it == loaded_map_.cend())
    {
        return std::nullopt;
    }
    return it->second.mip_target;
}

std::size_t TextureStreamer::texture_budget() const noexcept
{
    return texture_budget_;
}

std::size_t TextureStreamer::pending_count() const noexcept
{
    return pending_map_.size();
}

std::size_t TextureStreamer::completed_count() const noexcept
{
    // Cumulative uploads since construction — NOT decremented by eviction.
    return completed_count_;
}

std::size_t TextureStreamer::loaded_count() const noexcept
{
    return loaded_map_.size();
}

}  // namespace cd::asset::texture_streamer
