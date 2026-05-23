// =============================================================================
// CHROMODYNAMIC — cd/rhi_metal/MetalDevice.hpp
// Phase 8 / Sprint 12 / Wave 97 — Metal backend for cd::rhi (skeleton).
//
// Mirrors the cd::rhi_vulkan factory shape: a single create function
// returns an `IDevice` implementation backed by the platform GPU API.
// Wave 97 ships the public header + a stub factory; the concrete
// MetalDevice (MTLDevice + MTLCommandQueue + MTLLibrary) lands in
// Phase 9 Sprint 1.
//
// Why a separate library from rhi_vulkan: each backend has its own
// dependency chain (Vulkan headers / MoltenVK / Metal frameworks);
// dragging them into rhi_vulkan would force every consumer to link
// the union. Application code calls `cd::rhi::create_native_device()`
// (Wave 97+) which selects the right backend per platform.
//
// Build gate: on non-Apple platforms the factory returns nullptr
// (Wave 79 CoreAudio pattern). On Apple the Phase 9 implementation
// will return a concrete device.
// =============================================================================
#pragma once

#include <cd/core/Result.hpp>
#include <cd/rhi/IDevice.hpp>

#include <cstdint>
#include <memory>
#include <string>

namespace cd::rhi_metal
{

struct MetalCreateInfo
{
    std::string app_name { "CHROMODYNAMIC" };
    /// Prefer a discrete GPU when multiple Metal devices are visible
    /// (eGPU + integrated). On a single-GPU Mac this is a no-op.
    bool prefer_discrete_gpu { true };
    /// Enable Metal API validation layer (debug-only). Mirrors the
    /// Vulkan validation layer toggle on the rhi_vulkan side.
    bool enable_validation { true };
};

/// Construct a Metal-backed IDevice. Returns nullptr (kBackendError)
/// when:
///   * Built on a non-Apple platform.
///   * `MTLCreateSystemDefaultDevice` returns nil (no Metal-capable
///     GPU, or the runtime hasn't been initialised).
///
/// The returned device is fully owned by the caller. Wave 97 ships
/// the stub factory only; Phase 9 Sprint 1 wires the real Metal
/// implementation.
[[nodiscard]] cd::core::Result<std::unique_ptr<cd::rhi::IDevice>>
create_metal_device(MetalCreateInfo info = {});

}  // namespace cd::rhi_metal
