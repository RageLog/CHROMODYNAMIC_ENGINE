// =============================================================================
// CHROMODYNAMIC — cd/rhi/metal/MetalDevice.hpp
// Phase 8 / Sprint 12 / Wave 97 → phases 1215–1225 — Metal backend for cd::rhi.
//
// Mirrors the cd::rhi_vulkan factory shape: a single create function
// returns an `IDevice` implementation backed by MTLDevice. Full
// IDevice surface written and structurally reviewed (IMPL ~95% on-paper
// per docs/RHI_COMPLETION_STATUS.md §A). GPU verification is
// Mac/Apple-Silicon-gated — 10 authored GPU test binaries behind
// `#if __APPLE__` await an Apple-Clang run; no code work remains,
// only hardware execution. See docs/METAL_MAC_TESTING.md.
//
// Why a separate library from rhi_vulkan: each backend has its own
// dependency chain (Vulkan headers / MoltenVK / Metal frameworks);
// dragging them into rhi_vulkan would force every consumer to link
// the union. Application code calls `cd::rhi::create_native_device()`
// which selects the right backend per platform.
//
// Build gate: on non-Apple platforms `MetalDevice.cpp` provides the
// kBackendInitFailed fallback. On Apple Clang, `MetalDevice.mm`
// compiles the concrete device (CD_RHI_METAL_ENABLED=ON).
// =============================================================================
#pragma once

#include <cd/core/Result.hpp>
#include <cd/rhi/IDevice.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace cd::rhi::metal
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
    /// V-PIPECACHE — optional seed for the device's MTLBinaryArchive. When
    /// non-empty it is the blob from a previous device's
    /// IDevice::get_pipeline_cache_data() (serialised via serializeToURL: into a
    /// temp file, then read back here). A blob from another GPU/driver makes
    /// newBinaryArchiveWithDescriptor: fail and the device falls back to a fresh
    /// archive. Never fails the device.
    std::vector<std::byte> pipeline_cache_blob {};
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

}  // namespace cd::rhi::metal
