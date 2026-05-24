// =============================================================================
// CHROMODYNAMIC — cd/rhi/PipelineCacheKey.hpp
// Phase 80.A / Wave 248 — hash key for graphics-pipeline cache lookup.
//
// A graphics pipeline (PSO) is expensive to create — bake once,
// reuse forever. `PipelineCacheKey` combines the inputs that uniquely
// identify a PSO into a single 64-bit hash:
//
//   * vertex_shader hash    (32 bit) — usually CRC32 of SPIR-V.
//   * fragment_shader hash  (32 bit)
//   * vertex_layout_hash    (16 bit) — VertexLayoutBuilder stride+formats.
//   * blend_state_hash      (8 bit)  — preset index.
//   * depth_state_hash      (8 bit)  — preset index.
//
// Packed into a 64-bit key. Cache hits return the existing PSO;
// misses trigger a real compile.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>

#include <cstdint>

namespace cd::rhi
{

struct PipelineCacheKey
{
    std::uint32_t vs_hash         { 0 };
    std::uint32_t fs_hash         { 0 };
    std::uint16_t vertex_layout   { 0 };
    std::uint8_t  blend_preset    { 0 };
    std::uint8_t  depth_preset    { 0 };

    [[nodiscard]] constexpr std::uint64_t hash() const noexcept
    {
        std::uint64_t h = static_cast<std::uint64_t>(vs_hash);
        h ^= (static_cast<std::uint64_t>(fs_hash) << 32);
        h ^= static_cast<std::uint64_t>(vertex_layout) * 0x100000001B3ULL;
        h ^= static_cast<std::uint64_t>(blend_preset) << 8;
        h ^= static_cast<std::uint64_t>(depth_preset) << 16;
        return h;
    }

    friend constexpr bool operator==(const PipelineCacheKey&,
                                     const PipelineCacheKey&) noexcept = default;
};

}  // namespace cd::rhi
