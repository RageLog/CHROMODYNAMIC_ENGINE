// =============================================================================
// CHROMODYNAMIC — cd/rhi_d3d12/D3D12Device.hpp
// Phase 8 / Sprint 12 / Wave 97 — D3D12 backend for cd::rhi (skeleton).
//
// Windows-native D3D12 backend factory. Wave 97 ships the public
// header + stub factory; the concrete D3D12Device (D3D12CreateDevice
// + CommandQueue + Root Signature + DescriptorHeap) lands in
// Phase 9 Sprint 2 (after Metal).
//
// Why a D3D12 backend in addition to Vulkan on Windows:
//   * NVAPI / AMD AGS extensions exposed in the native path.
//   * HLSL → DXIL shader cache (faster app startup vs. SPIR-V → DXIL
//     transpilation at load time).
//   * Per-frame profiling tools (PIX, Nsight Graphics) are richer on
//     D3D12 than Vulkan on Windows.
// =============================================================================
#pragma once

#include <cd/core/Result.hpp>
#include <cd/rhi/IDevice.hpp>

#include <cstdint>
#include <memory>
#include <string>

namespace cd::rhi_d3d12
{

struct D3D12CreateInfo
{
    std::string app_name { "CHROMODYNAMIC" };
    /// Prefer a discrete GPU when multiple adapters are visible.
    bool prefer_discrete_gpu { true };
    /// Enable D3D12 debug layer (debug-only). Counterpart to the
    /// Vulkan / Metal validation toggles.
    bool enable_validation { true };
    /// Minimum feature level. Defaults to D3D_FEATURE_LEVEL_12_0
    /// equivalent (mapped to 0x0c00); higher requires raw_int.
    std::uint32_t min_feature_level { 0x0c00 };
};

/// Construct a D3D12-backed IDevice. Returns kBackendInitFailed
/// when:
///   * Built on a non-Windows platform.
///   * `D3D12CreateDevice` fails (driver mismatch, no adapter).
[[nodiscard]] cd::core::Result<std::unique_ptr<cd::rhi::IDevice>>
create_d3d12_device(D3D12CreateInfo info = {});

}  // namespace cd::rhi_d3d12
