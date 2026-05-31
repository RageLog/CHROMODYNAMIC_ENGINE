// =============================================================================
// CHROMODYNAMIC — engine/render/rhi/src/metal/MetalCommandBuffer.mm
// phase531 — Metal command-buffer stub (Objective-C++).
//
// Compiled only when CD_RHI_METAL_ENABLED=ON (Apple platform).
//
// Phase531 scope: the translation unit exists so the CMake target
// resolves without linker errors. The concrete ICommandBuffer
// implementation (MTLCommandBuffer + MTLRenderCommandEncoder +
// MTLComputeCommandEncoder) lands in Phase 9 Sprint 1.
//
// The MetalDevice stub (MetalDevice.mm) returns a NullCommandBuffer from
// create_command_buffer(), so this file does not need to define any
// symbols used at runtime in phase531 — it is a placeholder TU that
// verifies the Objective-C++ toolchain compiles .mm files correctly.
// =============================================================================
#if defined(__APPLE__)

#import <Metal/Metal.h>
#import <Foundation/Foundation.h>

// Objective-C++ TU: intentionally minimal.
// Phase 9 Sprint 1 will add:
//   class MetalCommandBuffer final : public cd::rhi::ICommandBuffer { ... };
// and export a factory create_metal_command_buffer().

namespace cd::rhi::metal
{
// Placeholder — Phase 9 Sprint 1 wires the real MTLCommandBuffer.
}  // namespace cd::rhi::metal

#endif  // __APPLE__
