// =============================================================================
// CHROMODYNAMIC — cd/rhi/Descriptors.hpp
// ADR-001 (Sprint S3.0) — resource creation descriptors.
//
// Plain-data structs ("Desc") describe resources at the moment of creation.
// They live longer than a frame only if the back-end chooses to cache them
// (e.g. pipeline state objects). All defaults are intentional — a caller that
// only sets the few fields it cares about must get a sensible resource.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <cd/rhi/Enums.hpp>
#include <cd/rhi/Format.hpp>
#include <cd/rhi/Handles.hpp>

#include <cstdint>
#include <span>
#include <string_view>

namespace cd::rhi
{

struct Extent2D
{
    std::uint32_t width { 0 }, height { 0 };
};

struct Extent3D
{
    std::uint32_t width { 0 }, height { 0 }, depth { 1 };
};

struct Offset2D
{
    std::int32_t x { 0 }, y { 0 };
};

struct Offset3D
{
    std::int32_t x { 0 }, y { 0 }, z { 0 };
};

struct Rect2D
{
    Offset2D offset {};
    Extent2D extent {};
};

struct Viewport
{
    float x { 0.0f }, y { 0.0f };
    float width { 0.0f }, height { 0.0f };
    float min_depth { 0.0f }, max_depth { 1.0f };
};

union ClearColor
{
    float f32[4];
    std::int32_t i32[4];
    std::uint32_t u32[4];
};

struct ClearDepthStencil
{
    float depth { 1.0f };
    std::uint32_t stencil { 0 };
};

struct ClearValue
{
    ClearColor color {};
    ClearDepthStencil depth_stencil {};
};

// ---- Buffer ---------------------------------------------------------------

struct BufferDesc
{
    std::uint64_t size { 0 };
    BufferUsage usage { BufferUsage::kNone };
    MemoryUsage memory { MemoryUsage::kAuto };
    std::string_view debug_name {};
};

// ---- Texture --------------------------------------------------------------

struct TextureDesc
{
    TextureType type { TextureType::k2D };
    Format format { Format::kRGBA8Unorm };
    Extent3D extent {};
    std::uint32_t mip_levels { 1 };
    std::uint32_t array_layers { 1 };
    SampleCount samples { SampleCount::k1 };
    TextureUsage usage { TextureUsage::kNone };
    MemoryUsage memory { MemoryUsage::kGpuOnly };
    std::string_view debug_name {};
};

struct TextureViewDesc
{
    TextureHandle texture {};
    TextureType type { TextureType::k2D };
    Format format { Format::kUndefined };  // kUndefined → inherit texture
    std::uint32_t base_mip { 0 }, mip_count { 1 };
    std::uint32_t base_layer { 0 }, layer_count { 1 };
};

// ---- Sampler --------------------------------------------------------------

struct SamplerDesc
{
    SamplerFilter mag_filter { SamplerFilter::kLinear };
    SamplerFilter min_filter { SamplerFilter::kLinear };
    SamplerMipmapMode mipmap_mode { SamplerMipmapMode::kLinear };
    SamplerAddressMode address_u { SamplerAddressMode::kRepeat };
    SamplerAddressMode address_v { SamplerAddressMode::kRepeat };
    SamplerAddressMode address_w { SamplerAddressMode::kRepeat };
    float mip_lod_bias { 0.0f };
    float min_lod { 0.0f };
    float max_lod { 1000.0f };
    float max_anisotropy { 1.0f };
    bool anisotropy_enable { false };
    bool compare_enable { false };
    CompareOp compare_op { CompareOp::kAlways };
    BorderColor border_color { BorderColor::kFloatOpaqueBlack };
};

// ---- Shader / pipeline ----------------------------------------------------

struct ShaderModuleDesc
{
    ShaderStage stage { ShaderStage::kNone };
    const void* code { nullptr };
    std::uint64_t code_size { 0 };
    std::string_view entry_point { "main" };
    std::string_view debug_name {};
};

struct VertexAttribute
{
    std::uint32_t location { 0 };
    std::uint32_t binding { 0 };
    Format format { Format::kRGB32Float };
    std::uint32_t offset { 0 };
};

struct VertexBinding
{
    std::uint32_t binding { 0 };
    std::uint32_t stride { 0 };
    bool per_instance { false };
};

struct DepthStencilState
{
    bool depth_test { true };
    bool depth_write { true };
    CompareOp depth_compare { CompareOp::kLess };
    bool stencil_test { false };
};

struct RasterState
{
    PolygonMode polygon_mode { PolygonMode::kFill };
    CullMode cull { CullMode::kBack };
    /// Default = kClockwise to match the Vulkan-NDC convention used by every
    /// sample in the tree (and every modern engine target backend — D3D12 and
    /// Metal are also Y-down). Source code authored "CCW from outside the
    /// mesh" still survives because the vertex shader's `clip.y = -clip.y`
    /// (or any equivalent Y-flip) flips winding after the perspective divide;
    /// the rasterizer then sees the resulting CW triangles, and with
    /// `front_face = kClockwise` it correctly classifies them as front.
    /// This is the default that makes back-face culling actually work.
    ///
    /// Pre-v0.25.0 the default was kCounterClockwise. Every Vulkan-NDC sample
    /// had to override `cull = kNone` to mask the broken winding (BUGs #1,
    /// #2, #5 in the v1.0 rollback ADR). That boilerplate is now obsolete
    /// but still present — explicit overrides remain valid and continue to
    /// behave identically.
    FrontFace front_face { FrontFace::kClockwise };
    float line_width { 1.0f };
    bool depth_clamp { false };
    bool depth_bias_enable { false };
    float depth_bias_constant { 0.0f };
    float depth_bias_slope { 0.0f };
};

struct BlendAttachmentState
{
    bool blend_enable { false };
    BlendFactor src_color { BlendFactor::kOne };
    BlendFactor dst_color { BlendFactor::kZero };
    BlendOp color_op { BlendOp::kAdd };
    BlendFactor src_alpha { BlendFactor::kOne };
    BlendFactor dst_alpha { BlendFactor::kZero };
    BlendOp alpha_op { BlendOp::kAdd };
    std::uint8_t color_write_mask { 0xF };
};

// ---- Render pass / attachment ---------------------------------------------

struct AttachmentDesc
{
    Format format { Format::kUndefined };
    SampleCount samples { SampleCount::k1 };
    LoadOp load_op { LoadOp::kClear };
    StoreOp store_op { StoreOp::kStore };
    LoadOp stencil_load_op { LoadOp::kDontCare };
    StoreOp stencil_store_op { StoreOp::kDontCare };
    ResourceState initial_state { ResourceState::kUndefined };
    ResourceState final_state { ResourceState::kPresent };
};

// ---- Acceleration structures (Phase 14.G — Wave 163) ----------------------
//
// API shape lands at v0.40.0; backend implementation is queued for a
// follow-up wave alongside the ray-tracing pipeline + shader-binding
// table. The interface mirrors VK_KHR_acceleration_structure +
// DXR Tier 1.1: BLAS (per-mesh triangle geometry) and TLAS (instance
// list referencing BLAS handles).
//
// Backends that don't support RT return kNotImplemented from the
// `create_acceleration_structure` / `build_acceleration_structure`
// entry points; consumers gate on `device.features().ray_tracing`
// before reaching for these.

enum class AccelStructureKind : std::uint8_t
{
    kBottomLevel,  ///< BLAS — per-mesh triangle geometry
    kTopLevel,     ///< TLAS — instance list referencing one or more BLAS
};

struct AccelTriangleGeometry
{
    /// Vertex buffer (positions). Stride bytes between successive
    /// vertex positions; format is RGB32Float in v0.40.0.
    BufferHandle vertex_buffer {};
    std::uint64_t vertex_offset { 0 };
    std::uint32_t vertex_count { 0 };
    std::uint32_t vertex_stride { 12 };  // RGB32F → 12 bytes
    /// Index buffer (kUInt16 or kUInt32). Optional — when
    /// `index_count == 0` the geometry is non-indexed.
    BufferHandle index_buffer {};
    std::uint64_t index_offset { 0 };
    std::uint32_t index_count { 0 };
    IndexType index_type { IndexType::kUInt32 };
};

/// Phase 117 — TLAS instance descriptor. Mirrors
/// `VkAccelerationStructureInstanceKHR` (and DXR
/// `D3D12_RAYTRACING_INSTANCE_DESC`): each instance references a BLAS
/// handle, places it in the world with a 3x4 row-major transform
/// (column 3 is translation), and carries a 24-bit instance id, an
/// 8-bit visibility mask, a 24-bit hit-group / SBT-record offset, and
/// 8 bits of flags. Default-constructed instance is identity-transform
/// and fully visible.
struct AccelInstance
{
    /// 3x4 row-major transform. Identity by default.
    float                  transform[12] {
        1.0F, 0.0F, 0.0F, 0.0F,
        0.0F, 1.0F, 0.0F, 0.0F,
        0.0F, 0.0F, 1.0F, 0.0F
    };
    AccelStructureHandle   blas {};            ///< target BLAS reference
    std::uint32_t          instance_id : 24    { 0 };  ///< exposed to hit shader as InstanceCustomIndex
    std::uint32_t          mask        :  8    { 0xFF }; ///< visibility (TraceRays cullmask & this)
    std::uint32_t          hit_offset  : 24    { 0 };  ///< SBT hit-group offset
    std::uint32_t          flags       :  8    { 0 };  ///< VkGeometryInstanceFlagsKHR bits
};

struct AccelStructureDesc
{
    AccelStructureKind kind { AccelStructureKind::kBottomLevel };
    /// BLAS: one or more triangle geometries.
    std::span<const AccelTriangleGeometry> triangles;
    /// TLAS: one or more BLAS instances (Phase 117). Ignored when
    /// `kind == kBottomLevel`.
    std::span<const AccelInstance>         instances;
    std::string_view debug_name {};
};

/// Phase 656 (M11 W2A / T1.11) — TLAS-coverage host-side candidate.
///
/// `AccelInstance` is 64-byte ABI-locked because it mirrors
/// `VkAccelerationStructureInstanceKHR` bit-for-bit (see
/// `test_rt_descriptors.cpp::AccelInstanceIs64Bytes`).  We CANNOT add an
/// "include me in the TLAS?" flag to that struct without breaking the
/// driver-side layout.
///
/// `TlasInstanceCandidate` is the host-side wrapper that scene-ingest and
/// per-frame TLAS-builder code use BEFORE the driver-facing
/// `AccelInstance` array is materialised.  It carries the same instance
/// payload plus a `tlas_eligible` bool that *defaults to true* so a glTF
/// prim ends up in the TLAS without per-asset setup -- matching the
/// T1.11 moment: a developer drags a glTF asset into a scene and the
/// chrome PBR sphere's reflection picks it up immediately.
///
/// Contract: there is NO geometry-kind filter at the RHI level.  Any
/// candidate with `tlas_eligible == true` flows through to the backend
/// TLAS build.  Higher-level code (scene ingest, ECS render pass) may
/// flip the flag to false for a specific instance (e.g. an editor-only
/// gizmo, a debug visualiser, a ghost-shadow placeholder) but must
/// never gate on the BLAS source asset type.
///
/// Use `build_tlas_instances(candidates, out)` (declared below) to
/// materialise the filtered `AccelInstance` vector ready to pass to
/// `AccelStructureDesc::instances`.
struct TlasInstanceCandidate
{
    /// Driver-facing instance payload (transform, BLAS, id, mask, flags).
    AccelInstance instance {};

    /// Host-side gate.  True by default -- the engine wants new geometry
    /// to enter the TLAS without per-call setup.  Flip to false to
    /// exclude this instance from the TLAS build (the BLAS itself is
    /// unaffected -- only the top-level reference is dropped).
    bool          tlas_eligible { true };
};

/// Phase 118 — RT pipeline shader-stage tags. Mirrors
/// `VkRayTracingShaderGroupTypeKHR` / DXR hit-group categories.
enum class RtShaderStage : std::uint8_t
{
    kRaygen      = 0,   ///< origin generator — one per dispatch
    kMiss        = 1,   ///< invoked when no hit found within tmax
    kClosestHit  = 2,   ///< per-hit-group; runs at closest valid hit
    kAnyHit      = 3,   ///< per-hit-group; runs per intersection (optional)
    kIntersection = 4,  ///< custom primitive shader (optional)
    kCallable    = 5,   ///< callable from any RT stage (optional)
};

/// One entry in the RT pipeline shader-stage list. Shader source is
/// pre-compiled to SPIR-V via `cd::shader::ICompiler` + passed in as
/// a `ShaderModuleHandle` — same contract as graphics + compute
/// pipelines. Decouples `cd::rhi::vulkan` from `cd::shader`.
struct RtShaderEntry
{
    RtShaderStage      stage   { RtShaderStage::kRaygen };
    ShaderModuleHandle module  {};
    std::string_view   entry   { "main" };
    /// Group index assigned by the caller. Same group index ties
    /// closest-hit + any-hit + intersection into one hit group.
    std::uint32_t      group   { 0 };
};

struct RtPipelineDesc
{
    std::span<const RtShaderEntry> shaders;
    /// Maximum recursion depth for `traceRayEXT`. Vulkan + DXR both
    /// guarantee at least 1; runtime queries the actual hardware
    /// limit before pipeline creation.
    std::uint32_t                  max_recursion { 1 };
    /// Maximum payload size in bytes (rayPayloadEXT struct). 64 is the
    /// typical default; tune up for thick payloads.
    std::uint32_t                  max_payload_bytes { 64 };
    /// Maximum hit-attribute size (intersect / any-hit communication).
    std::uint32_t                  max_attribute_bytes { 32 };
    std::string_view               debug_name {};
};

/// Shader binding table region. The actual SBT is one device-local
/// buffer, but each region (raygen / miss / hit / callable) is a
/// sub-range with its own stride. Mirrors the four
/// `VkStridedDeviceAddressRegionKHR` parameters of
/// `vkCmdTraceRaysKHR`.
struct SbtRegion
{
    BufferHandle  buffer {};
    std::uint64_t offset       { 0 };
    std::uint64_t stride_bytes { 0 };
    std::uint64_t size_bytes   { 0 };
};

struct DispatchRaysDesc
{
    std::uint32_t width  { 0 };
    std::uint32_t height { 0 };
    std::uint32_t depth  { 1 };
    /// Phase 118: shader binding table regions. raygen is REQUIRED;
    /// miss + hit are optional but typically present; callable is
    /// optional. A region whose buffer is invalid is skipped.
    SbtRegion     raygen   {};
    SbtRegion     miss     {};
    SbtRegion     hit      {};
    SbtRegion     callable {};
};

// ---- Swapchain ------------------------------------------------------------

/// Colour-space selector for the swapchain. SDR (sRGB) is the canonical
/// default; HDR10 PQ + scRGB linear are opt-in and require the display +
/// platform to support them. Backends fall back to kSrgbNonlinear when
/// the requested colour space isn't available on the surface.
enum class ColorSpace : std::uint8_t
{
    kSrgbNonlinear = 0,  ///< Standard SDR (VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
    kHdr10St2084   = 1,  ///< HDR10 PQ (VK_COLOR_SPACE_HDR10_ST2084_EXT)
    kScrgbLinear   = 2,  ///< scRGB FP16 (VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT)
};

struct SwapchainDesc
{
    void* window_handle { nullptr };   // HWND on Windows, xcb_window_t on Linux, NSView* on macOS
    void* display_handle { nullptr };  // HINSTANCE / Display* / nullptr
    Extent2D extent {};
    std::uint32_t image_count { 2 };
    Format format { Format::kBGRA8Srgb };
    ColorSpace colour_space { ColorSpace::kSrgbNonlinear };
    bool vsync { true };
};

// ---- Bindless texture array (phase837 — ADR W8-BE) -----------------------
//
// Runtime-indexed sampler2D array. The fragment shader (or any shader
// stage) reads `texture(arr[N], uv)` where N is a runtime value loaded
// from a buffer / push constant. Requires:
//   * Vulkan VK_EXT_descriptor_indexing (1.2 core) with
//     UPDATE_AFTER_BIND | PARTIALLY_BOUND | VARIABLE_DESCRIPTOR_COUNT.
//   * D3D12 RESOURCE_BINDING_TIER_3 (heap-indexed SRVs).
//
// `IDevice::create_bindless_texture_array` returns `kNotImplemented` on
// backends that lack the prerequisite — callers fall back to the
// per-prim avg-colour path (W8-BD).
struct BindlessTextureArrayDesc
{
    /// Maximum slot count for the array. Per-prim showcase scenes today
    /// fit comfortably under 256 (Khronos Sponza is 103). Implementations
    /// SHOULD honor `min(slot_count, device.limits().max_descriptor_set_sampled_images)`.
    std::uint32_t slot_count { 256 };

    /// Sampler bound for every slot (one sampler per array; per-slot
    /// sampler variation is not supported in v1). Caller owns the sampler
    /// lifetime; the bindless array does not destroy it.
    SamplerHandle sampler {};

    /// Optional debug name (Vulkan VK_EXT_debug_utils / D3D12 SetName).
    std::string_view debug_name {};
};

// ---- Device limits & capabilities ----------------------------------------

struct DeviceLimits
{
    std::uint32_t max_texture_dimension_1d { 0 };
    std::uint32_t max_texture_dimension_2d { 0 };
    std::uint32_t max_texture_dimension_3d { 0 };
    std::uint32_t max_texture_array_layers { 0 };
    std::uint32_t max_uniform_buffer_range { 0 };
    std::uint32_t max_storage_buffer_range { 0 };
    std::uint32_t max_push_constants_size { 0 };
    std::uint32_t max_bound_descriptor_sets { 0 };
    std::uint32_t max_vertex_input_attributes { 0 };
    std::uint32_t max_vertex_input_bindings { 0 };
    std::uint32_t max_color_attachments { 0 };
    float max_anisotropy { 1.0f };
    std::uint64_t min_uniform_buffer_offset_alignment { 1 };
    std::uint64_t min_storage_buffer_offset_alignment { 1 };
};

struct DeviceFeatures
{
    /// Coarse-grained "the device exposes a ray-tracing path". Set when
    /// the backend sees `VK_KHR_acceleration_structure` +
    /// `VK_KHR_ray_tracing_pipeline` on Vulkan, or DXR Tier 1.0 on D3D12.
    /// Phase 12.D v0.29.0 lights this bit up so callers can branch on
    /// "do I have RT?" before writing RT-dependent code paths. The
    /// actual dispatch_rays / build_acceleration_structure RHI surface
    /// lands in Phase 13.
    bool ray_tracing { false };

    /// Mesh + task shader stage. Set when the backend sees
    /// `VK_EXT_mesh_shader` (preferred) or `VK_NV_mesh_shader`, or
    /// `D3D12_MESH_SHADER_TIER_1`. Same Phase 12.D detection-only
    /// approach as ray_tracing above.
    bool mesh_shader { false };

    /// Pipeline allows ray-query intrinsics in conventional fragment /
    /// compute shaders. `VK_KHR_ray_query` on Vulkan; closely related
    /// to but not the same as ray_tracing. Some hardware exposes ray
    /// query without full ray-tracing-pipeline support.
    bool ray_query { false };

    bool variable_rate_shading { false };
    bool bindless_resources { false };
    bool timestamp_queries { false };
    bool pipeline_statistics_queries { false };
    bool tessellation_shader { false };
    bool geometry_shader { false };
    bool sampler_anisotropy { false };
    bool depth_clamp { false };
    bool dual_source_blend { false };
};

}  // namespace cd::rhi
