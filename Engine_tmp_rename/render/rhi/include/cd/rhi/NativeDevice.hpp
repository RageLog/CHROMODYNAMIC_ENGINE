// =============================================================================
// CHROMODYNAMIC — cd/rhi/NativeDevice.hpp
// Phase 8 / Sprint 12 / Wave 97 — unified GPU backend dispatcher.
//
// Picks the best available IDevice backend for the current host:
//   Windows : D3D12 → Vulkan → null fallback
//   macOS   : Metal → Vulkan (MoltenVK) → null fallback
//   iOS     : Metal → null fallback
//   Linux   : Vulkan → null fallback
//
// Pattern mirrors `cd::audio::make_native_audio_backend()` from
// Wave 34. Application code calls `make_native_device()` and gets
// the right thing without knowing which API it landed on. A
// `NativeBackendKind` enum tells diagnostic tooling what was
// selected (useful for sample logs / about screens).
//
// Native factory returns nullptr in `backend` ONLY when every
// candidate failed; a debug message in last_error explains which
// backend was attempted and why each one declined.
//
// Wave 97 ships the dispatcher with Metal + D3D12 backends in stub
// form. Phase 9 Sprint 1 / 2 land the concrete implementations,
// and the existing call sites pick them up without API change.
// =============================================================================
#pragma once

#include <cd/core/Result.hpp>
#include <cd/rhi/IDevice.hpp>

#include <cstdint>
#include <memory>
#include <string>

namespace cd::rhi
{

enum class NativeBackendKind : std::uint8_t
{
    kD3D12,
    kMetal,
    kVulkan,
    kNullFallback,
};

struct NativeDeviceResult
{
    std::unique_ptr<cd::rhi::IDevice> device;
    NativeBackendKind kind { NativeBackendKind::kNullFallback };
    /// Diagnostic for sample logs / about screens.
    std::string description;
};

/// Best-available native GPU backend for the host. `kind` indicates
/// which backend was picked. A null `device` with `kNullFallback`
/// means every candidate failed (the engine should run in a
/// software-rasterised / image-only mode in that case).
[[nodiscard]] NativeDeviceResult make_native_device();

}  // namespace cd::rhi
