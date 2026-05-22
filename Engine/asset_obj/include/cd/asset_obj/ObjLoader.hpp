// =============================================================================
// CHROMODYNAMIC — cd/asset_obj/ObjLoader.hpp
//
// Wavefront .obj loader. Hand-rolled (no third-party dep) because:
//   * The format is small enough that a self-contained parser is shorter
//     than wrapping tinyobjloader's vendored copy.
//   * Avoiding tinyobjloader keeps the cd::asset_obj dependency surface at
//     EXACTLY cd::core + cd::math. Anyone shipping a tiny demo can link
//     cd::asset_obj without dragging in tinygltf's JSON parser etc.
//
// Scope (v1):
//   * Vertex positions (`v`)
//   * Vertex normals (`vn`) — optional; if missing, flat per-face normals
//     are synthesized from triangle edges
//   * Texture coords (`vt`) — optional; missing → (0, 0)
//   * Faces (`f`) with vertex/UV/normal triplets, triangles + quads (quads
//     get fan-triangulated 0-1-2 / 0-2-3)
//   * Comments (`#`) and blank lines
//
// NOT in v1:
//   * `mtllib` / `usemtl` (materials) — caller can layer a separate
//     materials file via cd::asset_material later
//   * `g` / `o` (groups / objects) — entire file collapses to one mesh
//   * `s` (smoothing groups) — ignored; per-vertex normals if present win,
//     otherwise flat per-face shading
//   * Negative indices (relative addressing) — explicitly rejected with
//     kUnsupported, even though the spec permits them
//   * w-coords on `v` lines (we read only the first three floats)
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <cd/core/ErrorCode.hpp>
#include <cd/core/Result.hpp>
#include <cd/math/Vector.hpp>

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace cd::asset_obj
{

namespace obj_errors
{
inline constexpr std::uint32_t kDomain = 0x000F;

enum class Code : std::uint32_t
{
    kOk = 0,
    kFileNotFound = 1,
    kParseFailed = 2,
    kUnsupported = 3,
    kInvalidArgument = 4,
};

[[nodiscard]] inline cd::core::ErrorCode make(Code c, std::string_view m = {}) noexcept
{
    return cd::core::ErrorCode { kDomain, static_cast<std::uint32_t>(c), m };
}
}  // namespace obj_errors

/// Interleaved render vertex — matches `cd::asset_gltf::GltfVertex` byte-
/// for-byte so a renderer can blit cd::asset_obj output through the same
/// pipeline without a separate vertex layout.
struct ObjVertex
{
    cd::math::Vec3f position { 0.0F, 0.0F, 0.0F };
    cd::math::Vec3f normal { 0.0F, 1.0F, 0.0F };
    cd::math::Vec2f texcoord0 { 0.0F, 0.0F };
};

/// Decoded mesh. One file → one mesh (no group splitting in v1). Always
/// indexed; non-degenerate vertices are de-duplicated so a 1 M-tri obj
/// file produces the smallest possible VBO.
struct ObjMesh
{
    std::vector<ObjVertex> vertices;
    std::vector<std::uint32_t> indices;
    cd::math::Vec3f bbox_min { 0.0F, 0.0F, 0.0F };
    cd::math::Vec3f bbox_max { 0.0F, 0.0F, 0.0F };
};

/// Load a .obj file from disk. The file is parsed in a single pass; UVs
/// and normals are inlined into the vertex array as encountered.
[[nodiscard]] cd::core::Result<ObjMesh> load_obj(std::string_view path);

/// Parse a .obj from an already-loaded text buffer. Useful for tests and
/// for embedded asset blobs.
[[nodiscard]] cd::core::Result<ObjMesh> parse_obj(std::string_view text);

}  // namespace cd::asset_obj
