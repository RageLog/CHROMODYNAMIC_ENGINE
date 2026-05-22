// =============================================================================
// CHROMODYNAMIC — cd/asset_cdmesh/CdMesh.hpp
//
// Cooked binary mesh format. The runtime path for shipping geometry:
//   * .obj / .gltf parsed offline (cd::asset_obj, cd::asset_gltf)
//   * Result written to .cdmesh via `save()`
//   * Runtime reads .cdmesh via `load()` — single fread + memcpy-equivalent
//     blits straight into the GPU vertex/index buffer descriptors.
//
// Format (little-endian, all offsets and sizes are 32-bit):
//
//   ┌──────────────── HEADER (32 B) ──────────────┐
//   │ magic[4]     "CDMS"                          │
//   │ version      u32 = kFormatVersion            │
//   │ flags        u32 (reserved, =0)              │
//   │ vertex_count u32                             │
//   │ index_count  u32                             │
//   │ vertex_stride u32   (bytes per vertex)       │
//   │ index_stride  u32   (2 or 4)                 │
//   │ bbox_min[3]   f32 (12 B)                     │
//   │ bbox_max[3]   f32 (12 B)                     │
//   └──────────────────────────────────────────────┘
//   Vertex blob: vertex_count * vertex_stride bytes (aligned to 16)
//   Index  blob: index_count  * index_stride  bytes (aligned to 4)
//
// Why hand-rolled instead of FlatBuffers / Capnproto:
//   * Mesh data is a hot-path resource; a pointer-and-length API beats
//     anything with vtable / dispatch overhead at load time.
//   * No external schema dependency keeps the cd::asset_cdmesh library
//     at exactly cd::core + cd::math.
//   * Format version is a u32; bumping it is a one-line change and the
//     loader rejects mismatched versions explicitly (kVersionMismatch).
//
// Vertex layout is OPAQUE to cdmesh — the library only sees a stride and
// a blob. Both producer (e.g. cd::asset_obj → cdmesh cooker) and consumer
// (renderer) must agree on the interleaved format. The default
// CdVertexStd matches cd::asset_obj::ObjVertex and cd::asset_gltf::
// GltfVertex byte-for-byte for the common 32-byte (pos+normal+uv) layout.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <cd/core/ErrorCode.hpp>
#include <cd/core/Result.hpp>
#include <cd/math/Vector.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace cd::asset_cdmesh
{

namespace cdmesh_errors
{
inline constexpr std::uint32_t kDomain = 0x0010;

enum class Code : std::uint32_t
{
    kOk = 0,
    kFileNotFound = 1,
    kIoError = 2,
    kMagicMismatch = 3,
    kVersionMismatch = 4,
    kCorrupt = 5,
    kInvalidArgument = 6,
};

[[nodiscard]] inline cd::core::ErrorCode make(Code c, std::string_view m = {}) noexcept
{
    return cd::core::ErrorCode { kDomain, static_cast<std::uint32_t>(c), m };
}
}  // namespace cdmesh_errors

inline constexpr std::array<char, 4> kMagic { 'C', 'D', 'M', 'S' };
inline constexpr std::uint32_t kFormatVersion = 1;

/// Default 32-byte interleaved vertex layout (matches GltfVertex / ObjVertex).
/// Engines that ship a different vertex spec pass their own stride to
/// `save()` and read raw bytes from `loaded.vertex_blob` on load.
struct CdVertexStd
{
    cd::math::Vec3f position { 0.0F, 0.0F, 0.0F };
    cd::math::Vec3f normal { 0.0F, 1.0F, 0.0F };
    cd::math::Vec2f texcoord0 { 0.0F, 0.0F };
};
static_assert(sizeof(CdVertexStd) == 32, "CdVertexStd default stride is 32 bytes");

/// On-load representation: header values + owning byte blobs the renderer
/// memcpys straight into GPU buffers.
struct CdMesh
{
    std::uint32_t vertex_count { 0 };
    std::uint32_t index_count { 0 };
    std::uint32_t vertex_stride { 0 };
    std::uint32_t index_stride { 0 };
    cd::math::Vec3f bbox_min { 0.0F, 0.0F, 0.0F };
    cd::math::Vec3f bbox_max { 0.0F, 0.0F, 0.0F };
    std::vector<std::uint8_t> vertex_blob;
    std::vector<std::uint8_t> index_blob;

    /// Convenience typed access when `vertex_stride == sizeof(CdVertexStd)`.
    [[nodiscard]] std::span<const CdVertexStd> std_vertices() const noexcept
    {
        if (vertex_stride != sizeof(CdVertexStd))
            return {};
        return { reinterpret_cast<const CdVertexStd*>(vertex_blob.data()), vertex_count };
    }

    [[nodiscard]] std::span<const std::uint16_t> indices_u16() const noexcept
    {
        if (index_stride != 2)
            return {};
        return { reinterpret_cast<const std::uint16_t*>(index_blob.data()), index_count };
    }

    [[nodiscard]] std::span<const std::uint32_t> indices_u32() const noexcept
    {
        if (index_stride != 4)
            return {};
        return { reinterpret_cast<const std::uint32_t*>(index_blob.data()), index_count };
    }
};

/// Producer-side write descriptor. Caller supplies tightly-packed vertex
/// + index blobs; we copy them into the file. `vertex_stride` must equal
/// `vertices.size_bytes() / vertex_count`.
struct SaveDesc
{
    std::span<const std::uint8_t> vertices;
    std::span<const std::uint8_t> indices;
    std::uint32_t vertex_count { 0 };
    std::uint32_t index_count { 0 };
    std::uint32_t vertex_stride { 0 };
    std::uint32_t index_stride { 0 };  ///< Must be 2 or 4.
    cd::math::Vec3f bbox_min { 0.0F, 0.0F, 0.0F };
    cd::math::Vec3f bbox_max { 0.0F, 0.0F, 0.0F };
};

/// Persist a mesh blob to disk. Returns kInvalidArgument if any size
/// invariant fails, kIoError if the file can't be written.
[[nodiscard]] cd::core::Result<void> save(std::string_view path, const SaveDesc& desc);

/// Load a previously-saved .cdmesh file. Validates magic + version +
/// claimed sizes before allocating; corrupt files return kCorrupt with
/// no allocation, never crash.
[[nodiscard]] cd::core::Result<CdMesh> load(std::string_view path);

}  // namespace cd::asset_cdmesh
