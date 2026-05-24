// =============================================================================
// CHROMODYNAMIC — cd/scene/SceneStats.hpp
// Phase 96.B / Wave 264 — aggregate scene statistics struct.
//
// Editor inspector "Scene Info" panel summary:
//   * entity_count, mesh_renderer_count, light_count
//   * total triangle count
//   * scene world-space bbox
//   * estimated memory bytes (textures + meshes + animations)
//
// Pure data — caller (Editor / DiagnosticsSystem) fills the struct
// by walking the scene; this primitive holds the contract.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <cd/math/Vector.hpp>

#include <cstdint>

namespace cd::scene
{

struct SceneStats
{
    std::uint32_t   entity_count         { 0 };
    std::uint32_t   mesh_renderer_count  { 0 };
    std::uint32_t   light_count          { 0 };
    std::uint64_t   total_triangle_count { 0 };
    std::uint64_t   estimated_bytes      { 0 };
    cd::math::Vec3f bbox_min             {};
    cd::math::Vec3f bbox_max             {};
};

[[nodiscard]] constexpr cd::math::Vec3f bbox_extent(const SceneStats& s) noexcept
{
    return cd::math::Vec3f {
        s.bbox_max.x - s.bbox_min.x,
        s.bbox_max.y - s.bbox_min.y,
        s.bbox_max.z - s.bbox_min.z,
    };
}

}  // namespace cd::scene
