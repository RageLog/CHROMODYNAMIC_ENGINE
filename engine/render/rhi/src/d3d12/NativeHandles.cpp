// =============================================================================
// CHROMODYNAMIC — engine/render/rhi/src/d3d12/NativeHandles.cpp
//
// D3D12Device / D3D12CommandBuffer live in an anonymous namespace inside
// D3D12Device.cpp. We can't dynamic_cast to them from this TU, so we route
// through the tiny trampolines declared in D3D12Internal.hpp (defined in
// D3D12Device.cpp where the concrete types are visible) — mirrors the Vulkan
// backend's NativeHandles.cpp.
// =============================================================================
#if defined(_WIN32)

    #include "D3D12Internal.hpp"

    #include <cd/rhi/ICommandBuffer.hpp>
    #include <cd/rhi/IDevice.hpp>
    #include <cd/rhi/d3d12/NativeHandles.hpp>

namespace cd::rhi::d3d12
{

std::optional<D3D12NativeHandles> get_native(cd::rhi::IDevice& dev) noexcept
{
    D3D12NativeHandles out {};
    if (!try_fill_d3d12_native_handles(dev, &out.device, &out.graphics_queue))
    {
        return std::nullopt;
    }
    return out;
}

ID3D12GraphicsCommandList* get_native(cd::rhi::ICommandBuffer& cmd) noexcept
{
    return native_command_list(cmd);
}

}  // namespace cd::rhi::d3d12

#endif  // _WIN32
