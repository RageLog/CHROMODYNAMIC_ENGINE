// =============================================================================
// CHROMODYNAMIC — engine/render/rhi/src/metal/MetalPipeline.mm
// phase531 — Metal pipeline stub (Objective-C++).
//
// Compiled only when CD_RHI_METAL_ENABLED=ON (Apple platform).
//
// Phase531 scope: placeholder TU. Concrete pipeline objects
// (MTLRenderPipelineState + MTLComputePipelineState, MSL compilation,
// vertex descriptor mapping, blend / depth state) land in Phase 9 Sprint 1.
//
// Architecture note (Phase531):
//   Graphics pipeline:  MTLRenderPipelineDescriptor → newRenderPipelineStateWithDescriptor
//   Compute pipeline:   MTLComputePipelineDescriptor → newComputePipelineStateWithDescriptor
//   Shader modules:     MSL source → newLibraryWithSource / precompiled .metallib
//
// Pipeline layout (PipelineLayoutHandle) maps to the Metal argument-buffer
// tier or to explicit [[buffer(N)]] bindings; the exact strategy will be
// settled in the Phase 9 Sprint 1 ADR for the Metal resource-binding model.
// =============================================================================
#if defined(__APPLE__)

#import <Metal/Metal.h>
#import <Foundation/Foundation.h>

// Objective-C++ TU: intentionally minimal.
// Phase 9 Sprint 1 will add:
//   class MetalGraphicsPipeline { id<MTLRenderPipelineState> pso_; ... };
//   class MetalComputePipeline  { id<MTLComputePipelineState> pso_; ... };

namespace cd::rhi::metal
{
// Placeholder — Phase 9 Sprint 1 wires MTLRenderPipelineState /
// MTLComputePipelineState creation.
}  // namespace cd::rhi::metal

#endif  // __APPLE__
