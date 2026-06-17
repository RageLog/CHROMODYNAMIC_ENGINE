// =============================================================================
// CHROMODYNAMIC — cd/asset/texture_streamer/TextureStreamer.cpp
// Phase 599 — cd::asset::texture_streamer implementation (Sprint-1: synchronous)
// Phase 714 — opt-in async path via AsyncTexturePool
// Band 6   — real cdtex / image CPU decode wired behind the orchestration.
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
//   * Pick the highest-priority pending entry, run decode_texture_file().
//   * Create a GPU texture sized to the REAL decoded dimensions via
//     IDevice::create_texture(); record carries the real width/height.
//   * A failed decode silently drops the entry (is_loaded stays false).
//
// Async mode (use_async == true):
//   * AsyncTexturePool workers CPU-decode each request and push the decoded
//     payload back. tick() / join_pending() drain those on the owner thread
//     and create the GPU texture there (workers never touch IDevice).
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
    if (completed_index_.contains(request.asset_path))
    {
        return;
    }

    // Idempotent: already pending → no-op (do not update priority or mip_target).
    if (pending_map_.contains(request.asset_path))
    {
        return;
    }

    const std::uint8_t mip    = request.mip_target;
    const std::uint8_t prio   = request.priority;
    const std::string  path   = std::move(request.asset_path);

    pending_map_.emplace(path, PendingEntry{ path, mip, prio });
}

// ---------------------------------------------------------------------------
// cancel
// ---------------------------------------------------------------------------

void TextureStreamer::cancel(std::string_view asset_path)
{
    // Only removes from the local dedup set — completed entries are immutable.
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

    // Select the highest-priority pending entry (O(n), acceptable for Sprint-1).
    const auto best = std::ranges::max_element(
        pending_map_,
        [](const auto& lhs, const auto& rhs) noexcept {
            return lhs.second.priority < rhs.second.priority;
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

    const std::size_t idx = completed_.size();
    completed_.push_back(LoadedRecord{ *result, mip_target, decoded.width, decoded.height });
    completed_index_.emplace(path, idx);
}

// ---------------------------------------------------------------------------
// tick — async path implementation (Sprint-2)
// ---------------------------------------------------------------------------

void TextureStreamer::tick_async_impl()
{
    // Submit all pending requests to the worker pool.
    for (auto& [path, entry] : pending_map_)
    {
        async_pool_->submit_async(StreamRequest{ entry.path, entry.mip_target, entry.priority });
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
        if (completed_index_.contains(item.path))
        {
            continue;  // async pool may complete a path that was already cancelled
        }
        create_gpu_record(item.path, 0U, item.decoded, device);
    }
}

// ---------------------------------------------------------------------------
// tick (public)
// ---------------------------------------------------------------------------

void TextureStreamer::tick(float /*dt*/, cd::rhi::IDevice& device)
{
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
// Queries
// ---------------------------------------------------------------------------

bool TextureStreamer::is_loaded(std::string_view asset_path) const
{
    return completed_index_.contains(std::string{ asset_path });
}

std::optional<cd::rhi::TextureHandle>
TextureStreamer::get_loaded(std::string_view asset_path) const
{
    const auto it = completed_index_.find(std::string{ asset_path });
    if (it == completed_index_.cend())
    {
        return std::nullopt;
    }
    return completed_.at(it->second).handle;
}

std::optional<std::pair<std::uint32_t, std::uint32_t>>
TextureStreamer::get_dimensions(std::string_view asset_path) const
{
    const auto it = completed_index_.find(std::string{ asset_path });
    if (it == completed_index_.cend())
    {
        return std::nullopt;
    }
    const auto& rec = completed_.at(it->second);
    return std::pair<std::uint32_t, std::uint32_t>{ rec.width, rec.height };
}

std::size_t TextureStreamer::pending_count() const noexcept
{
    return pending_map_.size();
}

std::size_t TextureStreamer::completed_count() const noexcept
{
    // completed_index_ tracks what the owner thread has actually accepted +
    // created a GPU record for, in both sync and async modes.
    return completed_index_.size();
}

}  // namespace cd::asset::texture_streamer
