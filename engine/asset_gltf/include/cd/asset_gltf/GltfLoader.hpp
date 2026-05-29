// =============================================================================
// CHROMODYNAMIC — cd/asset_gltf/GltfLoader.hpp
//
// glTF 2.0 importer built on top of tinygltf. Decodes both ASCII (.gltf+bin)
// and binary (.glb) containers into a flat, render-ready intermediate scene
// (`GltfScene`) that the engine's render tier can hand to cd::rhi without
// further parsing.
//
// Scope (v1, extended in SK1/SK2 phase 226):
//   * Static meshes — POSITION (req) + NORMAL + TEXCOORD_0
//   * Indexed primitives (uint16 / uint32)
//   * Base-color factor + base-color texture per material (no PBR yet)
//   * Skinning (joints/weights VB + GltfSkin) added in earlier phase
//   * Animations (channels + samplers + per-node TRS curves) — SK1/SK2
//   * No morph targets, no cameras
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
#include <cd/math/Matrix.hpp>
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

/// Per-vertex skin influences. JOINTS_0 + WEIGHTS_0 from the glTF spec.
/// Up to 4 bones per vertex (the spec's first set of attributes). Vertex
/// is unaffected by skinning when the skin index is -1 in the primitive.
struct GltfSkinVertex
{
    std::array<std::uint16_t, 4> joints { 0, 0, 0, 0 };
    std::array<float, 4> weights { 0.0F, 0.0F, 0.0F, 0.0F };
};

/// One drawable primitive (a single VkDraw call equivalent). Always indexed —
/// non-indexed primitives are converted to a trivial 0..N-1 index buffer at
/// load time so the renderer has one code path.
struct GltfPrimitive
{
    std::vector<GltfVertex> vertices;
    std::vector<std::uint32_t> indices;
    int material_index { -1 };  ///< Index into `GltfScene::materials`, or -1.
    /// Per-vertex skin influences, parallel to `vertices`. Empty when
    /// the primitive has no JOINTS_0 / WEIGHTS_0 attribute.
    std::vector<GltfSkinVertex> skin_vertices;
};

/// A glTF skin: an ordered list of joint node indices + the per-joint
/// inverse-bind matrices (so the skinning pipeline doesn't have to
/// re-derive them from the node hierarchy). Maps directly onto
/// `cd::anim::Skeleton`.
struct GltfSkin
{
    std::string name;
    std::vector<int> joints;                      ///< Indices into `GltfScene::nodes`.
    std::vector<cd::math::Mat4f> inverse_bind_matrices;
    int skeleton_root { -1 };                     ///< Optional explicit root node, or -1.
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

/// glTF alphaMode values. OPAQUE = no alpha; MASK = discard below alphaCutoff;
/// BLEND = alpha blending (treated as MASK with cutoff=0.5 for first cut).
enum class GltfAlphaMode : std::uint8_t
{
    kOpaque = 0,
    kMask   = 1,
    kBlend  = 2,
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
    GltfAlphaMode alpha_mode { GltfAlphaMode::kOpaque };
    float alpha_cutoff { 0.5F };  ///< Used when alpha_mode == kMask.
};

/// One node of the glTF scene tree. Nodes carry a local transform and an
/// optional mesh reference. The full hierarchy is held in `GltfScene::nodes`
/// with parent indices forming a forest (roots have parent = -1).
struct GltfNode
{
    std::string name;
    int parent { -1 };          ///< Index into `GltfScene::nodes`, or -1 for roots.
    std::vector<int> children;  ///< Indices into `GltfScene::nodes`.
    int mesh_index { -1 };      ///< Index into `GltfScene::meshes`, or -1 if pure transform.
    int skin_index { -1 };      ///< Index into `GltfScene::skins`, or -1 if static mesh.
    cd::math::Mat4f local_matrix { cd::math::Mat4f::identity() };
};

/// Flat draw instance — produced by walking the node forest after load.
/// Renderers iterate `GltfScene::instances` and draw mesh[mesh_index] with
/// `world_matrix` baked in; no node-tree traversal at render time.
struct GltfInstance
{
    int mesh_index { -1 };
    int node_index { -1 };  ///< Source node, in case the caller wants the name / hierarchy back.
    cd::math::Mat4f world_matrix { cd::math::Mat4f::identity() };
};

// ---- Animation (SK1, phase 226) ---------------------------------------------

/// glTF interpolation modes. Per the spec each sampler picks one; the
/// channel inherits it implicitly via its sampler.
enum class GltfInterpolation : std::uint8_t
{
    kLinear      = 0,  ///< default — linear blend for TRS, slerp for rotation
    kStep        = 1,  ///< hold-then-snap; "stair-step" pose change
    kCubicSpline = 2,  ///< 3x output per keyframe (inTangent, value, outTangent)
};

/// glTF channel target paths. "weights" is morph-target weights, which the
/// engine doesn't yet support (kMorphWeights is parsed but channels with
/// this path are skipped at the bridge layer).
enum class GltfTargetPath : std::uint8_t
{
    kTranslation  = 0,
    kRotation     = 1,
    kScale        = 2,
    kMorphWeights = 3,
};

/// One sampler: a time axis (input) + per-time output values. Output stride
/// depends on target path: translation/scale = 3 floats, rotation = 4, morph
/// weights = N (number of morph targets). For kCubicSpline the output buffer
/// holds three packed entries per keyframe — engine flattens them out at
/// bridge time (SK3).
struct GltfAnimSampler
{
    std::vector<float> times;          ///< Length = keyframe count.
    std::vector<float> values;         ///< Length = times.size() * stride * (kCubicSpline ? 3 : 1).
    GltfInterpolation interpolation { GltfInterpolation::kLinear };
};

/// One channel: connects a sampler to a target node + property path.
struct GltfAnimChannel
{
    int sampler_index { -1 };          ///< Index into GltfAnimation::samplers.
    int target_node { -1 };            ///< Index into GltfScene::nodes.
    GltfTargetPath path { GltfTargetPath::kTranslation };
};

/// One animation = collection of channels driven by a shared sampler pool.
/// `duration` is the max time observed across every sampler (clip length).
struct GltfAnimation
{
    std::string name;
    std::vector<GltfAnimSampler> samplers;
    std::vector<GltfAnimChannel> channels;
    float duration { 0.0F };
};

struct GltfScene
{
    std::vector<GltfMesh> meshes;
    std::vector<GltfMaterial> materials;
    std::vector<GltfTexture> textures;
    std::vector<GltfSkin> skins;  ///< Optional skinning data; empty for non-skinned scenes.
    std::vector<GltfAnimation> animations;  ///< SK1: TRS curves per channel; empty when the asset has none.

    /// Full node hierarchy. `roots` indexes into this list; each non-root
    /// node's `parent` field also points back here.
    std::vector<GltfNode> nodes;
    std::vector<int> roots;

    /// Pre-baked flat instance list. One entry per (node, mesh) pair, with
    /// the node's accumulated world matrix already computed. Renderers may
    /// ignore the node tree entirely and just iterate this.
    std::vector<GltfInstance> instances;

    /// Axis-aligned bounding box over every primitive in every mesh,
    /// computed in WORLD SPACE (i.e. after applying instance world matrices)
    /// so auto-framing works correctly for off-origin scenes.
    cd::math::Vec3f bbox_min { 0.0F, 0.0F, 0.0F };
    cd::math::Vec3f bbox_max { 0.0F, 0.0F, 0.0F };
};

// ---- Loader API -------------------------------------------------------------

/// Load a glTF file from disk. The container type is detected from the file
/// extension: `.glb` → binary, anything else → ASCII (.gltf with external
/// .bin / image files). Returns the decoded scene or an error code with a
/// human-readable message.
[[nodiscard]] cd::core::Result<GltfScene> load_gltf(std::string_view path);

/// Decode an in-memory glTF blob — `.gltf` (JSON text) or `.glb` (binary).
/// Auto-detects the variant by checking the magic "glTF" 4-byte header.
/// `base_dir` is the resource-resolution root for external URIs
/// (textures, `.bin`); pass an empty string to disallow external lookups.
[[nodiscard]] cd::core::Result<GltfScene>
load_gltf_from_memory(const std::uint8_t* bytes, std::size_t size, std::string_view base_dir = {});

}  // namespace cd::asset_gltf
