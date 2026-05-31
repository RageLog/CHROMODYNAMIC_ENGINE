// =============================================================================
// CHROMODYNAMIC — cd/asset/texture_streamer/TextureStreamer.cpp
// Phase 599 — cd::asset::texture_streamer implementation (Sprint-1: synchronous)
//
// Sprint-1 strategy:
//   * On tick(), pick the highest-priority pending entry via O(n) scan.
//     (Sprint-2 will replace with a priority_queue for O(log n).)
//   * Attempt to allocate a minimal 1x1 RGBA8 GPU texture via IDevice::
//     create_texture() to represent "asset resident on GPU".
//   * Real decode pipeline (cdtex → pixel data → staged upload) is a future
//     Sprint deliverable.
//   * Failure (either from the device or a missing/undecodable file) results in
//     the entry being silently dropped — callers detect via is_loaded() staying
//     false.
// =============================================================================

#include <cd/asset/texture_streamer/TextureStreamer.hpp>

#include <cd/rhi/Descriptors.hpp>
#include <cd/rhi/Enums.hpp>
#include <cd/rhi/Format.hpp>

#include <algorithm>
#include <utility>

namespace cd::asset::texture_streamer
{

void TextureStreamer::enqueue(StreamRequest request)
{
    // Idempotent: already loaded → no-op.
    if (completed_paths_.contains(request.asset_path))
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

void TextureStreamer::cancel(std::string_view asset_path)
{
    // Only removes from the pending set — completed entries are immutable.
    pending_map_.erase(std::string{ asset_path });
}

void TextureStreamer::tick(float /*dt*/, cd::rhi::IDevice& device)
{
    if (pending_map_.empty())
    {
        return;
    }

    // Select the highest-priority pending entry (O(n), acceptable for Sprint-1).
    const auto best = std::max_element(
        pending_map_.cbegin(),
        pending_map_.cend(),
        [](const auto& lhs, const auto& rhs) noexcept {
            return lhs.second.priority < rhs.second.priority;
        });

    const std::string    path       = best->first;
    const std::uint8_t   mip_target = best->second.mip_target;
    pending_map_.erase(best);

    // Sprint-1: allocate a placeholder 1x1 GPU texture to represent residency.
    // The path is used for deduplication; actual pixel decode arrives in Sprint-2.
    cd::rhi::TextureDesc desc {};
    desc.type       = cd::rhi::TextureType::k2D;
    desc.format     = cd::rhi::Format::kRGBA8Unorm;
    desc.extent     = { 1U, 1U, 1U };
    desc.mip_levels = (mip_target == 0U) ? 1U : static_cast<std::uint32_t>(mip_target);
    desc.usage      = cd::rhi::TextureUsage::kSampled;
    desc.memory     = cd::rhi::MemoryUsage::kGpuOnly;

    auto result = device.create_texture(desc);
    if (!result.has_value())
    {
        // Device allocation failed — silently drop (path leaves pending + not
        // added to completed). Caller detects via is_loaded() returning false.
        return;
    }

    const cd::rhi::TextureHandle handle = *result;
    completed_.push_back(LoadedRecord{ handle, mip_target });
    completed_paths_.emplace(path, handle);
}

bool TextureStreamer::is_loaded(std::string_view asset_path) const
{
    return completed_paths_.contains(std::string{ asset_path });
}

std::optional<cd::rhi::TextureHandle>
TextureStreamer::get_loaded(std::string_view asset_path) const
{
    const auto it = completed_paths_.find(std::string{ asset_path });
    if (it == completed_paths_.cend())
    {
        return std::nullopt;
    }
    return it->second;
}

std::size_t TextureStreamer::pending_count() const noexcept
{
    return pending_map_.size();
}

std::size_t TextureStreamer::completed_count() const noexcept
{
    return completed_.size();
}

}  // namespace cd::asset::texture_streamer
