// =============================================================================
// CHROMODYNAMIC — cd/render/MeshStats.hpp
// Phase 68.B / Wave 236 — mesh vertex/triangle/bbox aggregate.
//
// Editor inspector + ProfilerView need a compact "what's this mesh
// made of" summary:
//   * vertex_count
//   * index_count
//   * triangle_count = index_count / 3
//   * bbox (min, max) — axis-aligned bounding box
//   * estimated_vertex_bytes (vertex_count * stride)
//
// Pure data — caller fills the struct after asset cooking. Use
// `cd::core::format_bytes()` (Phase 68.A) to render the byte total.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <cd/math/Vector.hpp>

#include <cstdint>

namespace cd::render
{

struct MeshStats
{
    std::uint32_t     vertex_count { 0 };
    std::uint32_t     index_count  { 0 };
    std::uint32_t     vertex_stride_bytes { 0 };
    cd::math::Vec3f   bbox_min {};
    cd::math::Vec3f   bbox_max {};

    [[nodiscard]] std::uint32_t triangle_count() const noexcept
    {
        return index_count / 3u;
    }

    [[nodiscard]] std::uint64_t estimated_vertex_bytes() const noexcept
    {
        return static_cast<std::uint64_t>(vertex_count) * vertex_stride_bytes;
    }

    [[nodiscard]] cd::math::Vec3f bbox_extent() const noexcept
    {
        return cd::math::Vec3f {
            bbox_max.x - bbox_min.x,
            bbox_max.y - bbox_min.y,
            bbox_max.z - bbox_min.z,
        };
    }

    [[nodiscard]] cd::math::Vec3f bbox_center() const noexcept
    {
        return cd::math::Vec3f {
            (bbox_min.x + bbox_max.x) * 0.5F,
            (bbox_min.y + bbox_max.y) * 0.5F,
            (bbox_min.z + bbox_max.z) * 0.5F,
        };
    }
};

}  // namespace cd::render
