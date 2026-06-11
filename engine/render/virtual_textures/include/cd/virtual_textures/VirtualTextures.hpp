// =============================================================================
// CHROMODYNAMIC — cd/virtual_textures/VirtualTextures.hpp
// Day 27 — Virtual textures (Mittring 2008 + Hollander 2013).
//
// Page-allocator + feedback-buffer pipeline:
//   * `PageId` = (virtual page x, virtual page y, mip).
//   * `PageTable` maps virtual page -> physical-atlas slot.
//   * GPU writes "I want this page" requests into a feedback buffer
//     during the shading pass; the CPU consumer reads them, transcodes
//     and uploads new pages into the atlas, and updates the page
//     table for the next frame.
//
// References:
//   * Mittring 2008 — "Advanced Virtual Texture Topics" (Crytek).
//   * Hollander 2013 — "Software Virtual Textures" (id Tech).
// =============================================================================
#pragma once

#include <cd/math/Vector.hpp>

#include <algorithm>
#include <cstdint>
#include <span>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace cd::virtual_textures
{

struct PageId
{
    std::uint16_t x  { 0 };
    std::uint16_t y  { 0 };
    std::uint8_t  mip{ 0 };

    [[nodiscard]] bool operator==(const PageId& o) const noexcept
    {
        return x == o.x && y == o.y && mip == o.mip;
    }
};

struct PageIdHash
{
    [[nodiscard]] std::size_t operator()(const PageId& p) const noexcept
    {
        return (std::size_t { p.x } << 24) ^
               (std::size_t { p.y } <<  8) ^
               std::size_t { p.mip };
    }
};

/// One slot in the physical atlas. `~0u` = invalid (page not resident).
struct AtlasSlot
{
    std::uint16_t slot_x { 0 };
    std::uint16_t slot_y { 0 };
    std::uint32_t valid  { 0 };  // 0 = empty, 1 = resident
};

class PageTable
{
public:
    /// Total physical slots = atlas_w_slots * atlas_h_slots.
    PageTable(std::uint16_t atlas_w_slots, std::uint16_t atlas_h_slots)
        : atlas_w_(atlas_w_slots), atlas_h_(atlas_h_slots) {}

    /// Look up a virtual page; returns nullptr when not resident.
    [[nodiscard]] const AtlasSlot* lookup(PageId p) const
    {
        auto it = map_.find(p);
        return it == map_.end() ? nullptr : &it->second;
    }

    /// Allocate the next free physical slot for `p`; evicts the
    /// oldest resident page when full (FIFO replacement is the
    /// reference Mittring choice; production wants LRU).
    AtlasSlot allocate(PageId p)
    {
        if (auto it = map_.find(p); it != map_.end()) return it->second;
        AtlasSlot s {};
        const std::uint32_t total = static_cast<std::uint32_t>(atlas_w_) *
                                    static_cast<std::uint32_t>(atlas_h_);
        if (next_slot_ < total)
        {
            s.slot_x = static_cast<std::uint16_t>(next_slot_ % atlas_w_);
            s.slot_y = static_cast<std::uint16_t>(next_slot_ / atlas_w_);
            ++next_slot_;
        }
        else if (!fifo_.empty())
        {
            const PageId victim = fifo_.front();
            fifo_.erase(fifo_.begin());
            s = map_[victim];
            map_.erase(victim);
        }
        s.valid = 1;
        map_[p] = s;
        fifo_.push_back(p);
        return s;
    }

    [[nodiscard]] std::size_t resident_count() const noexcept { return map_.size(); }

    /// phase1053: read-only view of the resident set (PageId ->
    /// AtlasSlot). Debug visualisers and feedback analysers need to
    /// enumerate what is resident; exposing the map const-ref keeps
    /// the allocator's invariants (mutation still only via
    /// allocate()). Iteration order is unordered -- callers that
    /// need FIFO age must not infer it from this view.
    [[nodiscard]] const std::unordered_map<PageId, AtlasSlot, PageIdHash>&
    residents() const noexcept { return map_; }

private:
    std::uint16_t atlas_w_;
    std::uint16_t atlas_h_;
    std::uint32_t next_slot_ { 0 };
    std::unordered_map<PageId, AtlasSlot, PageIdHash> map_;
    std::vector<PageId> fifo_;
};

// ---- GLSL feedback shader helper -------------------------------------------

constexpr std::string_view kFeedbackGlsl = R"glsl(
// Drop-in inside the material FS. Computes the requested PageId from
// the texture coordinate + mip, atomically appends it into the
// feedback buffer for the CPU consumer.
struct FeedbackReq { uvec4 pkg; };  // x=px, y=py, z=mip, w=frame_id
layout(set = 0, binding = 5) buffer Feedback {
  uint count;
  FeedbackReq reqs[];
} F;
void cd_vt_request(uvec2 page_xy, uint mip, uint frame_id) {
  uint slot = atomicAdd(F.count, 1u);
  F.reqs[slot].pkg = uvec4(page_xy, mip, frame_id);
}
)glsl";

}  // namespace cd::virtual_textures
