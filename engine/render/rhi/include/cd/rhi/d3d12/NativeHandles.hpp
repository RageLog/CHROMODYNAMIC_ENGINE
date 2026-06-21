// =============================================================================
// CHROMODYNAMIC — cd/rhi/d3d12/NativeHandles.hpp
//
// Opt-in raw D3D12 handle accessors. Only consumers that NEED ID3D12Device /
// ID3D12CommandQueue / ID3D12GraphicsCommandList directly should include this
// header — the regular cd::rhi public API hides them on purpose so engine code
// stays API-agnostic. This is the D3D12 mirror of cd/rhi/vulkan/NativeHandles.hpp.
//
// Typical clients:
//   * cd::imgui — the DX12 backend wants the device + queue for setup and the
//     native command list at draw time.
//   * Third-party debuggers (PIX programmatic capture, NSight scripts).
//   * Custom D3D12-specific render passes the engine doesn't yet wrap.
//
// Including this header pulls in <d3d12.h> (via a forward-declared interface),
// so it carries the Windows-SDK include cost. Don't include it in public
// engine headers. Windows-only — guard callers with #if defined(_WIN32).
//
// Design (mirrors the Vulkan NativeHandles contract, ADR-20260522-imgui-
// integration):
//   * Free functions in cd::rhi::d3d12 namespace.
//   * dynamic_cast under the hood; returns nullopt / nullptr when the supplied
//     IDevice / ICommandBuffer is from a different backend (e.g. Vulkan). This
//     makes the function safe to call from cross-backend client code.
// =============================================================================
#pragma once

#if !defined(_WIN32)
    #error "cd/rhi/d3d12/NativeHandles.hpp is a Windows-only header."
#endif

#include <optional>

// External Direct3D 12 COM interfaces — forward-declared so this public header
// does not pull in the heavy <d3d12.h>. COM interfaces are reference-counted
// via AddRef()/Release() and are never `delete`d, so their public non-virtual
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

/// Bundle of the D3D12 handles a "raw" client typically needs at init time.
/// Returned by value (cheap; handles are pointer-sized, non-owning views into
/// resources the cd::rhi device keeps alive).
struct D3D12NativeHandles
{
    ID3D12Device* device { nullptr };
    ID3D12CommandQueue* graphics_queue { nullptr };
};

/// Extract native handles from an IDevice. Returns nullopt when the supplied
/// IDevice is not a D3D12-backed device (e.g. a NullDevice or a Vulkan device).
[[nodiscard]] std::optional<D3D12NativeHandles> get_native(cd::rhi::IDevice& dev) noexcept;

/// Extract the underlying ID3D12GraphicsCommandList for the current recording.
/// Returns nullptr when the supplied ICommandBuffer is not from the D3D12
/// backend.
[[nodiscard]] ID3D12GraphicsCommandList* get_native(cd::rhi::ICommandBuffer& cmd) noexcept;

}  // namespace cd::rhi::d3d12
