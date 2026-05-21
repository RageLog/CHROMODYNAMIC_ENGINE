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

}  // namespace cd::rhi
