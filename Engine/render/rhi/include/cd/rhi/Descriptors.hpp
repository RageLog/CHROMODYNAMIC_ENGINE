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
    FrontFace front_face { FrontFace::kCounterClockwise };
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

// ---- Swapchain ------------------------------------------------------------

struct SwapchainDesc
{
    void* window_handle { nullptr };   // HWND on Windows, xcb_window_t on Linux, NSView* on macOS
    void* display_handle { nullptr };  // HINSTANCE / Display* / nullptr
    Extent2D extent {};
    std::uint32_t image_count { 2 };
    Format format { Format::kBGRA8Srgb };
    bool vsync { true };
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
    bool ray_tracing { false };
    bool mesh_shader { false };
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
