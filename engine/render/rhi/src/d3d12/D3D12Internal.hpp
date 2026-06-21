// =============================================================================
// CHROMODYNAMIC — engine/render/rhi/src/d3d12/D3D12Internal.hpp
//
// Internal (src-private) bridges for the D3D12 backend. The concrete
// D3D12Device / D3D12CommandBuffer classes live in an anonymous namespace
// inside D3D12Device.cpp, so a dynamic_cast to them can only run in that TU.
// NativeHandles.cpp calls through these trampolines instead of needing the
// full class layouts — mirrors src/vulkan/VulkanInternal.hpp.
//
// Windows-only.
// =============================================================================
#pragma once

#if defined(_WIN32)

// External Direct3D 12 COM interfaces — forward-declared (see NativeHandles.hpp).
// Reference-counted via Release(), never deleted, so the public non-virtual
// destructor is by design and not ours to change.
// NOLINTBEGIN(cppcoreguidelines-virtual-class-destructor)
struct ID3D12Device;
struct ID3D12CommandQueue;
struct ID3D12GraphicsCommandList;
// NOLINTEND(cppcoreguidelines-virtual-class-destructor)

namespace cd::rhi
{
class IDevice;
class ICommandBuffer;
}  // namespace cd::rhi

namespace cd::rhi::d3d12
{

/// Bridge for `cd::rhi::d3d12::get_native(IDevice&)`. Defined in
/// D3D12Device.cpp because that's where the concrete D3D12Device class is
/// visible. Returns false when the IDevice is not D3D12-backed (out
/// parameters untouched).
[[nodiscard]] bool try_fill_d3d12_native_handles(
    cd::rhi::IDevice& dev,
    ID3D12Device** out_device,
    ID3D12CommandQueue** out_graphics_queue
) noexcept;

/// Bridge for `cd::rhi::d3d12::get_native(ICommandBuffer&)`. Returns the
/// recording command list, or nullptr when the buffer is not D3D12-backed.
[[nodiscard]] ID3D12GraphicsCommandList* native_command_list(
    cd::rhi::ICommandBuffer& cmd
) noexcept;

}  // namespace cd::rhi::d3d12

#endif  // _WIN32
