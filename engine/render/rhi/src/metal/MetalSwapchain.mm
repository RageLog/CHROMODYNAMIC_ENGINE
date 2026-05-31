// =============================================================================
// CHROMODYNAMIC — engine/render/rhi/src/metal/MetalSwapchain.mm
// phase531 — Metal swapchain stub (Objective-C++).
//
// Compiled only when CD_RHI_METAL_ENABLED=ON (Apple platform).
//
// Phase531 scope: placeholder TU that imports the Metal + QuartzCore
// frameworks so the toolchain validates the Objective-C++ compilation
// pipeline. Concrete swapchain (CAMetalLayer + MTLDrawable acquire/present
// + triple-buffering with semaphores) lands in Phase 9 Sprint 1.
//
// On macOS the swapchain owns a CAMetalLayer attached to an NSView (or
// a CALayer directly when running headless in the test harness). iOS uses
// the same CAMetalLayer path. Both will be dispatched from a single TU
// gated on __APPLE__.
// =============================================================================
#if defined(__APPLE__)

#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#import <Foundation/Foundation.h>

// Objective-C++ TU: intentionally minimal.
// Phase 9 Sprint 1 will add:
//   struct MetalSwapchainDesc { ... };
//   class MetalSwapchain { CAMetalLayer* layer_; ... };

namespace cd::rhi::metal
{
// Placeholder — Phase 9 Sprint 1 wires the real CAMetalLayer swapchain.
}  // namespace cd::rhi::metal

#endif  // __APPLE__
