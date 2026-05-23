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
};

}  // namespace cd::rhi
