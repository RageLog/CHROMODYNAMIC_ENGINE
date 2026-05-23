// =============================================================================
// CHROMODYNAMIC — engine/render/rhi_d3d12/src/D3D12Device.cpp
//
// D3D12 backend skeleton. On non-Windows platforms returns
// kBackendInitFailed. On Windows compiles to a stub for Wave 97;
// Phase 9 Sprint 2 swaps in the concrete D3D12Device + CommandQueue
// + RootSignature + DescriptorHeap wiring.
// =============================================================================
#include <cd/rhi_d3d12/D3D12Device.hpp>

#include <cd/rhi/IDevice.hpp>

namespace cd::rhi_d3d12
{

cd::core::Result<std::unique_ptr<cd::rhi::IDevice>>
create_d3d12_device(D3D12CreateInfo /*info*/)
{
#if defined(_WIN32)
    return std::unexpected(cd::rhi::rhi_errors::make(
        cd::rhi::rhi_errors::Code::kNotImplemented,
        "D3D12 backend skeleton — Phase 9 Sprint 2 wires the concrete impl"));
#else
    return std::unexpected(cd::rhi::rhi_errors::make(
        cd::rhi::rhi_errors::Code::kBackendInitFailed,
        "D3D12 backend only available on Windows"));
#endif
}

}  // namespace cd::rhi_d3d12
