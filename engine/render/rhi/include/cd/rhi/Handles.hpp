// =============================================================================
// CHROMODYNAMIC — cd/rhi/Handles.hpp
// ADR-001 + ADR-005 (Sprint S3.0) — phantom-tagged RHI resource handles.
//
// All RHI resources are referenced via `cd::core::Handle<Tag>`, an opaque
// 64-bit value (32-bit index + 32-bit generation). Tags below ensure that
// e.g. `BufferHandle` cannot be implicitly converted to `TextureHandle` —
// the type system enforces correctness at zero runtime cost.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <cd/core/Handle.hpp>

namespace cd::rhi
{

struct BufferTag
{
};

struct TextureTag
{
};

struct TextureViewTag
{
};

struct SamplerTag
{
};

struct ShaderModuleTag
{
};

struct PipelineLayoutTag
{
};

struct GraphicsPipelineTag
{
};

struct ComputePipelineTag
{
};

struct DescriptorSetLayoutTag
{
};

struct DescriptorSetTag
{
};

struct RenderPassTag
{
};

struct FramebufferTag
{
};

struct SwapchainTag
{
};

struct SemaphoreTag
{
};

struct TimelineSemaphoreTag
{
};

struct FenceTag
{
};

struct QueryPoolTag
{
};

struct AccelStructureTag
{
};

struct RtPipelineTag
{
};

// phase837-W8-BE-rt-bindless-texture-sampling:
// Handle for a runtime-indexed bindless sampler2D array. One handle
// owns kMaxSlots descriptor slots; the caller fills slots one at a
// time via `IDevice::write_bindless_texture_slot`. Backends without
// descriptor_indexing support return kNotImplemented from the
// factory so consumers fall back to a per-prim avg-colour path.
struct BindlessTextureArrayTag
{
};

using BufferHandle = cd::core::Handle<BufferTag>;
using TextureHandle = cd::core::Handle<TextureTag>;
using TextureViewHandle = cd::core::Handle<TextureViewTag>;
using SamplerHandle = cd::core::Handle<SamplerTag>;
using ShaderModuleHandle = cd::core::Handle<ShaderModuleTag>;
using PipelineLayoutHandle = cd::core::Handle<PipelineLayoutTag>;
using GraphicsPipelineHandle = cd::core::Handle<GraphicsPipelineTag>;
using ComputePipelineHandle = cd::core::Handle<ComputePipelineTag>;
using DescriptorSetLayoutHandle = cd::core::Handle<DescriptorSetLayoutTag>;
using DescriptorSetHandle = cd::core::Handle<DescriptorSetTag>;
using RenderPassHandle = cd::core::Handle<RenderPassTag>;
using FramebufferHandle = cd::core::Handle<FramebufferTag>;
using SwapchainHandle = cd::core::Handle<SwapchainTag>;
using SemaphoreHandle = cd::core::Handle<SemaphoreTag>;
using TimelineSemaphoreHandle = cd::core::Handle<TimelineSemaphoreTag>;
using FenceHandle = cd::core::Handle<FenceTag>;
using QueryPoolHandle = cd::core::Handle<QueryPoolTag>;
/// Acceleration structure (BLAS or TLAS). Phase 14.G shipped the
/// handle + descriptor surface; backend implementations are queued
/// for a follow-up wave (Vulkan VK_KHR_acceleration_structure +
/// VK_KHR_ray_tracing_pipeline / D3D12 DXR Tier 1.1).
using AccelStructureHandle = cd::core::Handle<AccelStructureTag>;

/// Phase 134 — ray-tracing pipeline (raygen + miss + hit groups
/// bound together via VkPipelineLayout + an SBT). Vulkan backend
/// implementation arrives with Phase 135.
using RtPipelineHandle = cd::core::Handle<RtPipelineTag>;

/// phase837-W8-BE-rt-bindless-texture-sampling: runtime-indexed
/// sampler2D array handle. See `cd::rhi::BindlessTextureArrayDesc`
/// + `IDevice::create_bindless_texture_array` for the lifecycle.
using BindlessTextureArrayHandle = cd::core::Handle<BindlessTextureArrayTag>;

}  // namespace cd::rhi
