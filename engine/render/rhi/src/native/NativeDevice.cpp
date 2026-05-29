// =============================================================================
// CHROMODYNAMIC — engine/render/rhi/native/src/NativeDevice.cpp
//
// Native-backend dispatcher implementation. Order:
//   Windows : D3D12 → Vulkan → null
//   macOS   : Metal → Vulkan (MoltenVK) → null
//   iOS     : Metal → null
//   Linux   : Vulkan → null
// =============================================================================
#include <cd/rhi/NativeDevice.hpp>

#include <cd/rhi/d3d12/D3D12Device.hpp>
#include <cd/rhi/metal/MetalDevice.hpp>

#if defined(CD_ENABLE_VULKAN)
#    include <cd/rhi/vulkan/VulkanDevice.hpp>
#endif

#include <utility>

namespace cd::rhi
{

namespace
{

#if defined(_WIN32)
NativeDeviceResult try_d3d12_()
{
    NativeDeviceResult r;
    auto d = cd::rhi::d3d12::create_d3d12_device({});
    if (d.has_value() && *d)
    {
        r.device = std::move(*d);
        r.kind = NativeBackendKind::kD3D12;
        r.description = "D3D12 backend";
    }
    return r;
}
#endif

#if defined(__APPLE__)
NativeDeviceResult try_metal_()
{
    NativeDeviceResult r;
    auto d = cd::rhi::metal::create_metal_device({});
    if (d.has_value() && *d)
    {
        r.device = std::move(*d);
        r.kind = NativeBackendKind::kMetal;
        r.description = "Metal backend";
    }
    return r;
}
#endif

#if defined(CD_ENABLE_VULKAN)
NativeDeviceResult try_vulkan_()
{
    NativeDeviceResult r;
    auto d = cd::rhi::vulkan::create_vulkan_device({});
    if (d.has_value() && *d)
    {
        r.device = std::move(*d);
        r.kind = NativeBackendKind::kVulkan;
        r.description = "Vulkan backend";
    }
    return r;
}
#endif

}  // namespace

NativeDeviceResult make_native_device()
{
#if defined(_WIN32)
    if (auto r = try_d3d12_(); r.device)
        return r;
#    if defined(CD_ENABLE_VULKAN)
    if (auto r = try_vulkan_(); r.device)
        return r;
#    endif
#elif defined(__APPLE__)
    if (auto r = try_metal_(); r.device)
        return r;
#    if defined(CD_ENABLE_VULKAN)
    if (auto r = try_vulkan_(); r.device)
        return r;
#    endif
#elif defined(__linux__)
#    if defined(CD_ENABLE_VULKAN)
    if (auto r = try_vulkan_(); r.device)
        return r;
#    endif
#endif
    NativeDeviceResult fallback;
    fallback.kind = NativeBackendKind::kNullFallback;
    fallback.description = "no native backend available — every candidate declined";
    return fallback;
}

}  // namespace cd::rhi
