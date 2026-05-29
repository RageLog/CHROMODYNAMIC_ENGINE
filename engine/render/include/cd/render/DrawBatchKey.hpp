// =============================================================================
// CHROMODYNAMIC — cd/render/DrawBatchKey.hpp
// Phase 91.B / Wave 259 — instancing batch key.
//
// Draw-call instancing groups draws that share the same pipeline,
// material instance, and vertex buffer. `DrawBatchKey` is the
// equality + hash key by which the renderer buckets pending draws
// before issuing `vkCmdDrawIndexedIndirect` / `DrawInstanced`.
//
//   * mesh_handle    — uint64 stable handle to the vertex+index pair.
//   * material_id    — uint32, post-PSO.
//   * pso_hash       — uint64 from PipelineCacheKey::hash().
//   * descriptor_set — uint32 descriptor-set ID (0 = no override).
//
// Total 24 bytes; equality is fieldwise; hash is FNV-1a 64 over the
// raw memory.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace cd::render
{

struct DrawBatchKey
{
    std::uint64_t mesh_handle    { 0 };
    std::uint32_t material_id    { 0 };
    std::uint32_t descriptor_set { 0 };
    std::uint64_t pso_hash       { 0 };

    [[nodiscard]] std::uint64_t hash() const noexcept
    {
        std::uint64_t h = 0xCBF29CE484222325ULL;
        const auto* p = reinterpret_cast<const std::uint8_t*>(this);
        for (std::size_t i = 0; i < sizeof(DrawBatchKey); ++i)
        {
            h ^= static_cast<std::uint64_t>(p[i]);
            h *= 0x100000001B3ULL;
        }
        return h;
    }

    friend constexpr bool operator==(const DrawBatchKey&,
                                     const DrawBatchKey&) noexcept = default;
};
static_assert(sizeof(DrawBatchKey) == 24, "DrawBatchKey must stay 24 bytes");

}  // namespace cd::render
