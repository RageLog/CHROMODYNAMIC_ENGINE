// =============================================================================
// CHROMODYNAMIC — cd/asset_gltf/GltfLoader.hpp
//
// glTF 2.0 importer built on top of tinygltf. Decodes both ASCII (.gltf+bin)
// and binary (.glb) containers into a flat, render-ready intermediate scene
// (`GltfScene`) that the engine's render tier can hand to cd::rhi without
// further parsing.
//
// Scope (v1):
//   * Static meshes — POSITION (req) + NORMAL + TEXCOORD_0
//   * Indexed primitives (uint16 / uint32)
//   * Base-color factor + base-color texture per material (no PBR yet)
//   * No skinning, no morph targets, no animations, no cameras
//
// Why a separate library from `cd::asset`:
//   `cd::asset` is INTERFACE-only and stays dependency-free so foundation/
//   loader code can include it without dragging in heavy parsers. The glTF
//   path needs tinygltf + (transitively) stb_image, so it lives in its own
//   static lib that callers opt into.
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

namespace cd::asset_gltf
{

// ---- Error domain -----------------------------------------------------------

namespace gltf_errors
{
inline constexpr std::uint32_t kDomain = 0x000C;

enum class Code : std::uint32_t
{
    kOk = 0,
    kFileNotFound = 1,
    kParseFailed = 2,
    kMissingPosition = 3,
    kUnsupportedAccessor = 4,
    kInvalidArgument = 5,
};

[[nodiscard]] inline cd::core::ErrorCode make(Code c, std::string_view m = {}) noexcept
{
    return cd::core::ErrorCode { kDomain, static_cast<std::uint32_t>(c), m };
}
}  // namespace gltf_errors

// ---- Decoded geometry -------------------------------------------------------

/// Interleaved render vertex. The layout is stable and corresponds to the
/// `kAttrs` declaration the sample feeds to `cd::material::Material`.
struct GltfVertex
{
    cd::math::Vec3f position { 0.0F, 0.0F, 0.0F };
    cd::math::Vec3f normal { 0.0F, 1.0F, 0.0F };
    cd::math::Vec2f texcoord0 { 0.0F, 0.0F };
};

/// One drawable primitive (a single VkDraw call equivalent). Always indexed —
/// non-indexed primitives are converted to a trivial 0..N-1 index buffer at
/// load time so the renderer has one code path.
struct GltfPrimitive
{
    std::vector<GltfVertex> vertices;
    std::vector<std::uint32_t> indices;
    int material_index { -1 };  ///< Index into `GltfScene::materials`, or -1.
};

struct GltfMesh
{
    std::string name;
    std::vector<GltfPrimitive> primitives;
};

/// Decoded image (always RGBA8). Width/height of 0 marks an unloaded texture.
struct GltfTexture
{
    std::vector<std::uint8_t> rgba;
    std::uint32_t width { 0 };
    std::uint32_t height { 0 };
};

/// Minimal PBR-flavoured material — enough to drive a flat-color or single-
/// texture pipeline. Extended factors (metallic, roughness, emissive) are
/// decoded but unused by the current renderer; kept for forward-compat.
struct GltfMaterial
{
    std::string name;
    std::array<float, 4> base_color_factor { 1.0F, 1.0F, 1.0F, 1.0F };
    int base_color_texture { -1 };  ///< Index into `GltfScene::textures`, or -1.
    float metallic_factor { 1.0F };
    float roughness_factor { 1.0F };
    bool double_sided { false };
};

struct GltfScene
{
    std::vector<GltfMesh> meshes;
    std::vector<GltfMaterial> materials;
    std::vector<GltfTexture> textures;

    /// Axis-aligned bounding box over every primitive in every mesh. Useful
    /// for auto-framing the camera in viewers/samples.
    cd::math::Vec3f bbox_min { 0.0F, 0.0F, 0.0F };
    cd::math::Vec3f bbox_max { 0.0F, 0.0F, 0.0F };
};

// ---- Loader API -------------------------------------------------------------

/// Load a glTF file from disk. The container type is detected from the file
/// extension: `.glb` → binary, anything else → ASCII (.gltf with external
/// .bin / image files). Returns the decoded scene or an error code with a
/// human-readable message.
[[nodiscard]] cd::core::Result<GltfScene> load_gltf(std::string_view path);

}  // namespace cd::asset_gltf
