// =============================================================================
// CHROMODYNAMIC — cd/rhi/Pipeline.hpp
// ADR-001 (Sprint S3.1) — graphics + compute pipeline descriptors and
// descriptor-set layout bindings.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <cd/rhi/Descriptors.hpp>
#include <cd/rhi/Enums.hpp>
#include <cd/rhi/Format.hpp>
#include <cd/rhi/Handles.hpp>

#include <cstdint>
#include <span>

namespace cd::rhi
{

enum class DescriptorType : std::uint8_t
{
    kSampler,               // bare sampler
    kCombinedImageSampler,  // sampler + sampled image
    kSampledImage,          // texture read-only
    kStorageImage,          // texture read/write
    kUniformBuffer,         // CBV / std140 uniforms
    kStorageBuffer,         // SSBO / UAV buffer
    kUniformBufferDynamic,
    kStorageBufferDynamic,
    kInputAttachment,
    kAccelerationStructure, // Phase 140 — VkAccelerationStructureKHR /
                            // D3D12 RaytracingAccelerationStructure SRV
    // phase837-W8-BE-rt-bindless-texture-sampling:
    // Runtime-indexed sampler2D array. The fragment shader (or any
    // shader stage) reads `texture(arr[N], uv)` where N is a runtime
    // value computed from a buffer load / push constant. Requires
    // VK_EXT_descriptor_indexing on Vulkan (1.2 core) or
    // D3D12_RESOURCE_BINDING_TIER_3 on D3D12. Backends that lack
    // the prerequisite return `kNotImplemented` from
    // `IDevice::create_bindless_texture_array` so callers fall back
    // to the per-prim avg-colour path (W8-BD).
    kBindlessSampledImage,
};

struct DescriptorSetLayoutBinding
{
    std::uint32_t binding { 0 };
    DescriptorType type { DescriptorType::kUniformBuffer };
    std::uint32_t count { 1 };
    ShaderStage stages { ShaderStage::kAllGraphics };
};

struct DescriptorSetLayoutDesc
{
    std::span<const DescriptorSetLayoutBinding> bindings;
};

struct PushConstantRange
{
    ShaderStage stages { ShaderStage::kVertex };
    std::uint32_t offset { 0 };
    std::uint32_t size { 0 };
};

struct PipelineLayoutDesc
{
    std::span<const DescriptorSetLayoutHandle> set_layouts;
    std::span<const PushConstantRange> push_constants;
};

struct GraphicsPipelineDesc
{
    PipelineLayoutHandle layout {};
    ShaderModuleHandle vertex_shader {};
    ShaderModuleHandle fragment_shader {};
    ShaderModuleHandle geometry_shader {};
    ShaderModuleHandle tess_ctrl_shader {};
    ShaderModuleHandle tess_eval_shader {};

    /// phase766 — Mesh-shading pipeline (Shader Model 6.5 / VK_EXT_mesh_shader).
    /// When `mesh_shader` is valid the backend builds a mesh-shading PSO
    /// (D3D12 PIPELINE_STATE_STREAM with MS+AS subobjects, or Vulkan
    /// VkGraphicsPipelineCreateInfo with the mesh-stage chain). The
    /// `amplification_shader` (D3D12) / task shader (Vulkan) is optional.
    /// `vertex_shader` and the vertex-input fields are ignored when a mesh
    /// shader is present — DispatchMesh / vkCmdDrawMeshTasksEXT drives the
    /// pipeline directly.
    ShaderModuleHandle mesh_shader {};
    ShaderModuleHandle amplification_shader {};

    std::span<const VertexBinding> vertex_bindings;
    std::span<const VertexAttribute> vertex_attributes;

    PrimitiveTopology topology { PrimitiveTopology::kTriangleList };
    RasterState raster {};
    DepthStencilState depth_stencil {};
    std::span<const BlendAttachmentState> blend_attachments;
    SampleCount samples { SampleCount::k1 };

    // Output format declaration (used for dynamic rendering & PSO caches).
    std::span<const Format> color_attachment_formats;
    Format depth_attachment_format { Format::kUndefined };
    Format stencil_attachment_format { Format::kUndefined };
};

struct ComputePipelineDesc
{
    PipelineLayoutHandle layout {};
    ShaderModuleHandle shader {};
};

// ---- Pipeline kind discriminator (Phase 765 W2A — F5) ---------------------
//
// Identifies which pipeline variant a backend should construct from a
// descriptor. Graphics + Compute are the classic two; MeshShader is the
// Nanite-class virtual-geometry path that replaces the vertex-input
// assembler with a task -> mesh shader chain (Vulkan VK_EXT_mesh_shader,
// D3D12 mesh-shader tier). Backends without mesh-shader support reject
// `kMeshShader` with kNotImplemented; callers gate on
// `device.features().mesh_shader` before constructing one.
enum class PipelineKind : std::uint8_t
{
    kGraphics = 0,
    kCompute,
    kMeshShader,
};

// ---- Mesh-shader pipeline descriptor (Phase 765 W2A — F5) -----------------
//
// Mesh-shader pipelines skip the input assembler / vertex shader stage
// entirely and start from a task (amplification) shader that emits work
// for a mesh shader, which in turn emits triangles to the rasterizer.
// The descriptor mirrors `GraphicsPipelineDesc` for state shared with
// the rasterizer (raster, depth-stencil, blend, attachment formats) but
// drops vertex_input / topology fields that don't apply.
//
// `task_shader` is optional -- a pipeline may consist of mesh + fragment
// only. `fragment_shader` is required for any rasterized output.
struct MeshPipelineDesc
{
    PipelineLayoutHandle layout {};
    ShaderModuleHandle task_shader {};      // optional -- amplification stage
    ShaderModuleHandle mesh_shader {};      // required -- primitive output stage
    ShaderModuleHandle fragment_shader {};  // required for rasterized output

    RasterState raster {};
    DepthStencilState depth_stencil {};
    std::span<const BlendAttachmentState> blend_attachments;
    SampleCount samples { SampleCount::k1 };

    // Output format declaration (used for dynamic rendering & PSO caches).
    // Same conventions as GraphicsPipelineDesc.
    std::span<const Format> color_attachment_formats;
    Format depth_attachment_format { Format::kUndefined };
    Format stencil_attachment_format { Format::kUndefined };
};

// ---- Descriptor writes (S3.4) ---------------------------------------------
// A single DescriptorWrite updates one binding of a descriptor set. Only the
// fields relevant to `type` are consulted; the rest may stay default-init'd.
//   * Buffer descriptors (UB/SSBO/UBO_dynamic/SSBO_dynamic): use `buffer`,
//     `buffer_offset`, `buffer_range`.
//   * Image descriptors (sampled/storage/input attachment): use `view`.
//   * Combined image-sampler: use both `view` and `sampler`.
//   * Bare sampler: use `sampler`.
struct DescriptorWrite
{
    std::uint32_t binding { 0 };
    std::uint32_t array_element { 0 };
    DescriptorType type { DescriptorType::kUniformBuffer };

    // Buffer-side
    BufferHandle buffer {};
    std::uint64_t buffer_offset { 0 };
    std::uint64_t buffer_range { 0 };  // 0 → whole buffer

    // Image / sampler-side
    TextureViewHandle view {};
    SamplerHandle sampler {};

    // Acceleration structure (Phase 140) — populated when
    // `type == DescriptorType::kAccelerationStructure`. Backends
    // chain a VkWriteDescriptorSetAccelerationStructureKHR (Vulkan)
    // or place an AccelerationStructure SRV (D3D12) per binding.
    AccelStructureHandle accel {};
};

}  // namespace cd::rhi
