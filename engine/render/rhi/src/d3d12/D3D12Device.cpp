// =============================================================================
// CHROMODYNAMIC — engine/render/rhi_d3d12/src/D3D12Device.cpp
//
// Phase 12.B / v0.27.0 — D3D12 boot-only backend. Creates an
// ID3D12Device + DXGIFactory + adapter selection + direct command
// queue + a fence so wait_idle() works. The rest of the IDevice
// surface (50 virtuals total) is stubbed to return kNotImplemented;
// subsequent v0.27.x patches will fill in PSO, draw, buffer/texture
// upload, swapchain, present.
//
// This commit ships "the D3D12 path is reachable, the device + queue
// exist on this machine, adapter introspection works". That's the
// smallest defensible milestone for a backend the marathon author
// cannot rely on a human to pixel-check.
// =============================================================================
#include <algorithm>
#include <cd/rhi/d3d12/D3D12Device.hpp>

#include <cd/rhi/IDevice.hpp>
#include <cd/rhi/ICommandBuffer.hpp>

// D12 (phase1184): in-device GLSL/SPIR-V/HLSL → DXIL cross-compile.
#include <cd/rhi/d3d12/D3D12ShaderToolchain.hpp>
#include <cd/gluon/ModuleRegistry.hpp>
#include <cd/shader/Compiler.hpp>

#if defined(_WIN32)
    // Win32 platform headers first so D3D12 + DXGI macros resolve cleanly.
    // The samples already define WIN32_LEAN_AND_MEAN + NOMINMAX globally;
    // we mirror that here so this TU is independent of include order.
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    // IID_PPV_ARGS + __uuidof are MS-language extensions; clang-cl under
    // -Werror -Wlanguage-extension-token rejects them. This is the only
    // legitimate D3D12 entry point in the SDK, so we silence the warning
    // for the duration of these system includes.
    #if defined(__clang__)
        #pragma clang diagnostic push
        #pragma clang diagnostic ignored "-Wlanguage-extension-token"
    #endif
    #include <windows.h>
    #include <wrl/client.h>
    #include <d3d12.h>
    #include <d3dcompiler.h>  // D3D12SerializeRootSignature
    #include <dxgi1_6.h>
    #include <dxgidebug.h>
#endif

#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace cd::rhi::d3d12
{

#if defined(_WIN32)

namespace
{

using Microsoft::WRL::ComPtr;

// Forward declaration — D3D12CommandBuffer is defined after D3D12Device
// but submit(const SubmitDesc&) inside D3D12Device casts ICommandBuffer*
// to D3D12CommandBuffer*. The cast is safe at runtime because only
// D3D12CommandBuffer instances are submitted on the D3D12 device path.
class D3D12CommandBuffer;

// ---- Format mapping -----------------------------------------------------
//
// D-FORMAT-MAP (Backend-to-100 Wave 1): full Format → DXGI_FORMAT table,
// mirroring the Vulkan reference `map_format` (VulkanDevice.cpp:286). The
// historical table covered only ~13 of the interface Formats; the rest fell
// to UNKNOWN and were silently REJECTED by create_texture / create_swapchain.
// Now every renderable / vertex / depth / block-compressed Format the engine
// exposes resolves to a real DXGI value (the only intentional UNKNOWN returns
// are kUndefined / kCount, which carry no pixel format).
//
// Depth formats: the DSV/RTV path in create_texture_view + create_swapchain
// already passes the mapped (typed) format straight into the depth-stencil
// view, matching the prior kD32Float / kD24UnormS8Uint behaviour. When a depth
// texture is ALSO sampled as an SRV, D3D12 requires the *resource* to be
// created TYPELESS and the SRV to use the R-typed sibling — that resource-side
// TYPELESS promotion is handled separately at create_texture; this map returns
// the canonical typed DSV format so the existing depth attachment path is
// unchanged.
[[nodiscard]] DXGI_FORMAT to_dxgi_format(cd::rhi::Format f) noexcept
{
    using F = cd::rhi::Format;
    switch (f)
    {
        // 8-bit single
        case F::kR8Unorm:     return DXGI_FORMAT_R8_UNORM;
        case F::kR8Snorm:     return DXGI_FORMAT_R8_SNORM;
        case F::kR8Uint:      return DXGI_FORMAT_R8_UINT;
        case F::kR8Sint:      return DXGI_FORMAT_R8_SINT;
        // 8-bit dual
        case F::kRG8Unorm:    return DXGI_FORMAT_R8G8_UNORM;
        case F::kRG8Snorm:    return DXGI_FORMAT_R8G8_SNORM;
        case F::kRG8Uint:     return DXGI_FORMAT_R8G8_UINT;
        case F::kRG8Sint:     return DXGI_FORMAT_R8G8_SINT;
        // 8-bit quad
        case F::kRGBA8Unorm:  return DXGI_FORMAT_R8G8B8A8_UNORM;
        case F::kRGBA8Snorm:  return DXGI_FORMAT_R8G8B8A8_SNORM;
        case F::kRGBA8Uint:   return DXGI_FORMAT_R8G8B8A8_UINT;
        case F::kRGBA8Sint:   return DXGI_FORMAT_R8G8B8A8_SINT;
        case F::kRGBA8Srgb:   return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
        case F::kBGRA8Unorm:  return DXGI_FORMAT_B8G8R8A8_UNORM;
        case F::kBGRA8Srgb:   return DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
        // 16-bit single
        case F::kR16Unorm:    return DXGI_FORMAT_R16_UNORM;
        case F::kR16Snorm:    return DXGI_FORMAT_R16_SNORM;
        case F::kR16Uint:     return DXGI_FORMAT_R16_UINT;
        case F::kR16Sint:     return DXGI_FORMAT_R16_SINT;
        case F::kR16Float:    return DXGI_FORMAT_R16_FLOAT;
        // 16-bit dual
        case F::kRG16Unorm:   return DXGI_FORMAT_R16G16_UNORM;
        case F::kRG16Snorm:   return DXGI_FORMAT_R16G16_SNORM;
        case F::kRG16Uint:    return DXGI_FORMAT_R16G16_UINT;
        case F::kRG16Sint:    return DXGI_FORMAT_R16G16_SINT;
        case F::kRG16Float:   return DXGI_FORMAT_R16G16_FLOAT;
        // 16-bit quad
        case F::kRGBA16Unorm: return DXGI_FORMAT_R16G16B16A16_UNORM;
        case F::kRGBA16Snorm: return DXGI_FORMAT_R16G16B16A16_SNORM;
        case F::kRGBA16Uint:  return DXGI_FORMAT_R16G16B16A16_UINT;
        case F::kRGBA16Sint:  return DXGI_FORMAT_R16G16B16A16_SINT;
        case F::kRGBA16Float: return DXGI_FORMAT_R16G16B16A16_FLOAT;
        // 32-bit
        case F::kR32Uint:     return DXGI_FORMAT_R32_UINT;
        case F::kR32Sint:     return DXGI_FORMAT_R32_SINT;
        case F::kR32Float:    return DXGI_FORMAT_R32_FLOAT;
        case F::kRG32Uint:    return DXGI_FORMAT_R32G32_UINT;
        case F::kRG32Sint:    return DXGI_FORMAT_R32G32_SINT;
        case F::kRG32Float:   return DXGI_FORMAT_R32G32_FLOAT;
        case F::kRGB32Uint:   return DXGI_FORMAT_R32G32B32_UINT;
        case F::kRGB32Sint:   return DXGI_FORMAT_R32G32B32_SINT;
        case F::kRGB32Float:  return DXGI_FORMAT_R32G32B32_FLOAT;
        case F::kRGBA32Uint:  return DXGI_FORMAT_R32G32B32A32_UINT;
        case F::kRGBA32Sint:  return DXGI_FORMAT_R32G32B32A32_SINT;
        case F::kRGBA32Float: return DXGI_FORMAT_R32G32B32A32_FLOAT;
        // Packed / HDR
        case F::kR11G11B10Float: return DXGI_FORMAT_R11G11B10_FLOAT;
        case F::kRGB10A2Unorm:   return DXGI_FORMAT_R10G10B10A2_UNORM;
        case F::kRGB10A2Uint:    return DXGI_FORMAT_R10G10B10A2_UINT;
        case F::kRGB9E5Float:    return DXGI_FORMAT_R9G9B9E5_SHAREDEXP;
        // Depth / stencil (typed DSV formats; see TYPELESS note above)
        case F::kD16Unorm:       return DXGI_FORMAT_D16_UNORM;
        case F::kD32Float:       return DXGI_FORMAT_D32_FLOAT;
        case F::kD24UnormS8Uint: return DXGI_FORMAT_D24_UNORM_S8_UINT;
        case F::kD32FloatS8Uint: return DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
        case F::kS8Uint:         return DXGI_FORMAT_R8_UINT;  // no DXGI S8-only; R8_UINT sibling
        // Block-compressed (BC1-BC7)
        case F::kBC1RGBUnorm:
        case F::kBC1RGBAUnorm:   return DXGI_FORMAT_BC1_UNORM;
        case F::kBC1RGBSrgb:
        case F::kBC1RGBASrgb:    return DXGI_FORMAT_BC1_UNORM_SRGB;
        case F::kBC2Unorm:       return DXGI_FORMAT_BC2_UNORM;
        case F::kBC2Srgb:        return DXGI_FORMAT_BC2_UNORM_SRGB;
        case F::kBC3Unorm:       return DXGI_FORMAT_BC3_UNORM;
        case F::kBC3Srgb:        return DXGI_FORMAT_BC3_UNORM_SRGB;
        case F::kBC4Unorm:       return DXGI_FORMAT_BC4_UNORM;
        case F::kBC4Snorm:       return DXGI_FORMAT_BC4_SNORM;
        case F::kBC5Unorm:       return DXGI_FORMAT_BC5_UNORM;
        case F::kBC5Snorm:       return DXGI_FORMAT_BC5_SNORM;
        case F::kBC6HUFloat:     return DXGI_FORMAT_BC6H_UF16;
        case F::kBC6HSFloat:     return DXGI_FORMAT_BC6H_SF16;
        case F::kBC7Unorm:       return DXGI_FORMAT_BC7_UNORM;
        case F::kBC7Srgb:        return DXGI_FORMAT_BC7_UNORM_SRGB;
        // No pixel format — intentional UNKNOWN.
        case F::kUndefined:
        case F::kCount:
        default:                 return DXGI_FORMAT_UNKNOWN;
    }
}

// D-HDR-SWAPCHAIN (Backend-to-100 Wave 1): map the engine ColorSpace to the
// DXGI colour-space the swapchain's SetColorSpace1 understands. Mirrors the
// Vulkan reference's VkColorSpaceKHR selection:
//   kSrgbNonlinear → RGB_FULL_G22_NONE_P709     (sRGB / SDR; gamma 2.2, BT.709)
//   kHdr10St2084   → RGB_FULL_G2084_NONE_P2020  (HDR10 PQ; ST.2084, BT.2020)
//   kScrgbLinear   → RGB_FULL_G10_NONE_P709      (scRGB FP16 linear, BT.709)
[[nodiscard]] DXGI_COLOR_SPACE_TYPE
to_dxgi_color_space(cd::rhi::ColorSpace cs) noexcept
{
    switch (cs)
    {
        case cd::rhi::ColorSpace::kHdr10St2084:
            return DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020;
        case cd::rhi::ColorSpace::kScrgbLinear:
            return DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709;
        case cd::rhi::ColorSpace::kSrgbNonlinear:
        default:
            return DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;
    }
}

// D3 (phase1186): true when a DSV format carries a stencil aspect, so
// begin_render_pass only raises D3D12_CLEAR_FLAG_STENCIL on formats that
// actually have stencil bits (clearing stencil on a depth-only format is a
// debug-layer error).
[[nodiscard]] bool dsv_format_has_stencil(DXGI_FORMAT f) noexcept
{
    switch (f)
    {
        case DXGI_FORMAT_D24_UNORM_S8_UINT:
        case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
            return true;
        default:
            return false;
    }
}

// D5 (phase1186): translate the engine CompareOp into a D3D12 comparison
// function for the PSO depth/stencil state. Mirrors the Vulkan map_compare
// used by the reference backend so the depth-test semantics are identical.
[[nodiscard]] D3D12_COMPARISON_FUNC
to_d3d12_compare_func(cd::rhi::CompareOp op) noexcept
{
    using C = cd::rhi::CompareOp;
    switch (op)
    {
        case C::kNever:        return D3D12_COMPARISON_FUNC_NEVER;
        case C::kLess:         return D3D12_COMPARISON_FUNC_LESS;
        case C::kEqual:        return D3D12_COMPARISON_FUNC_EQUAL;
        case C::kLessEqual:    return D3D12_COMPARISON_FUNC_LESS_EQUAL;
        case C::kGreater:      return D3D12_COMPARISON_FUNC_GREATER;
        case C::kNotEqual:     return D3D12_COMPARISON_FUNC_NOT_EQUAL;
        case C::kGreaterEqual: return D3D12_COMPARISON_FUNC_GREATER_EQUAL;
        case C::kAlways:       return D3D12_COMPARISON_FUNC_ALWAYS;
    }
    return D3D12_COMPARISON_FUNC_LESS;
}

// D5 (phase1186): set StencilEnable + front/back StencilOpDesc on a
// D3D12_DEPTH_STENCIL_DESC. The engine `DepthStencilState` currently carries
// only a `stencil_test` toggle (no per-face fail/depthFail/pass ops, func, or
// read/write masks) — exactly like the Vulkan reference, whose
// VkPipelineDepthStencilStateCreateInfo leaves `.front = {}` / `.back = {}`
// zero. D3D12 stencil-op enums start at 1 (0 is invalid), so when stencil is
// enabled we install valid pass-through defaults (KEEP / ALWAYS + the D3D12
// default read/write masks) rather than the all-zero Vulkan layout. When the
// engine surface grows per-face StencilOpState fields this is the single
// translation point that consumes them.
inline void fill_d3d12_stencil_state(D3D12_DEPTH_STENCIL_DESC& ds,
                                     bool stencil_test) noexcept
{
    ds.StencilEnable = stencil_test ? TRUE : FALSE;
    ds.StencilReadMask  = D3D12_DEFAULT_STENCIL_READ_MASK;
    ds.StencilWriteMask = D3D12_DEFAULT_STENCIL_WRITE_MASK;
    const D3D12_DEPTH_STENCILOP_DESC op {
        .StencilFailOp      = D3D12_STENCIL_OP_KEEP,
        .StencilDepthFailOp = D3D12_STENCIL_OP_KEEP,
        .StencilPassOp      = D3D12_STENCIL_OP_KEEP,
        .StencilFunc        = D3D12_COMPARISON_FUNC_ALWAYS,
    };
    ds.FrontFace = op;
    ds.BackFace  = op;
}

// D4 (phase1187): per-attachment blend. Translate the engine BlendFactor /
// BlendOp enums to their D3D12 equivalents, mirroring the Vulkan reference's
// map_blend_factor / map_blend_op so the blend math is identical across
// backends. The color-channel maps use the plain SRC/DEST factors; the
// alpha-channel variants (SrcBlendAlpha/DestBlendAlpha) reuse the same map
// because D3D12, like Vulkan, requires the *_ALPHA blend enums to be the
// color-channel enums whose alpha components are taken — i.e. the same
// D3D12_BLEND token is valid in both the color and alpha slots.
[[nodiscard]] D3D12_BLEND
to_d3d12_blend(cd::rhi::BlendFactor f) noexcept
{
    using BF = cd::rhi::BlendFactor;
    switch (f)
    {
        case BF::kZero:                 return D3D12_BLEND_ZERO;
        case BF::kOne:                  return D3D12_BLEND_ONE;
        case BF::kSrcColor:             return D3D12_BLEND_SRC_COLOR;
        case BF::kOneMinusSrcColor:     return D3D12_BLEND_INV_SRC_COLOR;
        case BF::kDstColor:             return D3D12_BLEND_DEST_COLOR;
        case BF::kOneMinusDstColor:     return D3D12_BLEND_INV_DEST_COLOR;
        case BF::kSrcAlpha:             return D3D12_BLEND_SRC_ALPHA;
        case BF::kOneMinusSrcAlpha:     return D3D12_BLEND_INV_SRC_ALPHA;
        case BF::kDstAlpha:             return D3D12_BLEND_DEST_ALPHA;
        case BF::kOneMinusDstAlpha:     return D3D12_BLEND_INV_DEST_ALPHA;
        case BF::kConstantColor:        return D3D12_BLEND_BLEND_FACTOR;
        case BF::kOneMinusConstantColor:return D3D12_BLEND_INV_BLEND_FACTOR;
        // D3D12 has no separate constant-alpha blend; the BlendFactor RGBA
        // (set via OMSetBlendFactor) covers the constant-alpha case too —
        // map to the same BLEND_FACTOR token the Vulkan constant-color path
        // mirrors. This keeps the enum total and avoids an invalid value.
        case BF::kConstantAlpha:        return D3D12_BLEND_BLEND_FACTOR;
        case BF::kOneMinusConstantAlpha:return D3D12_BLEND_INV_BLEND_FACTOR;
        case BF::kSrcAlphaSaturate:     return D3D12_BLEND_SRC_ALPHA_SAT;
    }
    return D3D12_BLEND_ZERO;
}

[[nodiscard]] D3D12_BLEND_OP
to_d3d12_blend_op(cd::rhi::BlendOp o) noexcept
{
    using BO = cd::rhi::BlendOp;
    switch (o)
    {
        case BO::kAdd:             return D3D12_BLEND_OP_ADD;
        case BO::kSubtract:        return D3D12_BLEND_OP_SUBTRACT;
        case BO::kReverseSubtract: return D3D12_BLEND_OP_REV_SUBTRACT;
        case BO::kMin:             return D3D12_BLEND_OP_MIN;
        case BO::kMax:             return D3D12_BLEND_OP_MAX;
    }
    return D3D12_BLEND_OP_ADD;
}

// Pick the D3D12 heap type from the engine's MemoryUsage hint.
// DEFAULT  ← kGpuOnly (device-local, GPU-visible only)
// UPLOAD   ← kCpuToGpu (write-combine, host-visible)
// READBACK ← kGpuToCpu (cached, host-visible)
// For kAuto on a buffer we pick UPLOAD so the common "small constant
// buffer / staging" case works without a separate transfer; a future
// optimization can route bulk vertex/index buffers through a staging
// upload + DEFAULT residency.
[[nodiscard]] D3D12_HEAP_TYPE heap_type_for(cd::rhi::MemoryUsage m) noexcept
{
    using M = cd::rhi::MemoryUsage;
    switch (m)
    {
        case M::kGpuOnly:         return D3D12_HEAP_TYPE_DEFAULT;
        case M::kCpuToGpu:        return D3D12_HEAP_TYPE_UPLOAD;
        case M::kGpuToCpu:        return D3D12_HEAP_TYPE_READBACK;
        case M::kCpuRandomAccess: return D3D12_HEAP_TYPE_UPLOAD;
        case M::kAuto:            return D3D12_HEAP_TYPE_UPLOAD;
    }
    return D3D12_HEAP_TYPE_UPLOAD;
}

/// Phase 13.C v0.32.0 D3D12 backend: buffer + texture + swapchain wired,
/// plus a minimal command-list path sufficient for `hello_d3d12_clear`.
/// Other resource types (PSO, descriptor sets, shaders) remain stubbed
/// and surface kNotImplemented.
class D3D12Device final : public cd::rhi::IDevice
{
public:
    D3D12Device() noexcept = default;

    [[nodiscard]] HRESULT initialize(const D3D12CreateInfo& info)
    {
        // ---- 1. Debug layer (Debug builds only) ----------------------------
        if (info.enable_validation)
        {
            ComPtr<ID3D12Debug> debug;
            if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug))))
            {
                debug->EnableDebugLayer();
            }
        }

        // ---- 2. DXGI factory + adapter selection ---------------------------
        UINT factory_flags = 0;
        if (info.enable_validation)
            factory_flags |= DXGI_CREATE_FACTORY_DEBUG;

        HRESULT hr = CreateDXGIFactory2(factory_flags, IID_PPV_ARGS(&factory_));
        if (FAILED(hr))
            return hr;

        // Walk adapters in GPU-preference order; pick the first that
        // satisfies our min_feature_level. The high-performance preference
        // skips integrated when a discrete is present, matching the
        // prefer_discrete_gpu flag's intent.
        const DXGI_GPU_PREFERENCE pref =
            info.prefer_discrete_gpu ? DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE
                                     : DXGI_GPU_PREFERENCE_UNSPECIFIED;
        ComPtr<IDXGIAdapter4> adapter;
        for (UINT i = 0; ; ++i)
        {
            ComPtr<IDXGIAdapter1> next;
            hr = factory_->EnumAdapterByGpuPreference(i, pref, IID_PPV_ARGS(&next));
            if (hr == DXGI_ERROR_NOT_FOUND)
                break;
            if (FAILED(hr))
                return hr;

            DXGI_ADAPTER_DESC1 desc {};
            next->GetDesc1(&desc);
            if ((desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0)
                continue;  // skip WARP unless explicitly requested

            // Probe whether this adapter can create a D3D12 device at the
            // requested feature level. We pass nullptr to skip actual
            // device creation; we'll create the real one below once chosen.
            const auto fl =
                static_cast<D3D_FEATURE_LEVEL>(info.min_feature_level);
            hr = D3D12CreateDevice(next.Get(), fl, _uuidof(ID3D12Device), nullptr);
            if (SUCCEEDED(hr))
            {
                if (FAILED(next.As(&adapter)))
                    continue;
                adapter_desc_ = desc;
                break;
            }
        }
        if (!adapter)
            return DXGI_ERROR_NOT_FOUND;

        // ---- 3. The actual device ------------------------------------------
        hr = D3D12CreateDevice(
            adapter.Get(),
            static_cast<D3D_FEATURE_LEVEL>(info.min_feature_level),
            IID_PPV_ARGS(&device_)
        );
        if (FAILED(hr))
            return hr;

        // ---- 4. Direct (graphics) queue ------------------------------------
        D3D12_COMMAND_QUEUE_DESC qd {};
        qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        qd.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
        qd.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
        qd.NodeMask = 0;
        hr = device_->CreateCommandQueue(&qd, IID_PPV_ARGS(&graphics_queue_));
        if (FAILED(hr))
            return hr;

        // ---- 5. Fence for wait_idle() --------------------------------------
        hr = device_->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&idle_fence_));
        if (FAILED(hr))
            return hr;
        idle_event_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (idle_event_ == nullptr)
            return HRESULT_FROM_WIN32(GetLastError());

        // Cache the WCHAR adapter name as UTF-8 for adapter_name().
        char utf8[256] {};
        const int n = WideCharToMultiByte(
            CP_UTF8, 0,
            adapter_desc_.Description, -1,
            utf8, static_cast<int>(sizeof(utf8) - 1),
            nullptr, nullptr
        );
        if (n > 0)
            adapter_name_.assign(utf8, static_cast<std::size_t>(n - 1));
        else
            adapter_name_ = "D3D12 adapter (unknown)";

        // ---- 6. DXR feature query (Phase 142 — v0.99.84) -------------------
        // Tier 1.1 is the DXR generation that includes inline ray queries
        // (the equivalent of Vulkan VK_KHR_ray_query). Tier 1.0 covers the
        // pipeline + traceRayEXT-equivalent. Both are reported via
        // D3D12_FEATURE_DATA_D3D12_OPTIONS5::RaytracingTier.
        D3D12_FEATURE_DATA_D3D12_OPTIONS5 opts5 {};
        if (SUCCEEDED(device_->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5,
                                                   &opts5, sizeof(opts5))))
        {
            features_.ray_tracing = (opts5.RaytracingTier >= D3D12_RAYTRACING_TIER_1_0);
            // Inline ray queries (the equivalent of VK_KHR_ray_query) are a
            // Tier 1.1 capability. Mirrors VulkanDevice.cpp:4623 setting
            // features_.ray_query from the VK_KHR_ray_query extension.
            features_.ray_query =
                (opts5.RaytracingTier >= D3D12_RAYTRACING_TIER_1_1);
            // Query ID3D12Device5 for the AS-related entry points; if it
            // succeeds we can call BuildRaytracingAccelerationStructure.
            (void)device_.As(&device5_);
        }

        // ---- 6b. D3 — remaining DeviceFeatures parity (mirrors Vulkan) -----
        // VulkanDevice.cpp:4593-4597 reads these from VkPhysicalDeviceFeatures.
        // On D3D12 Feature Level 11_0+ all five are mandatory parts of the
        // spec (no per-device toggle), so the honest value is an unconditional
        // true — the engine never instantiates a sub-FL11_0 device. There is
        // no CheckFeatureSupport query that would say otherwise; querying one
        // and ignoring its (always-true) result would be dishonest noise.
        features_.geometry_shader     = true;  // FL9_1+ guaranteed.
        features_.tessellation_shader = true;  // FL11_0+ mandatory (HS/DS).
        features_.sampler_anisotropy  = true;  // MaxAnisotropy 16 mandated.
        features_.depth_clamp         = true;  // RasterizerState.DepthClipEnable.
        features_.dual_source_blend   = true;  // SRC1_COLOR/SRC1_ALPHA blends.

        // ---- 6c. D3 — timestamp + pipeline-statistics queries --------------
        // Mirrors the Vulkan side advertising these caps. D3D12 timestamp
        // queries (D3D12_QUERY_HEAP_TYPE_TIMESTAMP) are available on any
        // FL11_0+ direct/compute queue; pipeline-statistics queries
        // (D3D12_QUERY_HEAP_TYPE_PIPELINE_STATISTICS) are likewise mandated.
        // The honest source is whether a query heap of each type can be
        // created on this device, so probe that rather than hardcode.
        {
            D3D12_QUERY_HEAP_DESC ts_qd {};
            ts_qd.Type  = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
            ts_qd.Count = 1;
            ComPtr<ID3D12QueryHeap> ts_heap;
            features_.timestamp_queries = SUCCEEDED(device_->CreateQueryHeap(
                &ts_qd, IID_PPV_ARGS(&ts_heap)));

            D3D12_QUERY_HEAP_DESC ps_qd {};
            ps_qd.Type  = D3D12_QUERY_HEAP_TYPE_PIPELINE_STATISTICS;
            ps_qd.Count = 1;
            ComPtr<ID3D12QueryHeap> ps_heap;
            features_.pipeline_statistics_queries = SUCCEEDED(
                device_->CreateQueryHeap(&ps_qd, IID_PPV_ARGS(&ps_heap)));
        }

        // ---- 7. Mesh-shader feature query (phase766) -----------------------
        // D3D12_FEATURE_DATA_D3D12_OPTIONS7::MeshShaderTier reports
        // MESH_SHADER_TIER_1 on Shader Model 6.5+ adapters with
        // ID3D12Device2 / ID3D12GraphicsCommandList6 support. We also
        // probe ID3D12Device2 for CreatePipelineState(stream) — required
        // by the D3D12_PIPELINE_STATE_STREAM_DESC + MS/AS subobjects path.
        D3D12_FEATURE_DATA_D3D12_OPTIONS7 opts7 {};
        if (SUCCEEDED(device_->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS7,
                                                   &opts7, sizeof(opts7))))
        {
            const bool has_tier_1 = (opts7.MeshShaderTier >= D3D12_MESH_SHADER_TIER_1);
            (void)device_.As(&device2_);
            features_.mesh_shader = has_tier_1 && device2_;
        }

        // ---- 8. Bindless feature query (D10 — parity with Vulkan) ----------
        // D3D12_FEATURE_DATA_D3D12_OPTIONS::ResourceBindingTier governs whether
        // the device supports the large/unbounded shader-visible descriptor
        // tables the engine's dedicated bindless set (`register(t0, space1)[i]`,
        // MEMORY rule 9) dynamic-indexes. Tier 2 lifts the SRV-per-table cap to
        // ~1M and allows DESCRIPTORS_VOLATILE unbounded SRV arrays; Tier 3 makes
        // the whole heap addressable. We advertise `bindless_resources` on Tier 2+
        // (the Vulkan analog is VK_EXT_descriptor_indexing being present). WARP
        // reports Tier 3, so the autonomous smokes can exercise the real path.
        D3D12_FEATURE_DATA_D3D12_OPTIONS opts0 {};
        if (SUCCEEDED(device_->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS,
                                                   &opts0, sizeof(opts0))))
        {
            features_.bindless_resources =
                (opts0.ResourceBindingTier >= D3D12_RESOURCE_BINDING_TIER_2);
        }

        // ---- 9. DeviceLimits fill (D13 — parity with Vulkan) ---------------
        fill_device_limits();

        return S_OK;
    }

    // ---- D13: populate DeviceLimits from real D3D12 caps -------------------
    //
    // The RHI `DeviceLimits` struct (see Descriptors.hpp) exposes a fixed
    // set of fields; this fills each from the documented D3D12 hard limits
    // and the queried resource-binding tier, mirroring the Vulkan backend's
    // `limits_.* = p.limits.*` block. D3D12 has no per-device limit struct
    // for most of these (they are spec-mandated constants for FL 11_0+),
    // so the values come from the D3D12 spec / d3d12.h `#define`s, with the
    // binding-tier-dependent descriptor caps taken from CheckFeatureSupport.
    void fill_device_limits()
    {
        // Spec-mandated texture dimensions (Feature Level 11_0+).
        limits_.max_texture_dimension_1d  = D3D12_REQ_TEXTURE1D_U_DIMENSION;          // 16384
        limits_.max_texture_dimension_2d  = D3D12_REQ_TEXTURE2D_U_OR_V_DIMENSION;     // 16384
        limits_.max_texture_dimension_3d  = D3D12_REQ_TEXTURE3D_U_V_OR_W_DIMENSION;   // 2048
        limits_.max_texture_array_layers  = D3D12_REQ_TEXTURE2D_ARRAY_AXIS_DIMENSION; // 2048

        // Constant buffer max size: 4096 float4 registers * 16 bytes = 65536.
        limits_.max_uniform_buffer_range  =
            D3D12_REQ_CONSTANT_BUFFER_ELEMENT_COUNT * 16u;                            // 65536
        // Structured/raw UAV/SRV addressable range: 2^27 elements (typed) is the
        // documented cap; report the 128 MiB max resource span as the storage
        // range (matches what callers branch on for large SSBO support).
        limits_.max_storage_buffer_range  = 1u << 27;                                 // 134217728

        // Root signature is 64 DWORDs; root constants are a subset. We expose
        // the full root-signature budget in bytes as the push-constant ceiling.
        limits_.max_push_constants_size   = D3D12_MAX_ROOT_COST * 4u;                 // 256

        // Resource-binding tier governs the simultaneously-bound descriptor
        // counts. Tier 2/3 are effectively unbounded ("full heap"); Tier 1 has
        // the classic 14 CBV / 128 SRV / 64 UAV caps. We surface the number of
        // root-table descriptor sets the engine can bind (matches Vulkan's
        // maxBoundDescriptorSets semantics) — root signature can reference many
        // tables, capped by the 64-DWORD budget; 8 is a safe portable floor and
        // what the engine's pipeline-layout path assumes.
        limits_.max_bound_descriptor_sets = 8u;

        // Input-assembler limits (spec constants).
        limits_.max_vertex_input_attributes = D3D12_IA_VERTEX_INPUT_STRUCTURE_ELEMENT_COUNT; // 32
        limits_.max_vertex_input_bindings   = D3D12_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT;      // 32

        // Simultaneous render targets.
        limits_.max_color_attachments = D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT;       // 8

        // Max anisotropy (sampler).
        limits_.max_anisotropy = static_cast<float>(D3D12_REQ_MAXANISOTROPY);         // 16

        // Buffer offset alignments. CBVs must be 256-byte aligned; raw/structured
        // buffer SRV/UAV offsets follow the 16-byte structured-buffer rule but we
        // report the stricter 256-byte CBV alignment to stay safe for both.
        limits_.min_uniform_buffer_offset_alignment =
            D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT;                           // 256
        limits_.min_storage_buffer_offset_alignment =
            D3D12_RAW_UAV_SRV_BYTE_ALIGNMENT;                                         // 16
    }

    // ---- D1: validated MSAA sample-count mapping (parity with Vulkan) ------
    //
    // Mirrors VulkanDevice's `map_samples` (VulkanDevice.cpp:196), which turns
    // the engine `SampleCount` enum into the backend's native MSAA count. Where
    // Vulkan trusts the enum directly, D3D12 must additionally validate that
    // the (format, count) pair is supported on this adapter via
    // CheckFeatureSupport(D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS); an
    // unsupported count makes CreateCommittedResource / CreatePipelineState
    // fail with E_INVALIDARG. We clamp DOWN to the highest supported power-of-
    // two count (>= 1) and warn, so a 16x request on a 8x-max adapter renders
    // at 8x rather than failing the whole texture/PSO creation. The companion
    // `quality_levels` is reported through `out_quality` for the resource's
    // SampleDesc.Quality (0 selects the standard MSAA pattern).
    [[nodiscard]] UINT map_samples(cd::rhi::SampleCount s,
                                   DXGI_FORMAT          fmt,
                                   UINT*                out_quality = nullptr) const
    {
        const auto supported = [this, fmt](UINT count) -> bool {
            if (count <= 1u)
                return true;
            D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS q {};
            q.Format           = fmt;
            q.SampleCount      = count;
            q.Flags            = D3D12_MULTISAMPLE_QUALITY_LEVELS_FLAG_NONE;
            q.NumQualityLevels = 0;
            if (FAILED(device_->CheckFeatureSupport(
                    D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS, &q, sizeof(q))))
                return false;
            return q.NumQualityLevels > 0;
        };

        UINT requested = std::max<UINT>(static_cast<UINT>(s), 1u);

        // Clamp down to the highest supported power-of-two <= requested.
        UINT count = requested;
        while (count > 1u && !supported(count))
            count >>= 1u;

        if (count != requested)
        {
            std::fprintf(stderr,
                "[d3d12] map_samples: %ux MSAA unsupported for format %d on this "
                "adapter; clamped to %ux\n",
                static_cast<unsigned>(requested),
                static_cast<int>(fmt),
                static_cast<unsigned>(count));
        }

        if (out_quality != nullptr)
            *out_quality = 0;  // standard MSAA pattern.
        return count;
    }

    ~D3D12Device() override
    {
        // wait_idle so the queue drains before we tear down the queue.
        if (device_ && graphics_queue_ && idle_fence_)
            (void)wait_idle_internal();
        if (idle_event_ != nullptr)
            CloseHandle(idle_event_);
    }

    // ---- Introspection (REAL) ---------------------------------------------

    [[nodiscard]] cd::rhi::Backend backend() const noexcept override
    {
        return cd::rhi::Backend::kD3D12;
    }

    [[nodiscard]] std::string_view adapter_name() const noexcept override
    {
        return adapter_name_;
    }

    [[nodiscard]] const cd::rhi::DeviceLimits& limits() const noexcept override
    {
        return limits_;
    }

    [[nodiscard]] const cd::rhi::DeviceFeatures& features() const noexcept override
    {
        return features_;
    }

    // ---- wait_idle (REAL) -------------------------------------------------

    void wait_idle() override
    {
        (void)wait_idle_internal();
    }

    // ---- Buffer (REAL — Phase 13.C v0.32.0) -------------------------------

    [[nodiscard]] cd::core::Result<cd::rhi::BufferHandle>
    create_buffer(const cd::rhi::BufferDesc& desc) override
    {
        if (desc.size == 0)
        {
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kInvalidArgument, "buffer size == 0"));
        }
        D3D12_HEAP_PROPERTIES hp {};
        hp.Type = heap_type_for(desc.memory);
        hp.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
        hp.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
        hp.CreationNodeMask = 1;
        hp.VisibleNodeMask = 1;

        D3D12_RESOURCE_DESC rd {};
        rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        rd.Alignment = 0;
        rd.Width = desc.size;
        rd.Height = 1;
        rd.DepthOrArraySize = 1;
        rd.MipLevels = 1;
        rd.Format = DXGI_FORMAT_UNKNOWN;
        rd.SampleDesc.Count = 1;
        rd.SampleDesc.Quality = 0;
        rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        rd.Flags = D3D12_RESOURCE_FLAG_NONE;
        if ((static_cast<std::uint32_t>(desc.usage) &
             static_cast<std::uint32_t>(cd::rhi::BufferUsage::kStorage)) != 0)
            rd.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

        // UPLOAD/READBACK heaps require GENERIC_READ / COPY_DEST initial
        // state respectively per the D3D12 spec; DEFAULT heap allows any
        // initial state but COMMON is the safe portable choice.
        D3D12_RESOURCE_STATES initial_state = D3D12_RESOURCE_STATE_COMMON;
        if (hp.Type == D3D12_HEAP_TYPE_UPLOAD)
            initial_state = D3D12_RESOURCE_STATE_GENERIC_READ;
        else if (hp.Type == D3D12_HEAP_TYPE_READBACK)
            initial_state = D3D12_RESOURCE_STATE_COPY_DEST;

        ComPtr<ID3D12Resource> res;
        HRESULT hr = device_->CreateCommittedResource(
            &hp, D3D12_HEAP_FLAG_NONE, &rd, initial_state, nullptr,
            IID_PPV_ARGS(&res));
        if (FAILED(hr))
        {
            char msg[160] {};
            std::snprintf(msg, sizeof(msg),
                          "CreateCommittedResource(buffer) failed: HRESULT 0x%08lx",
                          static_cast<unsigned long>(hr));
            return std::unexpected(cd::rhi::rhi_errors::make_owning(
                cd::rhi::rhi_errors::Code::kResourceCreationFailed,
                std::string { msg }));
        }
        const auto id = next_id_++;
        BufferRecord rec;
        rec.resource = res;
        rec.size = desc.size;
        rec.heap_type = hp.Type;
        buffers_.emplace(id, std::move(rec));
        return cd::rhi::BufferHandle { id, 1u };
    }

    void destroy_buffer(cd::rhi::BufferHandle h) override
    {
        buffers_.erase(h.index());
    }

    // ---- Texture (REAL — Phase 13.C v0.32.0) ------------------------------

    [[nodiscard]] cd::core::Result<cd::rhi::TextureHandle>
    create_texture(const cd::rhi::TextureDesc& desc) override
    {
        if (desc.extent.width == 0)
        {
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kInvalidArgument, "texture extent has zero width"));
        }
        const DXGI_FORMAT fmt = to_dxgi_format(desc.format);
        if (fmt == DXGI_FORMAT_UNKNOWN)
        {
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kInvalidArgument,
                "unsupported texture format for D3D12 backend"));
        }

        // Validate type-specific dimension requirements.
        if (desc.type == cd::rhi::TextureType::k2D ||
            desc.type == cd::rhi::TextureType::kCube)
        {
            if (desc.extent.height == 0)
            {
                return std::unexpected(cd::rhi::rhi_errors::make(
                    cd::rhi::rhi_errors::Code::kInvalidArgument,
                    "k2D/kCube texture: height must be > 0"));
            }
        }
        else if (desc.type == cd::rhi::TextureType::k3D)
        {
            if (desc.extent.height == 0 || desc.extent.depth == 0)
            {
                return std::unexpected(cd::rhi::rhi_errors::make(
                    cd::rhi::rhi_errors::Code::kInvalidArgument,
                    "k3D texture: height and depth must be > 0"));
            }
        }

        D3D12_HEAP_PROPERTIES hp {};
        hp.Type = D3D12_HEAP_TYPE_DEFAULT;
        hp.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
        hp.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
        hp.CreationNodeMask = 1;
        hp.VisibleNodeMask = 1;

        D3D12_RESOURCE_DESC rd {};
        // Phase 393/394 — k1D + k3D dimension support. The DepthOrArraySize
        // field carries different meaning per resource type:
        //   Texture1D: array size (>=1)
        //   Texture2D: array size (>=1)
        //   Texture3D: depth (actual depth of the 3D volume)
        switch (desc.type)
        {
            case cd::rhi::TextureType::k1D:
                rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE1D;
                rd.Width     = desc.extent.width;
                rd.Height    = 1;
                rd.DepthOrArraySize = static_cast<UINT16>(
                    std::max(1u, desc.array_layers));
                break;
            case cd::rhi::TextureType::k3D:
                rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE3D;
                rd.Width     = desc.extent.width;
                rd.Height    = desc.extent.height;
                // DepthOrArraySize == depth slices for Texture3D.
                rd.DepthOrArraySize = static_cast<UINT16>(
                    std::max(1u, desc.extent.depth));
                break;
            case cd::rhi::TextureType::k2D:
            case cd::rhi::TextureType::kCube:
            default:
                rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
                rd.Width     = desc.extent.width;
                rd.Height    = desc.extent.height;
                // Cube maps require 6 array slices (or multiples of 6 for
                // cube arrays).
                rd.DepthOrArraySize = (desc.type == cd::rhi::TextureType::kCube)
                    ? static_cast<UINT16>(std::max(6u, desc.array_layers))
                    : static_cast<UINT16>(std::max(1u, desc.array_layers));
                break;
        }
        rd.Alignment  = 0;
        rd.MipLevels  = static_cast<UINT16>(desc.mip_levels);
        rd.Format     = fmt;
        // D1 (parity with Vulkan VkImageCreateInfo.samples = map_samples(...)):
        // honor TextureDesc.samples, validated + clamped against the adapter's
        // supported MSAA quality levels for this format.
        UINT tex_quality = 0;
        rd.SampleDesc.Count   = map_samples(desc.samples, fmt, &tex_quality);
        rd.SampleDesc.Quality = tex_quality;
        rd.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        rd.Flags  = D3D12_RESOURCE_FLAG_NONE;
        const auto u = static_cast<std::uint32_t>(desc.usage);
        if ((u & static_cast<std::uint32_t>(cd::rhi::TextureUsage::kColorAttachment)) != 0)
            rd.Flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        if ((u & static_cast<std::uint32_t>(cd::rhi::TextureUsage::kDepthStencilAttachment)) != 0)
            rd.Flags |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
        if ((u & static_cast<std::uint32_t>(cd::rhi::TextureUsage::kStorage)) != 0)
            rd.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

        ComPtr<ID3D12Resource> res;
        HRESULT hr = device_->CreateCommittedResource(
            &hp, D3D12_HEAP_FLAG_NONE, &rd,
            D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&res));
        if (FAILED(hr))
        {
            char msg[160] {};
            std::snprintf(msg, sizeof(msg),
                          "CreateCommittedResource(texture) failed: HRESULT 0x%08lx",
                          static_cast<unsigned long>(hr));
            return std::unexpected(cd::rhi::rhi_errors::make_owning(
                cd::rhi::rhi_errors::Code::kResourceCreationFailed,
                std::string { msg }));
        }
        const auto id = next_id_++;
        TextureRecord rec;
        rec.resource = res;
        rec.format = fmt;
        rec.extent = desc.extent;
        rec.usage = desc.usage;
        rec.type  = desc.type;
        rec.sample_count = rd.SampleDesc.Count;  // D1: remember the MSAA count.
        // D-MIPSTATE: remember the subresource grid so a per-mip barrier can
        // compute the D3D12 subresource index (mip + layer * mip_levels). A 3D
        // texture has 1 array layer (depth slices are NOT subresources).
        rec.mip_levels = std::max<UINT>(1u, static_cast<UINT>(rd.MipLevels));
        rec.array_layers = (desc.type == cd::rhi::TextureType::k3D)
            ? 1u
            : std::max<UINT>(1u, static_cast<UINT>(rd.DepthOrArraySize));
        textures_.emplace(id, std::move(rec));
        return cd::rhi::TextureHandle { id, 1u };
    }

    void destroy_texture(cd::rhi::TextureHandle h) override
    {
        textures_.erase(h.index());
    }

    // phase883-d3d12-dead-scaffold-cleanup: the
    // CD_D3D12_NOT_IMPL_RESULT macro and its "Everything else
    // stubbed" comment are historical scaffold from the Phase 13.C
    // parity sprint's early days. Every entry point that lived
    // under that umbrella has since received a real implementation
    // (texture views Phase 124, shader compile Phase 139, descriptor
    // sets Phase 145+, RT path Phases M4A-M4H, etc.). The macro had
    // zero call sites; removed along with the misleading header.
    //
    // The remaining `Code::kNotImplemented` returns in this file are
    // runtime CAPABILITY GATES (mesh shaders + DXR on adapters that
    // lack the feature) or defensive guards on unknown enum values,
    // not "TODO: implement me later" stubs.

    // ---- Texture view (REAL — Phase 124 v0.99.51) -------------------------
    //
    // D3D12 has no standalone "texture view" object — views are
    // descriptor-heap entries. We allocate the appropriate descriptor
    // (RTV / DSV / SRV / UAV) based on the parent texture's usage flags
    // and store the resulting CPU handle in TextureViewRecord. The
    // command buffer's begin_render_pass + descriptor-set bind paths
    // already know how to consume those handles.
    //
    // Bump-allocator pools per descriptor heap type. 256 slots each is
    // sufficient for marathon-scale scenes; switch to a free-list when
    // the cap shows up in profiling.

    [[nodiscard]] cd::core::Result<cd::rhi::TextureViewHandle>
    create_texture_view(const cd::rhi::TextureViewDesc& desc) override
    {
        auto* trec = find_texture(desc.texture);
        if (trec == nullptr)
        {
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kInvalidArgument,
                "create_texture_view: unknown parent texture handle"));
        }
        // Phase 393/395 — k1D, k2D, k3D, kCube all wired.
        const bool is_cube_view = (desc.type == cd::rhi::TextureType::kCube);
        const bool is_1d_view   = (desc.type == cd::rhi::TextureType::k1D);
        const bool is_3d_view   = (desc.type == cd::rhi::TextureType::k3D);
        if (!is_cube_view && !is_1d_view && !is_3d_view &&
            desc.type != cd::rhi::TextureType::k2D)
        {
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kNotImplemented,
                "create_texture_view: unsupported TextureType value"));
        }

        const DXGI_FORMAT view_fmt = (desc.format == cd::rhi::Format::kUndefined)
            ? trec->format
            : to_dxgi_format(desc.format);
        if (view_fmt == DXGI_FORMAT_UNKNOWN)
        {
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kInvalidArgument,
                "create_texture_view: unsupported view format"));
        }

        TextureViewRecord vrec;
        vrec.parent  = desc.texture;
        vrec.format  = view_fmt;
        vrec.is_cube = is_cube_view;
        vrec.is_1d   = is_1d_view;
        vrec.is_3d   = is_3d_view;
        vrec.is_ms   = (trec->sample_count > 1u);  // D-SRV-MS

        const auto u = static_cast<std::uint32_t>(trec->usage);
        const bool is_rt    = (u & static_cast<std::uint32_t>(cd::rhi::TextureUsage::kColorAttachment)) != 0;
        const bool is_depth = (u & static_cast<std::uint32_t>(cd::rhi::TextureUsage::kDepthStencilAttachment)) != 0;
        const bool is_samp  = (u & static_cast<std::uint32_t>(cd::rhi::TextureUsage::kSampled)) != 0;
        const bool is_uav   = (u & static_cast<std::uint32_t>(cd::rhi::TextureUsage::kStorage)) != 0;

        if (is_rt)
        {
            if (auto r = ensure_rtv_pool_(); !r.has_value()) return std::unexpected(r.error());
            if (rtv_cursor_ >= kViewPoolCap)
            {
                return std::unexpected(cd::rhi::rhi_errors::make(
                    cd::rhi::rhi_errors::Code::kResourceCreationFailed,
                    "create_texture_view: RTV pool exhausted"));
            }
            D3D12_CPU_DESCRIPTOR_HANDLE cpu = rtv_pool_->GetCPUDescriptorHandleForHeapStart();
            cpu.ptr += static_cast<SIZE_T>(rtv_cursor_) * rtv_increment_;
            ++rtv_cursor_;

            D3D12_RENDER_TARGET_VIEW_DESC rd {};
            rd.Format = view_fmt;
            // X4-E1 — RTV dimension follows the view's texture type, mirroring
            // VulkanDevice's VK_IMAGE_VIEW_TYPE_1D / _3D for attachments.
            // (Cube RTs are not a separate RTV dimension in D3D12 — they bind
            // as a TEXTURE2DARRAY slice; cube colour attachments are not part
            // of this RHI's render-target surface, so the cube-flag falls
            // through to the TEXTURE2D path here.)
            if (is_1d_view)
            {
                rd.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE1D;
                rd.Texture1D.MipSlice = desc.base_mip;
            }
            else if (is_3d_view)
            {
                rd.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE3D;
                rd.Texture3D.MipSlice = desc.base_mip;
                rd.Texture3D.FirstWSlice = desc.base_layer;
                rd.Texture3D.WSize = static_cast<UINT>(-1);  // all depth slices
            }
            else if (trec->sample_count > 1u)
            {
                // D1: an MSAA colour target needs a TEXTURE2DMS RTV — a plain
                // TEXTURE2D RTV on a multisampled resource is an invalid view
                // (device-removal under the debug layer). Mirrors Vulkan, where
                // the image view automatically reflects the multisampled image.
                rd.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DMS;
            }
            else
            {
                rd.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
                rd.Texture2D.MipSlice = desc.base_mip;
            }
            device_->CreateRenderTargetView(trec->resource.Get(), &rd, cpu);
            vrec.rtv_cpu = cpu;
        }
        else if (is_depth)
        {
            if (auto r = ensure_dsv_pool_(); !r.has_value()) return std::unexpected(r.error());
            if (dsv_cursor_ >= kViewPoolCap)
            {
                return std::unexpected(cd::rhi::rhi_errors::make(
                    cd::rhi::rhi_errors::Code::kResourceCreationFailed,
                    "create_texture_view: DSV pool exhausted"));
            }
            D3D12_CPU_DESCRIPTOR_HANDLE cpu = dsv_pool_->GetCPUDescriptorHandleForHeapStart();
            cpu.ptr += static_cast<SIZE_T>(dsv_cursor_) * dsv_increment_;
            ++dsv_cursor_;

            // X4-E1 — DSV dimension follows the view's texture type. D3D12
            // has no TEXTURE3D DSV dimension (Vulkan likewise has no 3D depth
            // attachment use), so a 3D depth view is rejected rather than
            // silently downgraded to TEXTURE2D.
            if (is_3d_view)
            {
                return std::unexpected(cd::rhi::rhi_errors::make(
                    cd::rhi::rhi_errors::Code::kInvalidArgument,
                    "create_texture_view: 3D textures cannot be a depth-stencil "
                    "attachment (no D3D12_DSV_DIMENSION_TEXTURE3D)"));
            }
            D3D12_DEPTH_STENCIL_VIEW_DESC dd {};
            dd.Format = view_fmt;
            if (is_1d_view)
            {
                dd.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE1D;
                dd.Texture1D.MipSlice = desc.base_mip;
            }
            else if (trec->sample_count > 1u)
            {
                // D1: an MSAA depth target needs a TEXTURE2DMS DSV (companion to
                // the MSAA RTV branch above).
                dd.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DMS;
            }
            else
            {
                dd.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
                dd.Texture2D.MipSlice = desc.base_mip;
            }
            device_->CreateDepthStencilView(trec->resource.Get(), &dd, cpu);
            // The DSV handle lives in a separate pool; we reuse rtv_cpu
            // as the "primary attachment view slot" — begin_render_pass'
            // depth-stencil path reads this slot when the parent texture
            // was created with kDepthStencilAttachment usage.
            vrec.rtv_cpu = cpu;
        }
        else if (is_samp || is_uav)
        {
            if (auto r = ensure_cpu_heap_(); !r.has_value()) return std::unexpected(r.error());
            if (cpu_heap_cursor_ >= 4096)
            {
                return std::unexpected(cd::rhi::rhi_errors::make(
                    cd::rhi::rhi_errors::Code::kResourceCreationFailed,
                    "create_texture_view: SRV/UAV pool exhausted (4096 cap shared with descriptor sets)"));
            }
            D3D12_CPU_DESCRIPTOR_HANDLE cpu = cpu_heap_->GetCPUDescriptorHandleForHeapStart();
            cpu.ptr += static_cast<SIZE_T>(cpu_heap_cursor_) * cpu_heap_increment_;
            ++cpu_heap_cursor_;

            if (is_samp)
            {
                D3D12_SHADER_RESOURCE_VIEW_DESC sd {};
                sd.Format = view_fmt;
                sd.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                const UINT mip_count = (desc.mip_count == 0u) ? 1u : desc.mip_count;
                if (is_cube_view)
                {
                    sd.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
                    sd.TextureCube.MostDetailedMip = desc.base_mip;
                    sd.TextureCube.MipLevels = mip_count;
                    sd.TextureCube.ResourceMinLODClamp = 0.0F;
                }
                else if (is_1d_view)
                {
                    // Phase 393 — Texture1D SRV.
                    sd.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE1D;
                    sd.Texture1D.MostDetailedMip = desc.base_mip;
                    sd.Texture1D.MipLevels = mip_count;
                    sd.Texture1D.ResourceMinLODClamp = 0.0F;
                }
                else if (is_3d_view)
                {
                    // Phase 395 — Texture3D SRV.
                    sd.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D;
                    sd.Texture3D.MostDetailedMip = desc.base_mip;
                    sd.Texture3D.MipLevels = mip_count;
                    sd.Texture3D.ResourceMinLODClamp = 0.0F;
                }
                else if (trec->sample_count > 1u)
                {
                    // D-SRV-MS (Backend-to-100 Wave 1): a multisampled texture
                    // can be SAMPLED (Texture2DMS in HLSL) only through a
                    // TEXTURE2DMS SRV — a plain TEXTURE2D SRV over an MSAA
                    // resource is an invalid view (device-removal under the
                    // debug layer). The RTV/DSV paths already branch to *2DMS;
                    // this is the named A1 follow-up that completes the SRV
                    // side. TEXTURE2DMS carries no mip fields (an MSAA target
                    // has a single mip).
                    sd.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DMS;
                }
                else
                {
                    sd.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
                    sd.Texture2D.MostDetailedMip = desc.base_mip;
                    sd.Texture2D.MipLevels = mip_count;
                    sd.Texture2D.PlaneSlice = 0;
                    sd.Texture2D.ResourceMinLODClamp = 0.0F;
                }
                device_->CreateShaderResourceView(trec->resource.Get(), &sd, cpu);
            }
            else  // is_uav
            {
                D3D12_UNORDERED_ACCESS_VIEW_DESC ud {};
                ud.Format = view_fmt;
                if (is_cube_view)
                {
                    // Cube UAV maps to a TEXTURE2DARRAY UAV view in DX12
                    // (no dedicated cube UAV). 6 array slices, all mips.
                    ud.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2DARRAY;
                    ud.Texture2DArray.MipSlice = desc.base_mip;
                    ud.Texture2DArray.FirstArraySlice = 0;
                    ud.Texture2DArray.ArraySize = 6;
                    ud.Texture2DArray.PlaneSlice = 0;
                }
                else if (is_1d_view)
                {
                    // Phase 393 — Texture1D UAV.
                    ud.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE1D;
                    ud.Texture1D.MipSlice = desc.base_mip;
                }
                else if (is_3d_view)
                {
                    // Phase 395 — Texture3D UAV.
                    ud.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE3D;
                    ud.Texture3D.MipSlice = desc.base_mip;
                    ud.Texture3D.FirstWSlice = 0;
                    ud.Texture3D.WSize = static_cast<UINT>(-1);  // all depth slices
                }
                else
                {
                    ud.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
                    ud.Texture2D.MipSlice = desc.base_mip;
                    ud.Texture2D.PlaneSlice = 0;
                }
                device_->CreateUnorderedAccessView(trec->resource.Get(), nullptr, &ud, cpu);
            }
            vrec.rtv_cpu = cpu;
        }
        else
        {
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kInvalidArgument,
                "create_texture_view: parent texture has no view-creatable usage flag set"));
        }

        const auto id = next_id_++;
        texture_views_.emplace(id, vrec);
        return cd::rhi::TextureViewHandle { id, 1u };
    }
    void destroy_texture_view(cd::rhi::TextureViewHandle h) override
    {
        // Bump-allocator pools — handle entry is dropped, slot reuse
        // requires a free-list pass (Phase 125 candidate).
        texture_views_.erase(h.index());
    }

    // ---- Sampler (REAL — phase466 v0.99.93 M4-parity-closeout) -----------
    //
    // D3D12 samplers live in a dedicated descriptor heap (TYPE_SAMPLER).
    // The cd::rhi surface returns a SamplerHandle that is later consumed
    // by update_descriptor_set (for combined-image-sampler) or by the
    // pipeline static-sampler path. We allocate from a lazily-created
    // CPU-visible sampler heap (256 slots; bumps per create).
    [[nodiscard]] cd::core::Result<cd::rhi::SamplerHandle>
    create_sampler(const cd::rhi::SamplerDesc& desc) override
    {
        // Lazily allocate sampler heap (single 256-slot bump allocator).
        if (sampler_heap_ == nullptr)
        {
            D3D12_DESCRIPTOR_HEAP_DESC hd {};
            hd.NumDescriptors = kSamplerHeapCap;
            hd.Type  = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
            hd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
            if (FAILED(device_->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&sampler_heap_))))
            {
                return std::unexpected(cd::rhi::rhi_errors::make(
                    cd::rhi::rhi_errors::Code::kResourceCreationFailed,
                    "create_sampler: sampler descriptor heap creation failed"));
            }
            sampler_heap_increment_ = device_->GetDescriptorHandleIncrementSize(
                D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
        }
        if (sampler_cursor_ >= kSamplerHeapCap)
        {
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kResourceCreationFailed,
                "create_sampler: sampler heap exhausted (256-slot cap)"));
        }

        // Map cd::rhi::Sampler* fields onto D3D12 enums.
        D3D12_SAMPLER_DESC sd {};
        const bool aniso     = desc.anisotropy_enable && desc.max_anisotropy > 1.0F;
        const bool compare   = desc.compare_enable;
        const bool min_lin   = desc.min_filter   == cd::rhi::SamplerFilter::kLinear;
        const bool mag_lin   = desc.mag_filter   == cd::rhi::SamplerFilter::kLinear;
        const bool mip_lin   = desc.mipmap_mode  == cd::rhi::SamplerMipmapMode::kLinear;
        if (aniso)
        {
            sd.Filter = compare ? D3D12_FILTER_COMPARISON_ANISOTROPIC
                                : D3D12_FILTER_ANISOTROPIC;
        }
        else
        {
            // 8 combinations across (min, mag, mip) all map to a unique
            // D3D12_FILTER constant. We compose via the standard formula.
            UINT bits = 0;
            if (min_lin) bits |= 0x10u;  // min linear
            if (mag_lin) bits |= 0x04u;  // mag linear
            if (mip_lin) bits |= 0x01u;  // mip linear
            sd.Filter = static_cast<D3D12_FILTER>(
                bits | (compare ? 0x80u : 0x00u));
        }
        auto map_addr = [](cd::rhi::SamplerAddressMode m) noexcept {
            switch (m)
            {
                case cd::rhi::SamplerAddressMode::kRepeat:         return D3D12_TEXTURE_ADDRESS_MODE_WRAP;
                case cd::rhi::SamplerAddressMode::kMirroredRepeat: return D3D12_TEXTURE_ADDRESS_MODE_MIRROR;
                case cd::rhi::SamplerAddressMode::kClampToEdge:    return D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
                case cd::rhi::SamplerAddressMode::kClampToBorder:  return D3D12_TEXTURE_ADDRESS_MODE_BORDER;
                case cd::rhi::SamplerAddressMode::kMirrorClampToEdge:
                                                                   return D3D12_TEXTURE_ADDRESS_MODE_MIRROR_ONCE;
            }
            return D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        };
        sd.AddressU = map_addr(desc.address_u);
        sd.AddressV = map_addr(desc.address_v);
        sd.AddressW = map_addr(desc.address_w);
        sd.MipLODBias = desc.mip_lod_bias;
        sd.MaxAnisotropy = static_cast<UINT>(desc.max_anisotropy);
        switch (desc.compare_op)
        {
            case cd::rhi::CompareOp::kNever:        sd.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;         break;
            case cd::rhi::CompareOp::kLess:         sd.ComparisonFunc = D3D12_COMPARISON_FUNC_LESS;          break;
            case cd::rhi::CompareOp::kEqual:        sd.ComparisonFunc = D3D12_COMPARISON_FUNC_EQUAL;         break;
            case cd::rhi::CompareOp::kLessEqual:    sd.ComparisonFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;    break;
            case cd::rhi::CompareOp::kGreater:      sd.ComparisonFunc = D3D12_COMPARISON_FUNC_GREATER;       break;
            case cd::rhi::CompareOp::kNotEqual:     sd.ComparisonFunc = D3D12_COMPARISON_FUNC_NOT_EQUAL;     break;
            case cd::rhi::CompareOp::kGreaterEqual: sd.ComparisonFunc = D3D12_COMPARISON_FUNC_GREATER_EQUAL; break;
            case cd::rhi::CompareOp::kAlways:       sd.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;        break;
        }
        switch (desc.border_color)
        {
            case cd::rhi::BorderColor::kFloatOpaqueWhite:
                sd.BorderColor[0] = sd.BorderColor[1] = sd.BorderColor[2] = sd.BorderColor[3] = 1.0F;
                break;
            case cd::rhi::BorderColor::kFloatOpaqueBlack:
            case cd::rhi::BorderColor::kIntOpaqueBlack:
                sd.BorderColor[0] = sd.BorderColor[1] = sd.BorderColor[2] = 0.0F;
                sd.BorderColor[3] = 1.0F;
                break;
            case cd::rhi::BorderColor::kFloatTransparentBlack:
            case cd::rhi::BorderColor::kIntTransparentBlack:
                sd.BorderColor[0] = sd.BorderColor[1] = sd.BorderColor[2] = sd.BorderColor[3] = 0.0F;
                break;
            case cd::rhi::BorderColor::kIntOpaqueWhite:
                sd.BorderColor[0] = sd.BorderColor[1] = sd.BorderColor[2] = sd.BorderColor[3] = 1.0F;
                break;
        }
        sd.MinLOD = desc.min_lod;
        sd.MaxLOD = desc.max_lod;

        D3D12_CPU_DESCRIPTOR_HANDLE dst = sampler_heap_->GetCPUDescriptorHandleForHeapStart();
        dst.ptr += static_cast<SIZE_T>(sampler_cursor_) * sampler_heap_increment_;
        device_->CreateSampler(&sd, dst);

        SamplerRecord rec;
        rec.heap_slot = sampler_cursor_;
        rec.cpu_handle = dst;
        ++sampler_cursor_;
        const auto id = next_id_++;
        samplers_.emplace(id, rec);
        return cd::rhi::SamplerHandle { id, 1u };
    }
    void destroy_sampler(cd::rhi::SamplerHandle h) override
    {
        samplers_.erase(h.index());
        // Heap slot not reclaimed (bump allocator); acceptable until a
        // slab-allocator rework lands.
    }

    // ---- Shader module (REAL — Phase 14.C v0.36.0) ------------------------

    // D12 (phase1184) — route by source language. `kBytecode` (default)
    // is the legacy pass-through: `code` is already DXIL/DXBC and is
    // stored verbatim (byte-identical to the pre-D12 contract). `kGlsl`,
    // `kSpirv`, `kHlsl` run the cross-compile toolchain
    // (GLSL→SPIR-V→HLSL→DXIL) so the engine shader corpus produces DXIL
    // here instead of relying on inline SM5.1 HLSL islands. All failures
    // are typed errors — no crash, no silent empty module.
    [[nodiscard]] cd::core::Result<cd::rhi::ShaderModuleHandle>
    create_shader_module(const cd::rhi::ShaderModuleDesc& desc) override
    {
        if (desc.code == nullptr || desc.code_size == 0)
        {
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kInvalidArgument,
                "shader module: empty source/bytecode"));
        }

        ShaderModuleRecord rec;
        rec.stage = desc.stage;
        rec.entry_point = std::string { desc.entry_point };

        switch (desc.language)
        {
            case cd::rhi::ShaderSourceLanguage::kBytecode:
            {
                // Native DXIL/DXBC — consume verbatim.
                const auto* src = static_cast<const std::uint8_t*>(desc.code);
                rec.bytecode.assign(src, src + desc.code_size);
                break;
            }
            case cd::rhi::ShaderSourceLanguage::kGlsl:
            {
                auto* compiler = glsl_compiler();
                if (compiler == nullptr)
                {
                    return std::unexpected(cd::rhi::rhi_errors::make(
                        cd::rhi::rhi_errors::Code::kResourceCreationFailed,
                        "shader module: GLSL requested but the glslang "
                        "front-end is unavailable (CD_ENABLE_GLSLANG=OFF)"));
                }
                // ADR-20260614 consumer-resolver pattern: a null
                // include_resolver bridges to the embedded cd::gluon module
                // catalogue via a FUNCTION-LOCAL stateless resolver (not
                // static / member / global — §2.2). When the caller injects
                // a resolver we respect it (§2.3). The resolver must outlive
                // the compile() call inside compile_glsl_to_dxil, so it is
                // declared here and kept in scope for the whole branch.
                cd::gluon::ModuleResolver default_resolver {};
                auto* injected = static_cast<cd::shader::IIncludeResolver*>(
                    desc.include_resolver);
                cd::rhi::d3d12::GlslToDxilDesc gd {};
                gd.glsl_source = std::string_view {
                    static_cast<const char*>(desc.code),
                    static_cast<std::size_t>(desc.code_size) };
                gd.stage = desc.stage;
                gd.entry_point = desc.entry_point;
                gd.source_name = desc.debug_name.empty()
                                     ? std::string_view { "<inline>" }
                                     : desc.debug_name;
                gd.include_resolver =
                    injected != nullptr ? injected : &default_resolver;
                auto dxil = cd::rhi::d3d12::compile_glsl_to_dxil(*compiler, gd);
                if (!dxil.has_value())
                {
                    return std::unexpected(cd::rhi::rhi_errors::make(
                        cd::rhi::rhi_errors::Code::kResourceCreationFailed,
                        std::string { "shader module (GLSL→DXIL): " } +
                            std::string { dxil.error().message }));
                }
                rec.bytecode = std::move(*dxil);
                break;
            }
            case cd::rhi::ShaderSourceLanguage::kSpirv:
            {
                if ((desc.code_size % 4) != 0)
                {
                    return std::unexpected(cd::rhi::rhi_errors::make(
                        cd::rhi::rhi_errors::Code::kInvalidArgument,
                        "shader module: SPIR-V code must be 32-bit aligned"));
                }
                const std::span<const std::uint32_t> words {
                    static_cast<const std::uint32_t*>(desc.code),
                    static_cast<std::size_t>(desc.code_size / 4) };
                cd::rhi::d3d12::GlslToDxilDesc gd {};
                gd.stage = desc.stage;
                gd.entry_point = desc.entry_point;
                gd.source_name = desc.debug_name.empty()
                                     ? std::string_view { "<inline>" }
                                     : desc.debug_name;
                auto dxil = cd::rhi::d3d12::compile_spirv_to_dxil(words, gd);
                if (!dxil.has_value())
                {
                    return std::unexpected(cd::rhi::rhi_errors::make(
                        cd::rhi::rhi_errors::Code::kResourceCreationFailed,
                        std::string { "shader module (SPIR-V→DXIL): " } +
                            std::string { dxil.error().message }));
                }
                rec.bytecode = std::move(*dxil);
                break;
            }
            case cd::rhi::ShaderSourceLanguage::kHlsl:
            {
                cd::rhi::d3d12::CompileOptions co {};
                co.source = std::string_view {
                    static_cast<const char*>(desc.code),
                    static_cast<std::size_t>(desc.code_size) };
                co.entry_point = desc.entry_point;
                co.stage = desc.stage;
                co.source_name = desc.debug_name.empty()
                                     ? std::string_view { "<inline>" }
                                     : desc.debug_name;
                co.optimization_level = 3u;
                co.model = cd::rhi::d3d12::ShaderModel::kSM6_5;
                auto dxil = cd::rhi::d3d12::compile_hlsl(co);
                if (!dxil.has_value())
                {
                    return std::unexpected(cd::rhi::rhi_errors::make(
                        cd::rhi::rhi_errors::Code::kResourceCreationFailed,
                        std::string { "shader module (HLSL→DXIL): " } +
                            std::string { dxil.error().message }));
                }
                rec.bytecode = std::move(*dxil);
                break;
            }
        }

        if (rec.bytecode.empty())
        {
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kResourceCreationFailed,
                "shader module: cross-compile produced empty DXIL"));
        }

        const auto id = next_id_++;
        shader_modules_.emplace(id, std::move(rec));
        return cd::rhi::ShaderModuleHandle { id, 1u };
    }

    void destroy_shader_module(cd::rhi::ShaderModuleHandle h) override
    {
        shader_modules_.erase(h.index());
    }

    // ---- Descriptor set layout (REAL — Phase 15.B v0.43.0) ----------------
    [[nodiscard]] cd::core::Result<cd::rhi::DescriptorSetLayoutHandle>
    create_descriptor_set_layout(const cd::rhi::DescriptorSetLayoutDesc& desc) override
    {
        DescriptorSetLayoutRecord rec;
        rec.bindings.assign(desc.bindings.begin(), desc.bindings.end());
        // Count descriptor-table entries — every binding occupies
        // one slot in the CPU heap regardless of type. Samplers are
        // budgeted separately if/when sampler bindings appear.
        for (const auto& b : rec.bindings)
        {
            if (b.type == cd::rhi::DescriptorType::kSampler)
                rec.sampler_count += b.count;
            else
                rec.view_count += b.count;
        }
        const auto id = next_id_++;
        descriptor_set_layouts_.emplace(id, std::move(rec));
        return cd::rhi::DescriptorSetLayoutHandle { id, 1u };
    }
    void destroy_descriptor_set_layout(cd::rhi::DescriptorSetLayoutHandle h) override
    {
        descriptor_set_layouts_.erase(h.index());
    }

    // ---- Pipeline layout (REAL — Phase 14.C v0.36.0 / Phase 15.B v0.43.0)
    //
    // D3D12 root signature. Builds one DESCRIPTOR_TABLE root parameter
    // per descriptor-set layout in the PipelineLayoutDesc; each table
    // contains separate ranges per descriptor type (CBV / SRV / UAV).
    // Sampler bindings live in their own table when present (D3D12
    // requires sampler heaps to be separate from CBV/SRV/UAV heaps).
    // Empty layout (no set_layouts) still produces a valid signature
    // — the triangle sample uses inline vertex data.
    [[nodiscard]] cd::core::Result<cd::rhi::PipelineLayoutHandle>
    create_pipeline_layout(const cd::rhi::PipelineLayoutDesc& desc) override
    {
        std::vector<D3D12_ROOT_PARAMETER> params;
        std::vector<std::vector<D3D12_DESCRIPTOR_RANGE>> all_ranges;
        all_ranges.reserve(desc.set_layouts.size() * 2);
        std::vector<std::uint32_t> table_params;
        table_params.reserve(desc.set_layouts.size());
        // D2 — one SAMPLER descriptor-table root-param index per set (or
        // UINT32_MAX when the set declares no bare-`kSampler` binding). A
        // SAMPLER range cannot share a table with CBV/SRV/UAV, so these live
        // in their own root parameters. Storage for the per-set ranges must
        // outlive serialization (pointers held by D3D12_ROOT_PARAMETER).
        std::vector<std::uint32_t> sampler_table_params;
        sampler_table_params.reserve(desc.set_layouts.size());
        std::vector<std::vector<D3D12_DESCRIPTOR_RANGE>> all_sampler_ranges;
        all_sampler_ranges.reserve(desc.set_layouts.size());

        // ADR-20260614-d3d12-binding-model: space-per-set. The N-th descriptor
        // set layout maps to HLSL register space N — matching the SPIRV-Cross
        // SM>=51 default (`register(<class>M, spaceN)`, N = Vulkan set index).
        // Previously every range was hardcoded to space0, which silently
        // mis-bound the dedicated bindless set (set 1 -> space1 in the shader).
        std::uint32_t set_space = 0;

        // B1b sampler-half — back the SPIRV-Cross s-registers.
        //
        // SPIRV-Cross splits each combined `sampler2D` (set N, binding M) into a
        // Texture SRV `register(tM, spaceN)` AND a SamplerState `register(sM,
        // spaceN)`. The texture half is served by the SRV descriptor table above;
        // the sampler half was UNBACKED (NumStaticSamplers=0) — so any textured
        // Sample() on D3D12 had no sampler. Two cases:
        //
        //  * CLASSIC combined sampler (kSampledImage / kCombinedImageSampler):
        //    the emission is a SINGLE SamplerState `register(sM, spaceN)`, which a
        //    STATIC sampler satisfies. Static samplers cost ZERO root budget and
        //    are immutable — a perfect fit for the engine's single-sampler model.
        //
        //  * BINDLESS combined sampler array (kBindlessSampledImage): the emission
        //    is a SamplerState ARRAY `register(sM, spaceN)[]` (an unbounded sampler
        //    DESCRIPTOR RANGE the DXIL dynamic-indexes). A static sampler CANNOT
        //    satisfy a shader sampler descriptor range ("not fully bound in root
        //    signature" → E_INVALIDARG at PSO creation), so the bindless sampler
        //    half needs a real SAMPLER DESCRIPTOR TABLE backed by a shader-visible
        //    sampler heap. We declare the unbounded sampler range here and bind the
        //    sampler heap + table in bind_bindless_texture_array.
        //
        // The shadow map (set 0 binding 1) is a plain `sampler2D` in prim.frag.glsl
        // (NOT sampler2DShadow), so a standard filtered sampler covers it; no
        // SamplerComparisonState half is emitted by the engine corpus today.
        std::vector<D3D12_STATIC_SAMPLER_DESC> static_samplers;
        // Bindless sampler ranges (one unbounded SAMPLER range per bindless
        // binding, at its (sM, spaceN)). Collected into a single sampler table.
        std::vector<D3D12_DESCRIPTOR_RANGE> bindless_sampler_ranges;

        for (const auto h : desc.set_layouts)
        {
            auto it = descriptor_set_layouts_.find(h.index());
            if (it == descriptor_set_layouts_.end())
            {
                return std::unexpected(cd::rhi::rhi_errors::make(
                    cd::rhi::rhi_errors::Code::kInvalidArgument,
                    "pipeline_layout: unknown descriptor_set_layout handle"));
            }
            const auto& layout = it->second;

            // Single descriptor table (CBV/SRV/UAV only — samplers are
            // a separate table when present).
            std::vector<D3D12_DESCRIPTOR_RANGE> ranges;
            ranges.reserve(layout.bindings.size());
            // D2 — bare-`kSampler` ranges for THIS set, collected into a
            // dedicated SAMPLER table below (cannot share the CBV/SRV/UAV one).
            std::vector<D3D12_DESCRIPTOR_RANGE> set_sampler_ranges;
            for (const auto& b : layout.bindings)
            {
                if (b.type == cd::rhi::DescriptorType::kSampler)
                {
                    // D2 — a SAMPLER range at (sM, spaceN) backed at bind time
                    // by the shader-visible sampler ring (the descriptor copied
                    // there from the SamplerRecord). Mirrors the Vulkan kSampler
                    // descriptor; replaces the prior silent-default behaviour.
                    D3D12_DESCRIPTOR_RANGE sr {};
                    sr.RangeType          = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
                    sr.NumDescriptors     = b.count == 0u ? 1u : b.count;
                    sr.BaseShaderRegister = b.binding;  // sM
                    sr.RegisterSpace      = set_space;   // spaceN
                    sr.OffsetInDescriptorsFromTableStart =
                        D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
                    set_sampler_ranges.push_back(sr);
                    continue;
                }
                D3D12_DESCRIPTOR_RANGE r {};
                switch (b.type)
                {
                    case cd::rhi::DescriptorType::kUniformBuffer:
                    case cd::rhi::DescriptorType::kUniformBufferDynamic:
                        r.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
                        break;
                    case cd::rhi::DescriptorType::kSampledImage:
                    case cd::rhi::DescriptorType::kCombinedImageSampler:
                    case cd::rhi::DescriptorType::kInputAttachment:
                    case cd::rhi::DescriptorType::kAccelerationStructure:
                    case cd::rhi::DescriptorType::kBindlessSampledImage:
                        // DXR exposes acceleration structures as SRVs of
                        // a special RAYTRACING_ACCELERATION_STRUCTURE
                        // type. The root-signature range slot is plain
                        // SRV; the descriptor-write side fills in the
                        // RAYTRACING_ACCELERATION_STRUCTURE SRV desc.
                        //
                        // ADR-20260614-d3d12-binding-model / B1b: the engine's
                        // dedicated bindless set (MEMORY rule 9 — set 1) declares
                        // a `kBindlessSampledImage` array. SPIRV-Cross emits it as
                        // `Texture2D ... : register(t<binding>, space<set>)`, so
                        // it MUST land on an SRV range or the whole set-1 table is
                        // dropped from the root signature → silent unbound at
                        // space1 (the shader's `register(t0, space1)` has no table).
                        // The unbounded NumDescriptors override below mirrors the
                        // Vulkan VARIABLE_DESCRIPTOR_COUNT / PARTIALLY_BOUND set.
                        r.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
                        break;
                    case cd::rhi::DescriptorType::kStorageImage:
                    case cd::rhi::DescriptorType::kStorageBuffer:
                    case cd::rhi::DescriptorType::kStorageBufferDynamic:
                        r.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
                        break;
                    default: continue;
                }
                // Bindless arrays are declared UNBOUNDED. NumDescriptors ==
                // UINT_MAX is the D3D12 sentinel for an unbounded descriptor
                // range; combined with SM5.1+ dynamic indexing it matches the
                // Vulkan bindless set's runtime sizing. Root-signature v1.0
                // ranges behave as fully DESCRIPTORS_VOLATILE | DATA_VOLATILE,
                // which is exactly the dynamic-indexing semantics SM6.x needs
                // here, so no v1.1 range-flags migration is required for the
                // declaration to be CONSISTENT with the SPIRV-Cross emission.
                // (D10: the runtime heap writes — write_bindless_texture_slot /
                // create_bindless_texture_array — back this range with a
                // persistent shader-visible heap, and features().bindless_resources
                // is advertised on resource-binding Tier 2+. See the bindless
                // methods after update_descriptor_set.)
                r.NumDescriptors = (b.type ==
                    cd::rhi::DescriptorType::kBindlessSampledImage)
                    ? UINT_MAX : b.count;
                r.BaseShaderRegister = b.binding;
                r.RegisterSpace = set_space;  // space N = N-th descriptor set
                r.OffsetInDescriptorsFromTableStart =
                    D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
                ranges.push_back(r);

                // B1b — emit the matching sampler half. SPIRV-Cross emits a
                // SamplerState at (sM, spaceN) for combined samplers; AS / input
                // attachments are SRVs with no sampler half, so they are excluded.
                if (b.type == cd::rhi::DescriptorType::kBindlessSampledImage)
                {
                    // BINDLESS: a SAMPLER range (a static sampler cannot satisfy a
                    // shader sampler descriptor RANGE — DXC reflects the combined
                    // sampler2D[]'s sampler half as a single shared SamplerState in
                    // a table, NumDescriptors=1). Bounded to the binding count so the
                    // matcher binds it without the unbounded-range edge cases.
                    // Collected into a single sampler table below; backed by a
                    // shader-visible sampler heap pre-filled with the default sampler.
                    D3D12_DESCRIPTOR_RANGE sr {};
                    sr.RangeType          = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
                    sr.NumDescriptors     = b.count == 0u ? 1u : b.count;
                    sr.BaseShaderRegister = b.binding; // sM
                    sr.RegisterSpace      = set_space; // spaceN
                    sr.OffsetInDescriptorsFromTableStart = 0;
                    bindless_sampler_ranges.push_back(sr);
                }
                else if (b.type == cd::rhi::DescriptorType::kSampledImage ||
                         b.type == cd::rhi::DescriptorType::kCombinedImageSampler)
                {
                    // CLASSIC: a single SamplerState — a static sampler satisfies it.
                    D3D12_STATIC_SAMPLER_DESC s {};
                    s.Filter           = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
                    s.AddressU         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
                    s.AddressV         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
                    s.AddressW         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
                    s.MipLODBias       = 0.0F;
                    s.MaxAnisotropy    = 1;
                    s.ComparisonFunc   = D3D12_COMPARISON_FUNC_NEVER;
                    s.BorderColor      = D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK;
                    s.MinLOD           = 0.0F;
                    s.MaxLOD           = D3D12_FLOAT32_MAX;
                    s.ShaderRegister   = b.binding;   // sM (== texture tM)
                    s.RegisterSpace    = set_space;   // spaceN (== texture space)
                    s.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
                    static_samplers.push_back(s);
                }
            }
            // Advance the register space for the NEXT descriptor set. The
            // Vulkan side binds set_layouts in order, so the space index must
            // equal the set ordinal even when a (sampler-only) layout produces
            // no CBV/SRV/UAV ranges — hence the increment lives before the
            // early-out below, not at the bottom of the loop.
            ++set_space;

            // D2 — emit the SAMPLER descriptor table for THIS set's bare
            // samplers (a separate root parameter; SAMPLER ranges cannot share
            // a table with CBV/SRV/UAV). bind_descriptor_set points it at the
            // set's region in the shader-visible sampler ring. Recorded per set
            // ordinal so the index lines up with set_index at bind time.
            if (!set_sampler_ranges.empty())
            {
                all_sampler_ranges.push_back(std::move(set_sampler_ranges));
                D3D12_ROOT_PARAMETER sp {};
                sp.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
                sp.DescriptorTable.NumDescriptorRanges =
                    static_cast<UINT>(all_sampler_ranges.back().size());
                sp.DescriptorTable.pDescriptorRanges = all_sampler_ranges.back().data();
                sp.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
                sampler_table_params.push_back(
                    static_cast<std::uint32_t>(params.size()));
                params.push_back(sp);
            }
            else
            {
                sampler_table_params.push_back(~std::uint32_t { 0 });
            }

            if (ranges.empty())
            {
                // Edge case: layout has nothing but samplers. The CBV/SRV/UAV
                // table is skipped (no view ranges), but the SAMPLER table
                // above is still emitted so a sampler-only set binds correctly.
                // Record UINT32_MAX so table_params stays indexed by set ordinal
                // (a sampler table for THIS set may have shifted the next view
                // table's root-param index off the set ordinal — bind_descriptor_set
                // therefore resolves the view table through table_params, never
                // assuming root-param == set_index).
                table_params.push_back(~std::uint32_t { 0 });
                continue;
            }
            all_ranges.push_back(std::move(ranges));

            D3D12_ROOT_PARAMETER p {};
            p.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            p.DescriptorTable.NumDescriptorRanges =
                static_cast<UINT>(all_ranges.back().size());
            p.DescriptorTable.pDescriptorRanges = all_ranges.back().data();
            p.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
            table_params.push_back(static_cast<std::uint32_t>(params.size()));
            params.push_back(p);
        }

        // B1b — one SAMPLER descriptor table for all bindless sampler ranges
        // collected above. Bound by bind_bindless_texture_array (the sampler heap
        // can coexist with the CBV/SRV/UAV bindless heap — D3D12 allows one of
        // each type bound simultaneously). UINT32_MAX = no bindless sampler table.
        std::uint32_t bindless_sampler_param_idx = ~std::uint32_t { 0 };
        if (!bindless_sampler_ranges.empty())
        {
            D3D12_ROOT_PARAMETER sp {};
            sp.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            sp.DescriptorTable.NumDescriptorRanges =
                static_cast<UINT>(bindless_sampler_ranges.size());
            sp.DescriptorTable.pDescriptorRanges = bindless_sampler_ranges.data();
            sp.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
            bindless_sampler_param_idx = static_cast<std::uint32_t>(params.size());
            params.push_back(sp);
        }

        // phase466 — push-constants → D3D12 root 32-bit constants slot.
        //
        // The Vulkan surface allows multiple PushConstantRange entries with
        // distinct stages. D3D12 root signatures support multiple 32-bit
        // constants parameters but for parity with the engine push_constants()
        // call shape (single (layout, stages, offset, size, data)), we
        // collapse all ranges into one root parameter that spans the union
        // of all (offset, size) pairs.
        std::uint32_t pc_param_idx = ~std::uint32_t { 0 };
        std::uint32_t pc_dwords    = 0;
        if (!desc.push_constants.empty())
        {
            std::uint32_t max_end = 0;
            for (const auto& r : desc.push_constants)
            {
                const auto end = r.offset + r.size;
                max_end = std::max(end, max_end);
            }
            // Round up to 4 bytes — root constants are u32 (DWORD) sized.
            pc_dwords = (max_end + 3u) / 4u;
            // D-ROOTCOST (Backend-to-100 Wave 1): D3D12_MAX_ROOT_COST is 64
            // DWORDs total and a descriptor table costs 1 DWORD each. The
            // historical code SILENTLY clamped pc_dwords when the union root
            // cost overflowed 64 — the silent-truncation bug class (same as
            // the BLAS-geo-cap lesson): the caller's push_constants() writes
            // past the clamped tail would land nowhere and corrupt nothing
            // visibly, an invisible miswire. Vulkan rejects an over-budget
            // pipeline layout at vkCreatePipelineLayout; mirror that and
            // FAIL LOUDLY with kInvalidArgument instead of clamping.
            const auto table_cost = static_cast<std::uint32_t>(params.size());
            if (pc_dwords + table_cost > 64u)
            {
                return std::unexpected(cd::rhi::rhi_errors::make(
                    cd::rhi::rhi_errors::Code::kInvalidArgument,
                    "create_pipeline_layout: push-constant root cost exceeds "
                    "the 64-DWORD D3D12 root-signature budget (descriptor "
                    "tables + 32-bit constants). Route the oversized block "
                    "through a uniform/constant buffer."));
            }
            if (pc_dwords > 0u)
            {
                D3D12_ROOT_PARAMETER pcp {};
                pcp.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
                pcp.Constants.ShaderRegister = 0;   // b0
                pcp.Constants.RegisterSpace  = 1;   // space1 (avoid CBV b0 collisions)
                pcp.Constants.Num32BitValues = pc_dwords;
                pcp.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
                pc_param_idx = static_cast<std::uint32_t>(params.size());
                params.push_back(pcp);
            }
        }

        D3D12_ROOT_SIGNATURE_DESC rsd {};
        rsd.NumParameters = static_cast<UINT>(params.size());
        rsd.pParameters = params.empty() ? nullptr : params.data();
        // B1b — static samplers back the SPIRV-Cross s-registers (one LINEAR-clamp
        // sampler per sampled-image binding at its (sM, spaceN); see the loop
        // above). Static samplers cost no root budget. Previously this was 0 and
        // every textured Sample() on D3D12 had an unbacked sampler register.
        rsd.NumStaticSamplers = static_cast<UINT>(static_samplers.size());
        rsd.pStaticSamplers =
            static_samplers.empty() ? nullptr : static_samplers.data();
        rsd.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

        ComPtr<ID3DBlob> blob;
        ComPtr<ID3DBlob> err;
        HRESULT hr = D3D12SerializeRootSignature(
            &rsd, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &err);
        if (FAILED(hr))
        {
            std::string msg = "D3D12SerializeRootSignature failed: ";
            if (err && err->GetBufferSize() > 0)
                msg.append(static_cast<const char*>(err->GetBufferPointer()),
                           err->GetBufferSize());
            return std::unexpected(cd::rhi::rhi_errors::make_owning(
                cd::rhi::rhi_errors::Code::kResourceCreationFailed, msg));
        }
        ComPtr<ID3D12RootSignature> root_sig;
        hr = device_->CreateRootSignature(
            0, blob->GetBufferPointer(), blob->GetBufferSize(),
            IID_PPV_ARGS(&root_sig));
        if (FAILED(hr))
        {
            char buf[160] {};
            std::snprintf(buf, sizeof(buf),
                          "CreateRootSignature failed: HRESULT 0x%08lx",
                          static_cast<unsigned long>(hr));
            return std::unexpected(cd::rhi::rhi_errors::make_owning(
                cd::rhi::rhi_errors::Code::kResourceCreationFailed,
                std::string { buf }));
        }
        PipelineLayoutRecord rec;
        rec.root_sig = root_sig;
        rec.root_sig_blob = blob;
        rec.table_params = std::move(table_params);
        rec.sampler_table_params = std::move(sampler_table_params);
        rec.push_constants_param  = pc_param_idx;
        rec.push_constants_dwords = pc_dwords;
        rec.bindless_sampler_param = bindless_sampler_param_idx;
        const auto id = next_id_++;
        pipeline_layouts_.emplace(id, std::move(rec));
        return cd::rhi::PipelineLayoutHandle { id, 1u };
    }
    void destroy_pipeline_layout(cd::rhi::PipelineLayoutHandle h) override
    {
        pipeline_layouts_.erase(h.index());
    }

    // ---- Graphics pipeline (REAL — Phase 14.C v0.36.0) --------------------

    [[nodiscard]] cd::core::Result<cd::rhi::GraphicsPipelineHandle>
    create_graphics_pipeline(const cd::rhi::GraphicsPipelineDesc& desc) override
    {
        auto layout_it = pipeline_layouts_.find(desc.layout.index());
        if (layout_it == pipeline_layouts_.end())
        {
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kInvalidArgument,
                "graphics pipeline: layout handle unknown"));
        }

        // phase766 — Mesh-shading PSO path. When the descriptor declares a
        // mesh shader, build a D3D12_PIPELINE_STATE_STREAM_DESC with the
        // MeshShader + (optional) AmplificationShader subobjects and route
        // through ID3D12Device2::CreatePipelineState. Vertex/IA stages are
        // suppressed — DispatchMesh drives the pipeline directly.
        if (desc.mesh_shader.value() != 0u)
        {
            return create_mesh_pipeline_(desc, layout_it->second.root_sig.Get());
        }

        auto vs_it = shader_modules_.find(desc.vertex_shader.index());
        auto fs_it = shader_modules_.find(desc.fragment_shader.index());
        if (vs_it == shader_modules_.end() || fs_it == shader_modules_.end())
        {
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kInvalidArgument,
                "graphics pipeline: missing vertex or fragment shader module"));
        }
        // Geometry / tess shaders are accepted by the descriptor but
        // optional; only wire them when present.
        const ShaderModuleRecord* gs = nullptr;
        if (desc.geometry_shader.value() != 0u)
        {
            auto it = shader_modules_.find(desc.geometry_shader.index());
            if (it != shader_modules_.end())
                gs = &it->second;
        }

        D3D12_GRAPHICS_PIPELINE_STATE_DESC psd {};
        psd.pRootSignature = layout_it->second.root_sig.Get();
        psd.VS = { vs_it->second.bytecode.data(),
                   vs_it->second.bytecode.size() };
        psd.PS = { fs_it->second.bytecode.data(),
                   fs_it->second.bytecode.size() };
        if (gs != nullptr)
            psd.GS = { gs->bytecode.data(), gs->bytecode.size() };

        // D1 (parity with Vulkan rasterizationSamples = map_samples(desc.samples)):
        // resolve + validate the PSO sample count once, against a representative
        // attachment format (first color, else depth, else a safe default), and
        // reuse it for both RasterizerState.MultisampleEnable and SampleDesc.
        const DXGI_FORMAT pso_sample_fmt = [&]() -> DXGI_FORMAT {
            if (!desc.color_attachment_formats.empty())
                return to_dxgi_format(desc.color_attachment_formats[0]);
            if (desc.depth_attachment_format != cd::rhi::Format::kUndefined)
                return to_dxgi_format(desc.depth_attachment_format);
            return DXGI_FORMAT_R8G8B8A8_UNORM;
        }();
        UINT pso_sample_quality = 0;
        const UINT pso_sample_count =
            map_samples(desc.samples, pso_sample_fmt, &pso_sample_quality);

        // Rasterizer state from the engine's RasterState.
        D3D12_RASTERIZER_DESC rs {};
        rs.FillMode = (desc.raster.polygon_mode == cd::rhi::PolygonMode::kFill)
            ? D3D12_FILL_MODE_SOLID : D3D12_FILL_MODE_WIREFRAME;
        switch (desc.raster.cull)
        {
            case cd::rhi::CullMode::kNone:  rs.CullMode = D3D12_CULL_MODE_NONE;  break;
            case cd::rhi::CullMode::kFront: rs.CullMode = D3D12_CULL_MODE_FRONT; break;
            case cd::rhi::CullMode::kBack:  rs.CullMode = D3D12_CULL_MODE_BACK;  break;
            // D3D12 has no FRONT_AND_BACK direct value — emulating it
            // requires culling both sides which equals "draw nothing".
            // We map to NONE (the only sensible interpretation that
            // doesn't suppress the entire geometry).
            case cd::rhi::CullMode::kFrontAndBack: rs.CullMode = D3D12_CULL_MODE_NONE; break;
        }
        // D3D12 "FrontCounterClockwise" — INVERTED vs the engine descriptor to
        // compensate for the NEGATIVE-HEIGHT viewport flip (phase1196,
        // set_viewport). The engine authors clip space in the Vulkan +Y-down
        // NDC convention; D3D12's NDC is +Y-up, so set_viewport maps with a
        // negative height (TopLeftY = y+height, Height = -height) to match
        // Vulkan pixel-for-pixel. That Y-axis flip in the NDC->window transform
        // INVERTS the window-space signed area the rasterizer uses for facing,
        // so the SAME clip-space triangle that Vulkan classifies as front
        // arrives at the D3D12 rasterizer with the OPPOSITE apparent winding.
        // To keep engine cull semantics backend-identical (a front_face=kCW +
        // cull=kBack triangle visible under Vulkan stays visible under D3D12),
        // FrontCounterClockwise must therefore be the LOGICAL NEGATION of the
        // un-flipped mapping: kClockwise -> TRUE, kCounterClockwise -> FALSE.
        // (Pre-fix this honored the descriptor directly on the stale premise
        // that "D3D12 doesn't Y-flip in NDC" — true before phase1196, a face-
        // cull parity bug after it. See ADR-20260615-ndc-y-handedness.)
        rs.FrontCounterClockwise =
            (desc.raster.front_face == cd::rhi::FrontFace::kClockwise)
                ? TRUE : FALSE;
        rs.DepthBias = 0;
        rs.DepthBiasClamp = 0.0F;
        rs.SlopeScaledDepthBias = 0.0F;
        rs.DepthClipEnable = TRUE;
        // D1: enable MSAA rasterization rules when samples > 1 (Vulkan's
        // multisample state implicitly does this via rasterizationSamples).
        rs.MultisampleEnable = (pso_sample_count > 1u) ? TRUE : FALSE;
        rs.AntialiasedLineEnable = FALSE;
        rs.ForcedSampleCount = 0;
        rs.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;
        psd.RasterizerState = rs;

        // Blend — D4 (phase1187): translate the engine's per-attachment
        // BlendAttachmentState[] into D3D12_BLEND_DESC, mirroring the Vulkan
        // reference (VulkanDevice.cpp:1618-1674). When no blend attachments
        // are supplied we synthesize opaque "write-all" entries for every
        // declared color attachment — identical to the Vulkan else-branch —
        // so fragment output still reaches the framebuffer. With one or more
        // attachments we fill RenderTarget[i] per attachment for MRT and set
        // IndependentBlendEnable when the attachments differ. The engine
        // surface carries no alpha-to-coverage flag (parity: the Vulkan
        // multisample state hardcodes alphaToCoverageEnable = VK_FALSE), so
        // AlphaToCoverageEnable stays FALSE here too.
        D3D12_BLEND_DESC bd {};
        bd.AlphaToCoverageEnable = FALSE;
        // Default every RT slot to opaque write-all (the D3D12 zero-init for
        // RenderTarget leaves Src/DestBlend == 0 which are *invalid* enums;
        // CreateGraphicsPipelineState only validates slot 0 when
        // IndependentBlendEnable == FALSE, but we fill all 8 defensively so
        // an MRT PSO never carries an invalid trailing slot).
        for (auto& rt : bd.RenderTarget)
        {
            rt.BlendEnable           = FALSE;
            rt.LogicOpEnable         = FALSE;
            rt.SrcBlend              = D3D12_BLEND_ONE;
            rt.DestBlend             = D3D12_BLEND_ZERO;
            rt.BlendOp               = D3D12_BLEND_OP_ADD;
            rt.SrcBlendAlpha         = D3D12_BLEND_ONE;
            rt.DestBlendAlpha        = D3D12_BLEND_ZERO;
            rt.BlendOpAlpha          = D3D12_BLEND_OP_ADD;
            rt.LogicOp               = D3D12_LOGIC_OP_NOOP;
            rt.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        }
        if (!desc.blend_attachments.empty())
        {
            const auto n = std::min<std::size_t>(desc.blend_attachments.size(), 8);
            for (std::size_t i = 0; i < n; ++i)
            {
                const auto& b = desc.blend_attachments[i];
                auto& rt = bd.RenderTarget[i];
                rt.BlendEnable           = b.blend_enable ? TRUE : FALSE;
                rt.SrcBlend              = to_d3d12_blend(b.src_color);
                rt.DestBlend             = to_d3d12_blend(b.dst_color);
                rt.BlendOp               = to_d3d12_blend_op(b.color_op);
                rt.SrcBlendAlpha         = to_d3d12_blend(b.src_alpha);
                rt.DestBlendAlpha        = to_d3d12_blend(b.dst_alpha);
                rt.BlendOpAlpha          = to_d3d12_blend_op(b.alpha_op);
                // The engine color-write mask shares the RGBA bit order with
                // D3D12_COLOR_WRITE_ENABLE_{RED,GREEN,BLUE,ALPHA} (1,2,4,8).
                rt.RenderTargetWriteMask =
                    static_cast<UINT8>(b.color_write_mask & 0xFu);
            }
            // IndependentBlendEnable when any attachment's state differs from
            // attachment 0 — D3D12 only consults RenderTarget[0] unless this
            // is TRUE, so MRT with per-target blend needs it set.
            bool independent = false;
            for (std::size_t i = 1; i < n && !independent; ++i)
            {
                const auto& a = desc.blend_attachments[0];
                const auto& c = desc.blend_attachments[i];
                independent =
                    a.blend_enable    != c.blend_enable    ||
                    a.src_color       != c.src_color       ||
                    a.dst_color       != c.dst_color       ||
                    a.color_op        != c.color_op        ||
                    a.src_alpha       != c.src_alpha       ||
                    a.dst_alpha       != c.dst_alpha       ||
                    a.alpha_op        != c.alpha_op        ||
                    a.color_write_mask != c.color_write_mask;
            }
            bd.IndependentBlendEnable = independent ? TRUE : FALSE;
        }
        else
        {
            bd.IndependentBlendEnable = FALSE;
        }
        psd.BlendState = bd;

        // Depth-stencil. Disabled by default for hello_d3d12_triangle —
        // the depth_attachment_format == kUndefined branch reflects that.
        // D5 (phase1186): translate the engine DepthStencilState — DepthFunc
        // from the compare op (was hardcoded LESS) and StencilEnable from
        // stencil_test (was hardcoded FALSE). Mirrors the Vulkan reference.
        D3D12_DEPTH_STENCIL_DESC ds {};
        ds.DepthEnable = (desc.depth_attachment_format != cd::rhi::Format::kUndefined)
            && desc.depth_stencil.depth_test;
        ds.DepthWriteMask = desc.depth_stencil.depth_write
            ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
        ds.DepthFunc = to_d3d12_compare_func(desc.depth_stencil.depth_compare);
        fill_d3d12_stencil_state(ds, desc.depth_stencil.stencil_test);
        psd.DepthStencilState = ds;

        psd.SampleMask = UINT_MAX;
        switch (desc.topology)
        {
            case cd::rhi::PrimitiveTopology::kPointList:
                psd.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
                break;
            case cd::rhi::PrimitiveTopology::kLineList:
            case cd::rhi::PrimitiveTopology::kLineStrip:
                psd.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
                break;
            default:
                psd.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
                break;
        }

        // Vertex input layout. Engine's VertexAttribute carries
        // (location, binding, format, offset) per attribute and the
        // VertexBinding[] carries the stride per binding slot. Translate
        // to D3D12_INPUT_ELEMENT_DESC[] — semantic name is "TEXCOORD"
        // with semantic index = engine's `location` so HLSL can
        // address them via TEXCOORD0/TEXCOORD1/... regardless of
        // what the user's `location` integer means.
        std::vector<D3D12_INPUT_ELEMENT_DESC> input_elements;
        input_elements.reserve(desc.vertex_attributes.size());
        std::vector<std::string> semantic_storage;  // keep names alive
        semantic_storage.reserve(desc.vertex_attributes.size());
        for (const auto& va : desc.vertex_attributes)
        {
            D3D12_INPUT_ELEMENT_DESC e {};
            semantic_storage.emplace_back("TEXCOORD");
            e.SemanticName = semantic_storage.back().c_str();
            e.SemanticIndex = va.location;
            e.Format = to_dxgi_format(va.format);
            e.InputSlot = va.binding;
            e.AlignedByteOffset = va.offset;
            e.InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
            e.InstanceDataStepRate = 0;
            input_elements.push_back(e);
        }
        psd.InputLayout.NumElements = static_cast<UINT>(input_elements.size());
        psd.InputLayout.pInputElementDescs =
            input_elements.empty() ? nullptr : input_elements.data();

        // Color attachment formats.
        psd.NumRenderTargets =
            static_cast<UINT>(std::min<std::size_t>(desc.color_attachment_formats.size(), 8));
        for (UINT i = 0; i < psd.NumRenderTargets; ++i)
        {
            psd.RTVFormats[i] = to_dxgi_format(desc.color_attachment_formats[i]);
        }
        psd.DSVFormat = (desc.depth_attachment_format != cd::rhi::Format::kUndefined)
            ? to_dxgi_format(desc.depth_attachment_format)
            : DXGI_FORMAT_UNKNOWN;
        // D1: honor GraphicsPipelineDesc.samples (was hardcoded 1x).
        psd.SampleDesc.Count   = pso_sample_count;
        psd.SampleDesc.Quality = pso_sample_quality;
        psd.NodeMask = 0;
        psd.CachedPSO = {};
        psd.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;

        ComPtr<ID3D12PipelineState> pso;
        HRESULT hr = device_->CreateGraphicsPipelineState(&psd, IID_PPV_ARGS(&pso));
        if (FAILED(hr))
        {
            char buf[160] {};
            std::snprintf(buf, sizeof(buf),
                          "CreateGraphicsPipelineState failed: HRESULT 0x%08lx",
                          static_cast<unsigned long>(hr));
            return std::unexpected(cd::rhi::rhi_errors::make_owning(
                cd::rhi::rhi_errors::Code::kResourceCreationFailed,
                std::string { buf }));
        }
        GraphicsPipelineRecord rec;
        rec.pso = pso;
        rec.layout_handle = desc.layout;
        // D6 (phase1187): capture each binding's stride from the engine
        // VertexBinding[] so bind_vertex_buffer can stamp the correct
        // D3D12_VERTEX_BUFFER_VIEW.StrideInBytes. Handles multiple bindings
        // / strides (e.g. separate position + instance streams).
        for (const auto& vb : desc.vertex_bindings)
            rec.binding_strides[vb.binding] = vb.stride;
        // Cache the D3D12 topology that bind_graphics_pipeline +
        // IASetPrimitiveTopology consumer needs (PSO carries the
        // *type* but the command-list call needs the *topology*
        // enum). We stamp kTriangleList by default for v0.36.0.
        switch (desc.topology)
        {
            case cd::rhi::PrimitiveTopology::kPointList:
                rec.d3d_topology = D3D_PRIMITIVE_TOPOLOGY_POINTLIST;
                break;
            case cd::rhi::PrimitiveTopology::kLineList:
                rec.d3d_topology = D3D_PRIMITIVE_TOPOLOGY_LINELIST;
                break;
            case cd::rhi::PrimitiveTopology::kLineStrip:
                rec.d3d_topology = D3D_PRIMITIVE_TOPOLOGY_LINESTRIP;
                break;
            case cd::rhi::PrimitiveTopology::kTriangleStrip:
                rec.d3d_topology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;
                break;
            default:
                rec.d3d_topology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
                break;
        }
        const auto id = next_id_++;
        graphics_pipelines_.emplace(id, std::move(rec));
        return cd::rhi::GraphicsPipelineHandle { id, 1u };
    }
    void destroy_graphics_pipeline(cd::rhi::GraphicsPipelineHandle h) override
    {
        graphics_pipelines_.erase(h.index());
    }

    // ---- Mesh-shading pipeline (REAL — phase766 F5) -----------------------
    //
    // Mesh-shading PSO via ID3D12Device2::CreatePipelineState +
    // D3D12_PIPELINE_STATE_STREAM_DESC. The PSS-stream is a sequence of
    // (subobject_type_alignas16, payload) tuples; we pack the subobjects
    // into a POD struct so the layout is contiguous and properly aligned
    // (each subobject must start on its natural alignment within the
    // stream — std::uint32_t for the tag + the payload type).
    //
    // Subobjects emitted (when applicable):
    //   * ROOT_SIGNATURE                                  -- pipeline layout
    //   * AS (Amplification Shader)                       -- optional
    //   * MS (Mesh Shader)                                -- REQUIRED
    //   * PS (Pixel Shader / fragment)                    -- optional but typical
    //   * RASTERIZER                                      -- raster state
    //   * DEPTH_STENCIL                                   -- depth/stencil state
    //   * BLEND                                           -- blend state
    //   * RENDER_TARGET_FORMATS                           -- color attachments
    //   * DEPTH_STENCIL_FORMAT                            -- depth attachment
    //   * SAMPLE_DESC                                     -- MSAA (count + quality)
    //   * SAMPLE_MASK                                     -- rasterizer mask
    //   * PRIMITIVE_TOPOLOGY (TRIANGLE)                   -- mesh PSOs default tri
    //
    // The DispatchMesh command-list entry point (ID3D12GraphicsCommandList6)
    // executes the pipeline. Vertex-input / IA stages are inert.
    [[nodiscard]] cd::core::Result<cd::rhi::GraphicsPipelineHandle>
    create_mesh_pipeline_(const cd::rhi::GraphicsPipelineDesc& desc,
                          ID3D12RootSignature*                 root_sig)
    {
        if (!features_.mesh_shader || !device2_)
        {
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kNotImplemented,
                "create_graphics_pipeline (mesh): adapter lacks "
                "D3D12_MESH_SHADER_TIER_1 / ID3D12Device2 (Shader Model 6.5+)"));
        }

        auto ms_it = shader_modules_.find(desc.mesh_shader.index());
        if (ms_it == shader_modules_.end())
        {
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kInvalidArgument,
                "create_graphics_pipeline (mesh): mesh-shader module handle unknown"));
        }
        const ShaderModuleRecord* as_rec = nullptr;
        if (desc.amplification_shader.value() != 0u)
        {
            auto as_it = shader_modules_.find(desc.amplification_shader.index());
            if (as_it == shader_modules_.end())
            {
                return std::unexpected(cd::rhi::rhi_errors::make(
                    cd::rhi::rhi_errors::Code::kInvalidArgument,
                    "create_graphics_pipeline (mesh): amplification shader module handle unknown"));
            }
            as_rec = &as_it->second;
        }
        // Pixel shader is optional (depth-only mesh-PSO is legal).
        const ShaderModuleRecord* ps_rec = nullptr;
        if (desc.fragment_shader.value() != 0u)
        {
            auto fs_it = shader_modules_.find(desc.fragment_shader.index());
            if (fs_it != shader_modules_.end())
                ps_rec = &fs_it->second;
        }

        // Pack PSS subobjects. Each (type-tag + payload) starts on its
        // natural alignment within the contiguous stream blob; the easiest
        // way to do this portably is one struct per subobject group with
        // CD3DX12-style alignas(void*) tags. We avoid CD3DX12 headers and
        // build the struct by hand for clarity.
        struct alignas(void*) PssSubObj
        {
            D3D12_PIPELINE_STATE_SUBOBJECT_TYPE type;
        };

        // D1 (parity with the graphics-PSO path + Vulkan rasterizationSamples):
        // resolve + validate the mesh-PSO sample count once against a
        // representative attachment format and reuse it for both the rasterizer
        // MultisampleEnable bit and the SAMPLE_DESC subobject below.
        const DXGI_FORMAT ms_sample_fmt = [&]() -> DXGI_FORMAT {
            if (!desc.color_attachment_formats.empty())
                return to_dxgi_format(desc.color_attachment_formats[0]);
            if (desc.depth_attachment_format != cd::rhi::Format::kUndefined)
                return to_dxgi_format(desc.depth_attachment_format);
            return DXGI_FORMAT_R8G8B8A8_UNORM;
        }();
        UINT ms_sample_quality = 0;
        const UINT ms_sample_count =
            map_samples(desc.samples, ms_sample_fmt, &ms_sample_quality);

        // Rasterizer
        D3D12_RASTERIZER_DESC rs {};
        rs.FillMode = (desc.raster.polygon_mode == cd::rhi::PolygonMode::kFill)
            ? D3D12_FILL_MODE_SOLID : D3D12_FILL_MODE_WIREFRAME;
        switch (desc.raster.cull)
        {
            case cd::rhi::CullMode::kNone:  rs.CullMode = D3D12_CULL_MODE_NONE;  break;
            case cd::rhi::CullMode::kFront: rs.CullMode = D3D12_CULL_MODE_FRONT; break;
            case cd::rhi::CullMode::kBack:  rs.CullMode = D3D12_CULL_MODE_BACK;  break;
            case cd::rhi::CullMode::kFrontAndBack: rs.CullMode = D3D12_CULL_MODE_NONE; break;
        }
        // FrontCounterClockwise INVERTED vs the descriptor — compensates the
        // negative-height viewport flip (phase1196) so engine cull semantics
        // match Vulkan. See the graphics-PSO site above + ADR-20260615 for the
        // winding-flip derivation. (mesh-shader PSO path.)
        rs.FrontCounterClockwise =
            (desc.raster.front_face == cd::rhi::FrontFace::kClockwise) ? TRUE : FALSE;
        rs.DepthClipEnable = TRUE;
        // D1: MSAA rasterization rules when samples > 1.
        rs.MultisampleEnable = (ms_sample_count > 1u) ? TRUE : FALSE;

        // Blend
        D3D12_BLEND_DESC bd {};
        bd.AlphaToCoverageEnable = FALSE;
        bd.IndependentBlendEnable = FALSE;
        for (auto& rt : bd.RenderTarget)
        {
            rt.BlendEnable = FALSE;
            rt.LogicOpEnable = FALSE;
            rt.SrcBlend = D3D12_BLEND_ONE;
            rt.DestBlend = D3D12_BLEND_ZERO;
            rt.BlendOp = D3D12_BLEND_OP_ADD;
            rt.SrcBlendAlpha = D3D12_BLEND_ONE;
            rt.DestBlendAlpha = D3D12_BLEND_ZERO;
            rt.BlendOpAlpha = D3D12_BLEND_OP_ADD;
            rt.LogicOp = D3D12_LOGIC_OP_NOOP;
            rt.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        }

        // Depth-stencil. D5 (phase1186): DepthFunc from compare op +
        // StencilEnable from stencil_test (was hardcoded LESS / FALSE).
        D3D12_DEPTH_STENCIL_DESC ds {};
        ds.DepthEnable = (desc.depth_attachment_format != cd::rhi::Format::kUndefined)
            && desc.depth_stencil.depth_test;
        ds.DepthWriteMask = desc.depth_stencil.depth_write
            ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
        ds.DepthFunc = to_d3d12_compare_func(desc.depth_stencil.depth_compare);
        fill_d3d12_stencil_state(ds, desc.depth_stencil.stencil_test);

        // Color formats
        D3D12_RT_FORMAT_ARRAY rtfmts {};
        rtfmts.NumRenderTargets =
            static_cast<UINT>(std::min<std::size_t>(desc.color_attachment_formats.size(), 8));
        for (UINT i = 0; i < rtfmts.NumRenderTargets; ++i)
            rtfmts.RTFormats[i] = to_dxgi_format(desc.color_attachment_formats[i]);

        // Depth format
        const DXGI_FORMAT dsv_fmt =
            (desc.depth_attachment_format != cd::rhi::Format::kUndefined)
                ? to_dxgi_format(desc.depth_attachment_format)
                : DXGI_FORMAT_UNKNOWN;

        // Sample desc — D1: honor GraphicsPipelineDesc.samples (was 1x).
        DXGI_SAMPLE_DESC sd {};
        sd.Count   = ms_sample_count;
        sd.Quality = ms_sample_quality;

        // Build the contiguous PSS blob via a single packed struct. The
        // D3D12 driver walks the stream by reading the tag, dispatching
        // to the matching subobject size, then advancing to the next
        // (alignas(void*)) boundary -- so we match that exactly here.
        struct PipelineStateStream
        {
            // Root signature
            alignas(void*) D3D12_PIPELINE_STATE_SUBOBJECT_TYPE root_sig_tag
                { D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_ROOT_SIGNATURE };
            ID3D12RootSignature* root_sig { nullptr };

            // AS (Amplification Shader)
            alignas(void*) D3D12_PIPELINE_STATE_SUBOBJECT_TYPE as_tag
                { D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_AS };
            D3D12_SHADER_BYTECODE as_bc {};

            // MS (Mesh Shader)
            alignas(void*) D3D12_PIPELINE_STATE_SUBOBJECT_TYPE ms_tag
                { D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_MS };
            D3D12_SHADER_BYTECODE ms_bc {};

            // PS (Pixel Shader)
            alignas(void*) D3D12_PIPELINE_STATE_SUBOBJECT_TYPE ps_tag
                { D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PS };
            D3D12_SHADER_BYTECODE ps_bc {};

            // Rasterizer
            alignas(void*) D3D12_PIPELINE_STATE_SUBOBJECT_TYPE rast_tag
                { D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RASTERIZER };
            D3D12_RASTERIZER_DESC rast_desc {};

            // Depth-stencil
            alignas(void*) D3D12_PIPELINE_STATE_SUBOBJECT_TYPE ds_tag
                { D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL };
            D3D12_DEPTH_STENCIL_DESC ds_desc {};

            // Blend
            alignas(void*) D3D12_PIPELINE_STATE_SUBOBJECT_TYPE blend_tag
                { D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_BLEND };
            D3D12_BLEND_DESC blend_desc {};

            // Sample mask
            alignas(void*) D3D12_PIPELINE_STATE_SUBOBJECT_TYPE smask_tag
                { D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_MASK };
            UINT sample_mask { UINT_MAX };

            // Sample desc
            alignas(void*) D3D12_PIPELINE_STATE_SUBOBJECT_TYPE sdesc_tag
                { D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_DESC };
            DXGI_SAMPLE_DESC sample_desc {};

            // RT formats
            alignas(void*) D3D12_PIPELINE_STATE_SUBOBJECT_TYPE rtfmt_tag
                { D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RENDER_TARGET_FORMATS };
            D3D12_RT_FORMAT_ARRAY rt_formats {};

            // DS format
            alignas(void*) D3D12_PIPELINE_STATE_SUBOBJECT_TYPE dsfmt_tag
                { D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL_FORMAT };
            DXGI_FORMAT ds_format { DXGI_FORMAT_UNKNOWN };
        };

        PipelineStateStream pss {};
        pss.root_sig = root_sig;
        if (as_rec != nullptr)
        {
            pss.as_bc.pShaderBytecode = as_rec->bytecode.data();
            pss.as_bc.BytecodeLength  = as_rec->bytecode.size();
        }
        pss.ms_bc.pShaderBytecode = ms_it->second.bytecode.data();
        pss.ms_bc.BytecodeLength  = ms_it->second.bytecode.size();
        if (ps_rec != nullptr)
        {
            pss.ps_bc.pShaderBytecode = ps_rec->bytecode.data();
            pss.ps_bc.BytecodeLength  = ps_rec->bytecode.size();
        }
        pss.rast_desc   = rs;
        pss.ds_desc     = ds;
        pss.blend_desc  = bd;
        pss.sample_desc = sd;
        pss.rt_formats  = rtfmts;
        pss.ds_format   = dsv_fmt;

        D3D12_PIPELINE_STATE_STREAM_DESC stream_desc {};
        stream_desc.SizeInBytes                   = sizeof(pss);
        stream_desc.pPipelineStateSubobjectStream = &pss;

        ComPtr<ID3D12PipelineState> pso;
        const HRESULT hr =
            device2_->CreatePipelineState(&stream_desc, IID_PPV_ARGS(&pso));
        if (FAILED(hr))
        {
            char buf[160] {};
            std::snprintf(buf, sizeof(buf),
                          "CreatePipelineState(mesh PSS) failed: HRESULT 0x%08lx",
                          static_cast<unsigned long>(hr));
            return std::unexpected(cd::rhi::rhi_errors::make_owning(
                cd::rhi::rhi_errors::Code::kResourceCreationFailed,
                std::string { buf }));
        }

        GraphicsPipelineRecord rec;
        rec.pso = pso;
        rec.layout_handle = desc.layout;
        // Mesh PSOs ignore IA topology -- DispatchMesh drives directly --
        // but the bind path still calls IASetPrimitiveTopology. Use TRI
        // as a safe default; the runtime will ignore it for mesh PSOs.
        rec.d3d_topology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
        rec.is_mesh_shader = true;
        const auto id = next_id_++;
        graphics_pipelines_.emplace(id, std::move(rec));
        return cd::rhi::GraphicsPipelineHandle { id, 1u };
    }

    // ---- Compute pipeline (REAL — phase466 v0.99.93 M4-parity-closeout) ----
    //
    // CreateComputePipelineState consumes a CS shader-bytecode blob plus
    // the root-signature from a cd::rhi::PipelineLayoutHandle. The shader
    // module records carry raw DXIL bytecode (already DXC-compiled by
    // D3D12ShaderCompile.cpp).
    [[nodiscard]] cd::core::Result<cd::rhi::ComputePipelineHandle>
    create_compute_pipeline(const cd::rhi::ComputePipelineDesc& desc) override
    {
        auto layout_it = pipeline_layouts_.find(desc.layout.index());
        if (layout_it == pipeline_layouts_.end())
        {
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kInvalidArgument,
                "create_compute_pipeline: unknown PipelineLayout handle"));
        }
        auto cs_it = shader_modules_.find(desc.shader.index());
        if (cs_it == shader_modules_.end())
        {
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kInvalidArgument,
                "create_compute_pipeline: unknown compute shader module"));
        }

        D3D12_COMPUTE_PIPELINE_STATE_DESC cpd {};
        cpd.pRootSignature = layout_it->second.root_sig.Get();
        cpd.CS.pShaderBytecode = cs_it->second.bytecode.data();
        cpd.CS.BytecodeLength  = cs_it->second.bytecode.size();
        cpd.NodeMask = 0;
        cpd.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;

        ComPtr<ID3D12PipelineState> pso;
        HRESULT hr = device_->CreateComputePipelineState(&cpd, IID_PPV_ARGS(&pso));
        if (FAILED(hr))
        {
            char buf[160] {};
            std::snprintf(buf, sizeof(buf),
                          "CreateComputePipelineState failed: HRESULT 0x%08lx",
                          static_cast<unsigned long>(hr));
            return std::unexpected(cd::rhi::rhi_errors::make_owning(
                cd::rhi::rhi_errors::Code::kResourceCreationFailed,
                std::string { buf }));
        }

        ComputePipelineRecord rec;
        rec.pso = pso;
        rec.layout_handle = desc.layout;
        const auto id = next_id_++;
        compute_pipelines_.emplace(id, std::move(rec));
        return cd::rhi::ComputePipelineHandle { id, 1u };
    }
    void destroy_compute_pipeline(cd::rhi::ComputePipelineHandle h) override
    {
        compute_pipelines_.erase(h.index());
    }

    // ---- DXR pipeline state object (Phase 398) ----------------------------
    //
    // Creates a D3D12_STATE_OBJECT (collection type = RAYTRACING_PIPELINE)
    // when the adapter has DXR support. Each RtShaderEntry becomes one
    // DXIL_LIBRARY subobject + one shader-config / root-signature
    // association. We produce a minimal but functional RTPSO:
    //   - DXIL_LIBRARY per shader (bytecode carried by ShaderModuleHandle)
    //   - RAYTRACING_SHADER_CONFIG (payload + attribute sizes)
    //   - RAYTRACING_PIPELINE_CONFIG (max recursion)
    //   - GLOBAL_ROOT_SIGNATURE (empty if no layout supplied)
    //
    // Scope-down per ADR: if DXR is absent on the adapter, return
    // kNotImplemented with a clear message. Callers MUST gate on
    // features().ray_tracing before creating an RT pipeline.
    [[nodiscard]] cd::core::Result<cd::rhi::RtPipelineHandle>
    create_rt_pipeline(const cd::rhi::RtPipelineDesc& desc,
                       cd::rhi::PipelineLayoutHandle   layout) override
    {
        if (!features_.ray_tracing || !device5_)
        {
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kNotImplemented,
                "create_rt_pipeline: adapter lacks DXR (check features().ray_tracing)"));
        }

        // Collect subobjects into a flat vector so we can build the
        // D3D12_STATE_OBJECT_DESC with a contiguous array. Each subobject
        // is a tagged union; the lifetime of pointed-to data must match
        // the CreateStateObject call.

        // --- 1. DXIL library subobjects (one per shader entry) ---
        std::vector<D3D12_DXIL_LIBRARY_DESC> lib_descs;
        lib_descs.reserve(desc.shaders.size());
        // Keep the export-desc arrays alive alongside lib_descs.
        std::vector<std::vector<D3D12_EXPORT_DESC>> lib_exports;
        lib_exports.reserve(desc.shaders.size());
        std::vector<std::wstring> export_names;   // wide-char storage
        export_names.reserve(desc.shaders.size());

        for (const auto& se : desc.shaders)
        {
            auto sm_it = shader_modules_.find(se.module.index());
            if (sm_it == shader_modules_.end())
            {
                return std::unexpected(cd::rhi::rhi_errors::make(
                    cd::rhi::rhi_errors::Code::kInvalidArgument,
                    "create_rt_pipeline: shader module handle unknown"));
            }
            const auto& sm = sm_it->second;

            // Convert UTF-8 entry point to wide-char for the DXIL API.
            const int wlen = MultiByteToWideChar(
                CP_UTF8, 0,
                sm.entry_point.c_str(),
                static_cast<int>(sm.entry_point.size()),
                nullptr, 0);
            std::wstring wname(static_cast<std::size_t>(wlen), L'\0');
            MultiByteToWideChar(CP_UTF8, 0,
                sm.entry_point.c_str(),
                static_cast<int>(sm.entry_point.size()),
                wname.data(), wlen);
            export_names.push_back(std::move(wname));

            D3D12_EXPORT_DESC exp {};
            exp.Name = export_names.back().c_str();
            exp.ExportToRename = nullptr;
            exp.Flags = D3D12_EXPORT_FLAG_NONE;

            std::vector<D3D12_EXPORT_DESC> exps { exp };
            lib_exports.push_back(std::move(exps));

            D3D12_DXIL_LIBRARY_DESC ld {};
            ld.DXILLibrary.pShaderBytecode = sm.bytecode.data();
            ld.DXILLibrary.BytecodeLength  = sm.bytecode.size();
            ld.NumExports   = 1;
            ld.pExports     = lib_exports.back().data();
            lib_descs.push_back(ld);
        }

        // --- 1b. Hit groups (one D3D12_HIT_GROUP_DESC per group that
        //         carries a closest-hit / any-hit / intersection shader) ---
        //
        // Mirror the Vulkan grouping convention: each unique RtShaderEntry
        // ::group index — in first-occurrence order — becomes one SBT shader
        // group. A general group (raygen / miss / callable) needs no DXR
        // subobject: its SBT identifier IS the shader export name. A hit group
        // (closest-hit + optional any-hit + intersection) needs a
        // D3D12_HIT_GROUP subobject whose HitGroupExport is the name the SBT
        // identifier is then looked up under. group_export_names is filled in
        // the SAME order so get_rt_shader_group_handles can map group index ->
        // export name.
        std::vector<std::uint32_t> group_ids_seen;
        for (const auto& se : desc.shaders)
        {
            if (std::ranges::find(group_ids_seen, se.group) == group_ids_seen.end())
                group_ids_seen.push_back(se.group);
        }

        std::vector<std::wstring>          group_export_names;   // SBT order
        std::vector<std::wstring>          hit_group_names;      // pinned storage
        std::vector<D3D12_HIT_GROUP_DESC>  hit_group_descs;
        group_export_names.reserve(group_ids_seen.size());
        hit_group_names.reserve(group_ids_seen.size());
        hit_group_descs.reserve(group_ids_seen.size());

        for (auto gid : group_ids_seen)
        {
            const std::wstring* general_export      = nullptr;
            const std::wstring* closest_hit_export  = nullptr;
            const std::wstring* any_hit_export      = nullptr;
            const std::wstring* intersection_export = nullptr;
            for (std::size_t i = 0; i < desc.shaders.size(); ++i)
            {
                const auto& se = desc.shaders[i];
                if (se.group != gid) continue;
                const std::wstring* name = &export_names[i];
                switch (se.stage)
                {
                    case cd::rhi::RtShaderStage::kRaygen:
                    case cd::rhi::RtShaderStage::kMiss:
                    case cd::rhi::RtShaderStage::kCallable:
                        general_export = name;
                        break;
                    case cd::rhi::RtShaderStage::kClosestHit:
                        closest_hit_export = name;
                        break;
                    case cd::rhi::RtShaderStage::kAnyHit:
                        any_hit_export = name;
                        break;
                    case cd::rhi::RtShaderStage::kIntersection:
                        intersection_export = name;
                        break;
                }
            }

            const bool is_hit_group =
                closest_hit_export != nullptr || any_hit_export != nullptr ||
                intersection_export != nullptr;

            if (!is_hit_group)
            {
                // General group — the SBT identifier is the shader export
                // name directly. A malformed group with no shader at all is
                // rejected: there is nothing to bind an SBT record to.
                if (general_export == nullptr)
                {
                    return std::unexpected(cd::rhi::rhi_errors::make(
                        cd::rhi::rhi_errors::Code::kInvalidArgument,
                        "create_rt_pipeline: shader group has no shader"));
                }
                group_export_names.push_back(*general_export);
                continue;
            }

            // Hit group — synthesise a unique export name (DXR requires every
            // export in the state object to be unique; the per-shader DXIL
            // exports already exist, so the hit-group export is a NEW name).
            std::wstring hg_name = L"hitgroup_" + std::to_wstring(gid);
            const auto& pinned = hit_group_names.emplace_back(std::move(hg_name));

            D3D12_HIT_GROUP_DESC hg {};
            hg.HitGroupExport = pinned.c_str();
            hg.Type = (intersection_export != nullptr)
                          ? D3D12_HIT_GROUP_TYPE_PROCEDURAL_PRIMITIVE
                          : D3D12_HIT_GROUP_TYPE_TRIANGLES;
            hg.ClosestHitShaderImport  =
                closest_hit_export  != nullptr ? closest_hit_export->c_str()  : nullptr;
            hg.AnyHitShaderImport      =
                any_hit_export      != nullptr ? any_hit_export->c_str()      : nullptr;
            hg.IntersectionShaderImport =
                intersection_export != nullptr ? intersection_export->c_str() : nullptr;
            hit_group_descs.push_back(hg);
            group_export_names.push_back(pinned);
        }

        // --- 2. Shader config ---
        D3D12_RAYTRACING_SHADER_CONFIG shader_cfg {};
        shader_cfg.MaxPayloadSizeInBytes   = desc.max_payload_bytes;
        shader_cfg.MaxAttributeSizeInBytes = desc.max_attribute_bytes;

        // --- 3. Pipeline config ---
        D3D12_RAYTRACING_PIPELINE_CONFIG pipeline_cfg {};
        pipeline_cfg.MaxTraceRecursionDepth = desc.max_recursion;

        // --- 4. Global root signature (empty if no layout) ---
        //
        // A DXR GLOBAL root signature MUST NOT carry
        // D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT (a
        // graphics-IA-only flag) — CreateStateObject rejects it with
        // E_INVALIDARG. create_pipeline_layout always sets that flag for the
        // shared graphics root sig, so we cannot reuse layout.root_sig
        // directly. Instead we deserialize the layout's serialized blob, strip
        // the IA flag, and re-serialize a DXR-compatible root signature.
        ComPtr<ID3D12RootSignature> global_rs;
        if (layout.value() != 0u)
        {
            auto pl_it = pipeline_layouts_.find(layout.index());
            if (pl_it != pipeline_layouts_.end() && pl_it->second.root_sig_blob)
            {
                const auto& src_blob = pl_it->second.root_sig_blob;
                ComPtr<ID3D12RootSignatureDeserializer> deser;
                if (SUCCEEDED(D3D12CreateRootSignatureDeserializer(
                        src_blob->GetBufferPointer(),
                        src_blob->GetBufferSize(),
                        IID_PPV_ARGS(&deser))))
                {
                    const D3D12_ROOT_SIGNATURE_DESC* src_desc =
                        deser->GetRootSignatureDesc();
                    if (src_desc != nullptr)
                    {
                        D3D12_ROOT_SIGNATURE_DESC dxr_desc = *src_desc;
                        // Strip the graphics-only IA flag; everything else
                        // (params, static samplers, deny flags) is preserved.
                        dxr_desc.Flags &= ~D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
                        ComPtr<ID3DBlob> dxr_blob;
                        ComPtr<ID3DBlob> dxr_err;
                        if (SUCCEEDED(D3D12SerializeRootSignature(
                                &dxr_desc, D3D_ROOT_SIGNATURE_VERSION_1,
                                &dxr_blob, &dxr_err)))
                        {
                            (void)device_->CreateRootSignature(
                                0, dxr_blob->GetBufferPointer(),
                                dxr_blob->GetBufferSize(),
                                IID_PPV_ARGS(&global_rs));
                        }
                    }
                }
            }
        }
        if (!global_rs)
        {
            // Minimal empty root signature for RTPSO.
            D3D12_ROOT_SIGNATURE_DESC rsd {};
            rsd.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;
            ComPtr<ID3DBlob> blob;
            ComPtr<ID3DBlob> err;
            if (SUCCEEDED(D3D12SerializeRootSignature(
                    &rsd, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &err)))
            {
                (void)device_->CreateRootSignature(
                    0, blob->GetBufferPointer(), blob->GetBufferSize(),
                    IID_PPV_ARGS(&global_rs));
            }
        }

        // --- 5. Assemble flat subobject array ---
        std::vector<D3D12_STATE_SUBOBJECT> subs;
        subs.reserve(lib_descs.size() + hit_group_descs.size() + 3u);

        for (auto& ld : lib_descs)
        {
            D3D12_STATE_SUBOBJECT s {};
            s.Type = D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY;
            s.pDesc = &ld;
            subs.push_back(s);
        }
        for (auto& hg : hit_group_descs)
        {
            D3D12_STATE_SUBOBJECT s {};
            s.Type  = D3D12_STATE_SUBOBJECT_TYPE_HIT_GROUP;
            s.pDesc = &hg;
            subs.push_back(s);
        }
        {
            D3D12_STATE_SUBOBJECT s {};
            s.Type  = D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_SHADER_CONFIG;
            s.pDesc = &shader_cfg;
            subs.push_back(s);
        }
        {
            D3D12_STATE_SUBOBJECT s {};
            s.Type  = D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_PIPELINE_CONFIG;
            s.pDesc = &pipeline_cfg;
            subs.push_back(s);
        }
        D3D12_GLOBAL_ROOT_SIGNATURE grs {};
        grs.pGlobalRootSignature = global_rs.Get();
        if (global_rs)
        {
            D3D12_STATE_SUBOBJECT s {};
            s.Type  = D3D12_STATE_SUBOBJECT_TYPE_GLOBAL_ROOT_SIGNATURE;
            s.pDesc = &grs;
            subs.push_back(s);
        }

        D3D12_STATE_OBJECT_DESC sod {};
        sod.Type            = D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE;
        sod.NumSubobjects   = static_cast<UINT>(subs.size());
        sod.pSubobjects     = subs.data();

        ComPtr<ID3D12StateObject> state_obj;
        HRESULT hr = device5_->CreateStateObject(&sod, IID_PPV_ARGS(&state_obj));
        if (FAILED(hr))
        {
            char buf[160] {};
            std::snprintf(buf, sizeof(buf),
                          "CreateStateObject(RTPSO) failed: HRESULT 0x%08lx",
                          static_cast<unsigned long>(hr));
            return std::unexpected(cd::rhi::rhi_errors::make_owning(
                cd::rhi::rhi_errors::Code::kResourceCreationFailed,
                std::string { buf }));
        }

        // Query ID3D12StateObjectProperties for shader-group handle size.
        ComPtr<ID3D12StateObjectProperties> props;
        (void)state_obj.As(&props);

        RtPipelineRecord rec;
        rec.state_obj = state_obj;
        rec.props     = props;
        rec.group_handle_size =
            D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;  // 32 per DXR spec
        rec.group_export_names = std::move(group_export_names);
        rec.global_root_sig    = global_rs;
        rec.layout_handle      = layout;
        const auto id = next_id_++;
        rt_pipelines_.emplace(id, std::move(rec));
        return cd::rhi::RtPipelineHandle { id, 1u };
    }

    void destroy_rt_pipeline(cd::rhi::RtPipelineHandle h) override
    {
        rt_pipelines_.erase(h.index());
    }

    [[nodiscard]] std::uint32_t rt_shader_group_handle_size() const noexcept override
    {
        // DXR mandates 32-byte shader identifier size.
        return features_.ray_tracing ? 32u : 0u;
    }
    [[nodiscard]] std::uint32_t rt_shader_group_handle_alignment() const noexcept override
    {
        // DXR requires 64-byte alignment for shader records.
        return features_.ray_tracing ? 64u : 0u;
    }
    [[nodiscard]] std::uint32_t rt_shader_group_base_alignment() const noexcept override
    {
        return features_.ray_tracing ? 64u : 0u;
    }

    [[nodiscard]] cd::core::Result<void>
    get_rt_shader_group_handles(cd::rhi::RtPipelineHandle pipeline,
                                std::uint32_t             first_group,
                                std::uint32_t             group_count,
                                std::span<std::byte>      out) override
    {
        if (!features_.ray_tracing)
        {
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kNotImplemented,
                "get_rt_shader_group_handles: DXR unavailable"));
        }
        auto it = rt_pipelines_.find(pipeline.index());
        if (it == rt_pipelines_.end() || !it->second.props)
        {
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kInvalidArgument,
                "get_rt_shader_group_handles: unknown RT pipeline handle"));
        }
        // ShaderIdentifier copy per group. Each identifier is 32 bytes.
        // Groups are addressed by sequential index in the SAME first-occurrence
        // order create_rt_pipeline emitted them (raygen=0, miss=1, hit=2, …).
        // We retained the per-group export name at creation time, so we can
        // call GetShaderIdentifier(name) and copy the real 32-byte identifier
        // the SBT builder consumes.
        const auto& rec = it->second;
        const UINT id_size = rec.group_handle_size;
        if (out.size() < static_cast<std::size_t>(group_count) * id_size)
        {
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kInvalidArgument,
                "get_rt_shader_group_handles: output buffer too small"));
        }
        const std::size_t total_groups = rec.group_export_names.size();
        if (static_cast<std::size_t>(first_group) + group_count > total_groups)
        {
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kInvalidArgument,
                "get_rt_shader_group_handles: group range out of bounds"));
        }
        for (std::uint32_t g = 0; g < group_count; ++g)
        {
            const std::wstring& name = rec.group_export_names[first_group + g];
            const void* identifier = rec.props->GetShaderIdentifier(name.c_str());
            std::byte* dst = out.data() + static_cast<std::size_t>(g) * id_size;
            if (identifier == nullptr)
            {
                // Export not present in the state object — zero-fill that
                // record so the SBT slot is inert rather than garbage.
                std::memset(dst, 0, id_size);
                continue;
            }
            std::memcpy(dst, identifier, id_size);
        }
        return {};
    }

    // ---- Descriptor set (REAL — Phase 15.B v0.43.0) -----------------------

    [[nodiscard]] cd::core::Result<cd::rhi::DescriptorSetHandle>
    allocate_descriptor_set(cd::rhi::DescriptorSetLayoutHandle layout) override
    {
        auto it = descriptor_set_layouts_.find(layout.index());
        if (it == descriptor_set_layouts_.end())
        {
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kInvalidArgument,
                "allocate_descriptor_set: unknown layout"));
        }
        const auto vcount = it->second.view_count;
        if (cpu_heap_ == nullptr)
        {
            // Lazily create the CPU-visible descriptor heap. 4096 slots
            // covers the marathon-shippable set count; future waves can
            // grow it or switch to a slab allocator.
            D3D12_DESCRIPTOR_HEAP_DESC hd {};
            hd.NumDescriptors = 4096;
            hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
            hd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
            if (FAILED(device_->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&cpu_heap_))))
            {
                return std::unexpected(cd::rhi::rhi_errors::make(
                    cd::rhi::rhi_errors::Code::kResourceCreationFailed,
                    "CPU descriptor heap creation failed"));
            }
            cpu_heap_increment_ = device_->GetDescriptorHandleIncrementSize(
                D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        }
        if (cpu_heap_cursor_ + vcount > 4096)
        {
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kResourceCreationFailed,
                "CPU descriptor heap exhausted (4096-slot cap)"));
        }
        DescriptorSetRecord rec;
        rec.layout_handle = layout;
        rec.cpu_heap_offset = cpu_heap_cursor_;
        rec.view_count = vcount;
        cpu_heap_cursor_ += vcount;

        // D2 — reserve CPU sampler-staging slots for the set's bare-`kSampler`
        // bindings (parity with the CBV/SRV/UAV reservation above). Zero when
        // the layout declares no kSampler binding — classic combined-sampler /
        // bindless sets keep their existing sampler handling.
        const auto scount = it->second.sampler_count;
        if (scount > 0)
        {
            if (auto r = ensure_sampler_set_cpu_heap_(); !r.has_value())
                return std::unexpected(r.error());
            if (sampler_set_cpu_cursor_ + scount > kSamplerSetCpuCap)
            {
                return std::unexpected(cd::rhi::rhi_errors::make(
                    cd::rhi::rhi_errors::Code::kResourceCreationFailed,
                    "allocate_descriptor_set: per-set sampler staging heap exhausted"));
            }
            rec.sampler_cpu_offset = sampler_set_cpu_cursor_;
            rec.sampler_count      = scount;
            sampler_set_cpu_cursor_ += scount;
        }
        const auto id = next_id_++;
        descriptor_sets_.emplace(id, rec);
        return cd::rhi::DescriptorSetHandle { id, 1u };
    }
    void destroy_descriptor_set(cd::rhi::DescriptorSetHandle h) override
    {
        descriptor_sets_.erase(h.index());
        // CPU heap slots are not reclaimed (bump allocator). Acceptable
        // until the slab allocator wave.
    }

    [[nodiscard]] cd::core::Result<void>
    update_descriptor_set(
        cd::rhi::DescriptorSetHandle h,
        std::span<const cd::rhi::DescriptorWrite> writes) override
    {
        auto set_it = descriptor_sets_.find(h.index());
        if (set_it == descriptor_sets_.end())
        {
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kInvalidArgument,
                "update_descriptor_set: unknown set"));
        }
        const auto& set = set_it->second;
        auto layout_it = descriptor_set_layouts_.find(set.layout_handle.index());
        if (layout_it == descriptor_set_layouts_.end())
        {
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kInvalidArgument,
                "update_descriptor_set: layout vanished"));
        }
        const auto& layout = layout_it->second;

        D3D12_CPU_DESCRIPTOR_HANDLE heap_base = cpu_heap_->GetCPUDescriptorHandleForHeapStart();
        heap_base.ptr += static_cast<SIZE_T>(set.cpu_heap_offset) *
                         cpu_heap_increment_;

        for (const auto& w : writes)
        {
            // Find the offset of this binding within the set's
            // descriptor table. Sum view-typed binding counts of
            // earlier bindings for the CBV/SRV/UAV table, and (D2)
            // sampler-typed counts for the separate SAMPLER table.
            std::uint32_t offset = 0;
            std::uint32_t sampler_offset = 0;
            bool found = false;
            for (const auto& b : layout.bindings)
            {
                if (b.binding == w.binding)
                {
                    found = true;
                    break;
                }
                if (b.type != cd::rhi::DescriptorType::kSampler)
                    offset += b.count;
                else
                    sampler_offset += b.count;
            }
            if (!found)
            {
                return std::unexpected(cd::rhi::rhi_errors::make(
                    cd::rhi::rhi_errors::Code::kInvalidArgument,
                    "update_descriptor_set: write binding not in layout"));
            }
            D3D12_CPU_DESCRIPTOR_HANDLE dst = heap_base;
            dst.ptr += static_cast<SIZE_T>(offset + w.array_element) *
                       cpu_heap_increment_;

            switch (w.type)
            {
                case cd::rhi::DescriptorType::kUniformBuffer:
                case cd::rhi::DescriptorType::kUniformBufferDynamic:
                {
                    auto buf_it = buffers_.find(w.buffer.index());
                    if (buf_it == buffers_.end())
                    {
                        return std::unexpected(cd::rhi::rhi_errors::make(
                            cd::rhi::rhi_errors::Code::kInvalidArgument,
                            "update_descriptor_set: CBV buffer unknown"));
                    }
                    D3D12_CONSTANT_BUFFER_VIEW_DESC cbv {};
                    cbv.BufferLocation =
                        buf_it->second.resource->GetGPUVirtualAddress() + w.buffer_offset;
                    UINT64 range = w.buffer_range == 0
                        ? (buf_it->second.size - w.buffer_offset)
                        : w.buffer_range;
                    // CBV size must be a multiple of 256.
                    range = (range + 255u) & ~static_cast<UINT64>(255u);
                    cbv.SizeInBytes = static_cast<UINT>(range);
                    device_->CreateConstantBufferView(&cbv, dst);
                    break;
                }
                case cd::rhi::DescriptorType::kSampledImage:
                {
                    auto view_it = texture_views_.find(w.view.index());
                    if (view_it == texture_views_.end())
                    {
                        return std::unexpected(cd::rhi::rhi_errors::make(
                            cd::rhi::rhi_errors::Code::kInvalidArgument,
                            "update_descriptor_set: SRV view unknown"));
                    }
                    auto tex_it = textures_.find(view_it->second.parent.index());
                    if (tex_it == textures_.end())
                    {
                        return std::unexpected(cd::rhi::rhi_errors::make(
                            cd::rhi::rhi_errors::Code::kInvalidArgument,
                            "update_descriptor_set: SRV texture unknown"));
                    }
                    D3D12_SHADER_RESOURCE_VIEW_DESC srv {};
                    srv.Format = view_it->second.format;
                    srv.Shader4ComponentMapping =
                        D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                    // Phase 126/393/395 — pick dimension from view flags.
                    if (view_it->second.is_cube)
                    {
                        srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
                        srv.TextureCube.MostDetailedMip = 0;
                        srv.TextureCube.MipLevels = 1;
                        srv.TextureCube.ResourceMinLODClamp = 0.0F;
                    }
                    else if (view_it->second.is_1d)
                    {
                        srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE1D;
                        srv.Texture1D.MostDetailedMip = 0;
                        srv.Texture1D.MipLevels = 1;
                        srv.Texture1D.ResourceMinLODClamp = 0.0F;
                    }
                    else if (view_it->second.is_3d)
                    {
                        srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D;
                        srv.Texture3D.MostDetailedMip = 0;
                        srv.Texture3D.MipLevels = 1;
                        srv.Texture3D.ResourceMinLODClamp = 0.0F;
                    }
                    else if (view_it->second.is_ms)
                    {
                        // D-SRV-MS: sample a multisampled texture (Texture2DMS).
                        srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DMS;
                    }
                    else
                    {
                        srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
                        srv.Texture2D.MipLevels = 1;
                    }
                    device_->CreateShaderResourceView(
                        tex_it->second.resource.Get(), &srv, dst);
                    break;
                }
                case cd::rhi::DescriptorType::kStorageImage:
                {
                    // UAV (Phase 16.B). Texture2D RW for compute /
                    // graphics-side image stores.
                    auto view_it = texture_views_.find(w.view.index());
                    if (view_it == texture_views_.end())
                        return std::unexpected(cd::rhi::rhi_errors::make(
                            cd::rhi::rhi_errors::Code::kInvalidArgument,
                            "update_descriptor_set: UAV view unknown"));
                    auto tex_it = textures_.find(view_it->second.parent.index());
                    if (tex_it == textures_.end())
                        return std::unexpected(cd::rhi::rhi_errors::make(
                            cd::rhi::rhi_errors::Code::kInvalidArgument,
                            "update_descriptor_set: UAV texture unknown"));
                    D3D12_UNORDERED_ACCESS_VIEW_DESC uav {};
                    uav.Format = view_it->second.format;
                    // D4 (parity with the kSampledImage SRV path above): the UAV
                    // ViewDimension must follow the view's texture type, not be
                    // hardcoded TEXTURE2D. A TEXTURE2D UAV on a 3D/array/cube
                    // resource read/writes only slice 0 (or fails creation), so
                    // compute image stores to anything but a plain 2D texture
                    // silently hit the wrong data. Mirrors the SRV branch.
                    if (view_it->second.is_3d)
                    {
                        // No D3D12_UAV_DIMENSION_TEXTURE3D mis-slicing: bind the
                        // full depth so a compute shader can store to any voxel.
                        uav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE3D;
                        uav.Texture3D.MipSlice    = 0;
                        uav.Texture3D.FirstWSlice = 0;
                        uav.Texture3D.WSize       = static_cast<UINT>(-1);  // all depth slices
                    }
                    else if (view_it->second.is_cube)
                    {
                        // D3D12 has no native cube UAV; expose the 6 faces as a
                        // TEXTURE2DARRAY (matches the documented D3D12 idiom).
                        uav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2DARRAY;
                        uav.Texture2DArray.MipSlice        = 0;
                        uav.Texture2DArray.FirstArraySlice = 0;
                        uav.Texture2DArray.ArraySize       = 6;
                        uav.Texture2DArray.PlaneSlice      = 0;
                    }
                    else if (view_it->second.is_1d)
                    {
                        uav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE1D;
                        uav.Texture1D.MipSlice = 0;
                    }
                    else
                    {
                        uav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
                        uav.Texture2D.MipSlice = 0;
                        uav.Texture2D.PlaneSlice = 0;
                    }
                    device_->CreateUnorderedAccessView(
                        tex_it->second.resource.Get(), nullptr, &uav, dst);
                    break;
                }
                case cd::rhi::DescriptorType::kStorageBuffer:
                case cd::rhi::DescriptorType::kStorageBufferDynamic:
                {
                    // UAV Buffer (raw / structured). Phase 16.B
                    // ships the raw flavour (one element per 4 bytes);
                    // structured stride needs more descriptor metadata.
                    auto buf_it = buffers_.find(w.buffer.index());
                    if (buf_it == buffers_.end())
                        return std::unexpected(cd::rhi::rhi_errors::make(
                            cd::rhi::rhi_errors::Code::kInvalidArgument,
                            "update_descriptor_set: UAV buffer unknown"));
                    D3D12_UNORDERED_ACCESS_VIEW_DESC uav {};
                    uav.Format = DXGI_FORMAT_R32_TYPELESS;
                    uav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
                    uav.Buffer.FirstElement = w.buffer_offset / 4;
                    UINT64 range = w.buffer_range == 0
                        ? (buf_it->second.size - w.buffer_offset)
                        : w.buffer_range;
                    uav.Buffer.NumElements = static_cast<UINT>(range / 4);
                    uav.Buffer.StructureByteStride = 0;
                    uav.Buffer.CounterOffsetInBytes = 0;
                    uav.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
                    device_->CreateUnorderedAccessView(
                        buf_it->second.resource.Get(), nullptr, &uav, dst);
                    break;
                }
                case cd::rhi::DescriptorType::kCombinedImageSampler:
                {
                    // CombinedImageSampler maps to a Texture2D SRV in the
                    // CBV/SRV/UAV table. The sampler half is served by a static
                    // sampler in the root signature: B1b populates one LINEAR-clamp
                    // D3D12_STATIC_SAMPLER_DESC at the binding's (sM, spaceN) in
                    // create_pipeline_layout (NumStaticSamplers was 0 before B1b, so
                    // the s-register was unbacked). Same SRV path as kSampledImage.
                    auto view_it = texture_views_.find(w.view.index());
                    if (view_it == texture_views_.end())
                        return std::unexpected(cd::rhi::rhi_errors::make(
                            cd::rhi::rhi_errors::Code::kInvalidArgument,
                            "update_descriptor_set: combined SRV view unknown"));
                    auto tex_it = textures_.find(view_it->second.parent.index());
                    if (tex_it == textures_.end())
                        return std::unexpected(cd::rhi::rhi_errors::make(
                            cd::rhi::rhi_errors::Code::kInvalidArgument,
                            "update_descriptor_set: combined SRV texture unknown"));
                    D3D12_SHADER_RESOURCE_VIEW_DESC srv {};
                    srv.Format = view_it->second.format;
                    srv.Shader4ComponentMapping =
                        D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                    // Phase 393/395 — combined sampler + view dimension.
                    if (view_it->second.is_cube)
                    {
                        srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
                        srv.TextureCube.MostDetailedMip = 0;
                        srv.TextureCube.MipLevels = 1;
                        srv.TextureCube.ResourceMinLODClamp = 0.0F;
                    }
                    else if (view_it->second.is_1d)
                    {
                        srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE1D;
                        srv.Texture1D.MostDetailedMip = 0;
                        srv.Texture1D.MipLevels = 1;
                        srv.Texture1D.ResourceMinLODClamp = 0.0F;
                    }
                    else if (view_it->second.is_3d)
                    {
                        srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D;
                        srv.Texture3D.MostDetailedMip = 0;
                        srv.Texture3D.MipLevels = 1;
                        srv.Texture3D.ResourceMinLODClamp = 0.0F;
                    }
                    else if (view_it->second.is_ms)
                    {
                        // D-SRV-MS: combined sampler over an MSAA texture.
                        srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DMS;
                    }
                    else
                    {
                        srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
                        srv.Texture2D.MipLevels = 1;
                    }
                    device_->CreateShaderResourceView(
                        tex_it->second.resource.Get(), &srv, dst);
                    break;
                }
                case cd::rhi::DescriptorType::kSampler:
                {
                    // D2 (parity with VulkanDevice kSampler — it writes the
                    // real VkDescriptorImageInfo.sampler into the set). Copy
                    // the SamplerRecord's staged descriptor into THIS set's
                    // CPU sampler-staging slot. bind_descriptor_set later
                    // copies the staged range into the shader-visible sampler
                    // ring and points the SAMPLER root table at it — so the
                    // dynamically-created sampler actually drives sampling
                    // instead of being silently ignored (the previous no-op
                    // resolved everything to a baked LINEAR-clamp default).
                    auto samp_it = samplers_.find(w.sampler.index());
                    if (samp_it == samplers_.end())
                        return std::unexpected(cd::rhi::rhi_errors::make(
                            cd::rhi::rhi_errors::Code::kInvalidArgument,
                            "update_descriptor_set: kSampler write references unknown sampler"));
                    if (set.sampler_count == 0 || sampler_set_cpu_heap_ == nullptr)
                        return std::unexpected(cd::rhi::rhi_errors::make(
                            cd::rhi::rhi_errors::Code::kInvalidArgument,
                            "update_descriptor_set: set has no sampler staging slot "
                            "(layout declared no kSampler binding)"));
                    D3D12_CPU_DESCRIPTOR_HANDLE samp_dst =
                        sampler_set_cpu_heap_->GetCPUDescriptorHandleForHeapStart();
                    samp_dst.ptr +=
                        static_cast<SIZE_T>(set.sampler_cpu_offset + sampler_offset +
                                            w.array_element) *
                        sampler_heap_increment_;
                    device_->CopyDescriptorsSimple(
                        1, samp_dst, samp_it->second.cpu_handle,
                        D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
                    break;
                }
                case cd::rhi::DescriptorType::kInputAttachment:
                {
                    // Phase 129 — D3D12 doesn't have a distinct "input
                    // attachment" concept. Vulkan input attachments are
                    // colour/depth render targets read back as textures
                    // inside the same render pass. On D3D12 the same
                    // image is bound twice (as an RTV in the OM and as
                    // an SRV in the root table); a developer writing
                    // cross-backend code can declare the same handle
                    // as kInputAttachment, and we route it down the
                    // SRV path so the descriptor is filled with a
                    // TEXTURE2D SRV.
                    auto view_it = texture_views_.find(w.view.index());
                    if (view_it == texture_views_.end())
                        return std::unexpected(cd::rhi::rhi_errors::make(
                            cd::rhi::rhi_errors::Code::kInvalidArgument,
                            "update_descriptor_set: input-attachment view unknown"));
                    auto tex_it = textures_.find(view_it->second.parent.index());
                    if (tex_it == textures_.end())
                        return std::unexpected(cd::rhi::rhi_errors::make(
                            cd::rhi::rhi_errors::Code::kInvalidArgument,
                            "update_descriptor_set: input-attachment texture unknown"));
                    D3D12_SHADER_RESOURCE_VIEW_DESC srv {};
                    srv.Format = view_it->second.format;
                    srv.Shader4ComponentMapping =
                        D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                    if (view_it->second.is_ms)
                    {
                        // D-SRV-MS: an input attachment fed by an MSAA render
                        // target is read back as Texture2DMS.
                        srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DMS;
                    }
                    else
                    {
                        srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
                        srv.Texture2D.MipLevels = 1;
                    }
                    device_->CreateShaderResourceView(
                        tex_it->second.resource.Get(), &srv, dst);
                    break;
                }
                case cd::rhi::DescriptorType::kAccelerationStructure:
                {
                    // Phase 396 — DXR acceleration structure SRV.
                    // In D3D12 an AS is exposed as an SRV with
                    // RaytracingAccelerationStructure format. The GPU
                    // virtual address is stored in the AccelRecord; we look
                    // it up via the DEDICATED `accel` field of the write —
                    // the documented contract (DescriptorWrite::accel) and
                    // the field every real caller fills (hello_path_trace,
                    // ddgi::DispatchPass) and the Vulkan backend reads
                    // (w.accel). The legacy `buffer`-packed lookup is kept
                    // as a fallback so any caller that packed the AS into
                    // `buffer` still resolves.
                    //
                    // If the AS is not found on this adapter, or DXR
                    // is unavailable, we silently skip the write so
                    // the non-DXR path doesn't crash on construction.
                    auto acc_it = accels_.find(w.accel.index());
                    if (acc_it == accels_.end())
                        acc_it = accels_.find(w.buffer.index());
                    if (acc_it != accels_.end())
                    {
                        D3D12_SHADER_RESOURCE_VIEW_DESC asrv {};
                        asrv.Format = DXGI_FORMAT_UNKNOWN;
                        asrv.ViewDimension =
                            D3D12_SRV_DIMENSION_RAYTRACING_ACCELERATION_STRUCTURE;
                        asrv.Shader4ComponentMapping =
                            D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                        asrv.RaytracingAccelerationStructure.Location =
                            acc_it->second.result_gva;
                        // CreateShaderResourceView for an AS requires
                        // pResource == nullptr; the GPU VA is in the desc.
                        device_->CreateShaderResourceView(nullptr, &asrv, dst);
                    }
                    break;
                }
                case cd::rhi::DescriptorType::kBindlessSampledImage:
                {
                    // X4-E1 — runtime-indexed sampler2D array slot written via
                    // the GENERIC descriptor-set surface (update_descriptor_set).
                    // The slot is an SRV in the per-set CBV/SRV/UAV heap; the
                    // sampler half is served by the bindless SAMPLER descriptor
                    // table (B1b — see create_pipeline_layout; a bindless
                    // sampler2D[] emits a SamplerState range, which a static
                    // sampler cannot satisfy). `array_element` already offset
                    // `dst` into the table at the top of the loop, so we write
                    // exactly one slot here.
                    //
                    // NOTE: this is the LEGACY single-set bindless path. The
                    // DEDICATED bindless pool (D10 — create_bindless_texture_array
                    // / write_bindless_texture_slot, which now back the engine's
                    // set-1 `register(t0, space1)[i]` array and advertise
                    // features().bindless_resources on Tier 2+) writes into its own
                    // persistent shader-visible heap, NOT this per-set ring. This
                    // case is retained for callers that route a bindless binding
                    // through the generic DescriptorWrite surface (parity with the
                    // Vulkan descriptor path; no silent kNotImplemented fall-through).
                    auto view_it = texture_views_.find(w.view.index());
                    if (view_it == texture_views_.end())
                        return std::unexpected(cd::rhi::rhi_errors::make(
                            cd::rhi::rhi_errors::Code::kInvalidArgument,
                            "update_descriptor_set: bindless slot view unknown"));
                    auto tex_it = textures_.find(view_it->second.parent.index());
                    if (tex_it == textures_.end())
                        return std::unexpected(cd::rhi::rhi_errors::make(
                            cd::rhi::rhi_errors::Code::kInvalidArgument,
                            "update_descriptor_set: bindless slot texture unknown"));
                    D3D12_SHADER_RESOURCE_VIEW_DESC srv {};
                    srv.Format = view_it->second.format;
                    srv.Shader4ComponentMapping =
                        D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                    if (view_it->second.is_cube)
                    {
                        srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
                        srv.TextureCube.MostDetailedMip = 0;
                        srv.TextureCube.MipLevels = 1;
                        srv.TextureCube.ResourceMinLODClamp = 0.0F;
                    }
                    else if (view_it->second.is_1d)
                    {
                        srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE1D;
                        srv.Texture1D.MostDetailedMip = 0;
                        srv.Texture1D.MipLevels = 1;
                        srv.Texture1D.ResourceMinLODClamp = 0.0F;
                    }
                    else if (view_it->second.is_3d)
                    {
                        srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D;
                        srv.Texture3D.MostDetailedMip = 0;
                        srv.Texture3D.MipLevels = 1;
                        srv.Texture3D.ResourceMinLODClamp = 0.0F;
                    }
                    else if (view_it->second.is_ms)
                    {
                        // D-SRV-MS: bindless slot over an MSAA texture.
                        srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DMS;
                    }
                    else
                    {
                        srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
                        srv.Texture2D.MipLevels = 1;
                    }
                    device_->CreateShaderResourceView(
                        tex_it->second.resource.Get(), &srv, dst);
                    break;
                }
                default:
                    return std::unexpected(cd::rhi::rhi_errors::make(
                        cd::rhi::rhi_errors::Code::kNotImplemented,
                        "update_descriptor_set: unhandled DescriptorType value"));
            }
        }
        return {};
    }

    // ---- Bindless texture array (D10 — parity with Vulkan) ----------------
    //
    // The engine's dedicated bindless set (set 1 = `register(t0, space1)[i]`,
    // MEMORY rule 9) is a runtime-indexed SRV array. The Vulkan reference
    // backs it with a dedicated UPDATE_AFTER_BIND | PARTIALLY_BOUND |
    // VARIABLE_DESCRIPTOR_COUNT descriptor set whose slots are written by
    // `write_bindless_texture_slot` (VulkanDevice.cpp:4017-4188). The D3D12
    // analog is the PERSISTENT bindless sub-region [kGpuHeapCap, kUnifiedHeapCap)
    // of the ONE shader-visible CBV/SRV/UAV `unified_heap_` (phase1190 B6
    // follow-up — see the unified_heap_ rationale at the member declaration).
    // The persistent contiguous slot range is the descriptor table the space1
    // unbounded SRV range resolves against:
    //
    //   create_bindless_texture_array(desc) -> reserve `desc.slot_count`
    //       contiguous descriptors in the bindless sub-region (bump allocator);
    //       the reserved base is the table base for `register(t0, space1)`.
    //   write_bindless_texture_slot(array, slot, view) -> CreateShaderResourceView
    //       for the view's parent texture directly into `unified_heap_` at
    //       (kGpuHeapCap + base + slot). Because the heap is shader-visible the
    //       SRV is live the moment it is written (no CPU->GPU copy step, unlike
    //       the per-set ring at `copy_set_to_gpu_heap`); this mirrors Vulkan's
    //       UPDATE_AFTER_BIND immediacy.
    //
    // The command path (bind_bindless_texture_array) binds `unified_heap_` +
    // the bindless sampler heap via SetDescriptorHeaps and points the set-1 SRV
    // root-table at the array's GPU base and the sampler root-table at the
    // sampler heap. Because the ring and the bindless pool share `unified_heap_`,
    // a draw can co-bind a classic set-0 table AND the bindless set-1 array
    // without either SetDescriptorHeaps call unbinding the other (the CBV/SRV/UAV
    // heap is identical; only the sampler heap is a separate, co-bindable type).
    // The sampler half is a real SAMPLER descriptor table (B1b — see
    // create_pipeline_layout): a bindless sampler2D[] emits a SamplerState range
    // that a static sampler cannot satisfy.

    [[nodiscard]] cd::core::Result<cd::rhi::BindlessTextureArrayHandle>
    create_bindless_texture_array(
        const cd::rhi::BindlessTextureArrayDesc& desc) override
    {
        if (!features_.bindless_resources)
        {
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kNotImplemented,
                "create_bindless_texture_array: device resource-binding tier < 2 "
                "(no unbounded shader-visible SRV tables)"));
        }
        if (desc.slot_count == 0)
        {
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kInvalidArgument,
                "create_bindless_texture_array: slot_count must be > 0"));
        }
        // The sampler half is served by the bindless SAMPLER descriptor table
        // (filled with the default LINEAR-clamp sampler), not a per-array sampler
        // descriptor; desc.sampler is accepted for Vulkan API parity but not
        // dereferenced here (a stale handle is not an error on D3D12). We still
        // reject slot_count overflow of the pool.
        if (auto r = ensure_bindless_heap_(); !r.has_value())
            return std::unexpected(r.error());
        if (bindless_cursor_ + desc.slot_count > kBindlessHeapCap)
        {
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kResourceCreationFailed,
                "create_bindless_texture_array: bindless heap exhausted"));
        }
        BindlessArrayRecord rec;
        rec.base_slot  = bindless_cursor_;
        rec.slot_count = desc.slot_count;
        bindless_cursor_ += desc.slot_count;
        const auto id = next_id_++;
        bindless_arrays_.emplace(id, rec);
        return cd::rhi::BindlessTextureArrayHandle { id, 1u };
    }

    [[nodiscard]] cd::core::Result<void>
    write_bindless_texture_slot(cd::rhi::BindlessTextureArrayHandle array,
                                std::uint32_t                       slot,
                                cd::rhi::TextureViewHandle          view) override
    {
        auto arr_it = bindless_arrays_.find(array.index());
        if (arr_it == bindless_arrays_.end())
        {
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kInvalidArgument,
                "write_bindless_texture_slot: unknown bindless array handle"));
        }
        if (slot >= arr_it->second.slot_count)
        {
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kInvalidArgument,
                "write_bindless_texture_slot: slot out of range"));
        }
        auto view_it = texture_views_.find(view.index());
        if (view_it == texture_views_.end())
        {
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kInvalidArgument,
                "write_bindless_texture_slot: unknown texture view"));
        }
        auto tex_it = textures_.find(view_it->second.parent.index());
        if (tex_it == textures_.end())
        {
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kInvalidArgument,
                "write_bindless_texture_slot: bindless slot texture unknown"));
        }
        D3D12_SHADER_RESOURCE_VIEW_DESC srv {};
        srv.Format = view_it->second.format;
        srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        if (view_it->second.is_cube)
        {
            srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
            srv.TextureCube.MostDetailedMip = 0;
            srv.TextureCube.MipLevels = 1;
            srv.TextureCube.ResourceMinLODClamp = 0.0F;
        }
        else if (view_it->second.is_3d)
        {
            srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D;
            srv.Texture3D.MostDetailedMip = 0;
            srv.Texture3D.MipLevels = 1;
            srv.Texture3D.ResourceMinLODClamp = 0.0F;
        }
        else
        {
            srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            srv.Texture2D.MipLevels = 1;
        }
        // Write directly into the bindless sub-region of the shader-visible
        // unified heap at (kGpuHeapCap + array.base + slot). The kGpuHeapCap
        // region offset keeps the SRV out of the [0, kGpuHeapCap) per-set ring.
        D3D12_CPU_DESCRIPTOR_HANDLE dst =
            unified_heap_->GetCPUDescriptorHandleForHeapStart();
        dst.ptr += static_cast<SIZE_T>(kGpuHeapCap + arr_it->second.base_slot + slot) *
                   bindless_increment_;
        device_->CreateShaderResourceView(tex_it->second.resource.Get(), &srv, dst);
        return {};
    }

    void destroy_bindless_texture_array(cd::rhi::BindlessTextureArrayHandle h) override
    {
        // Slots are not reclaimed (bump allocator), matching the per-set heap
        // policy; the persistent heap lives for the device's lifetime.
        bindless_arrays_.erase(h.index());
    }

    // ---- Synchronization (REAL — Phase 125 v0.99.52) ----------------------
    //
    // D3D12 doesn't have binary semaphores in the Vulkan sense; the
    // queue-side sync primitive is `ID3D12Fence` (which is essentially
    // a Vulkan timeline semaphore). We map:
    //
    //   * `SemaphoreHandle`  — `ID3D12Fence` + a per-instance value
    //                          that bumps to "1" on each signal.
    //                          acquire_next_image / present accept the
    //                          handle for interface parity and ignore
    //                          it (DXGI handles its own backbuffer
    //                          fencing).
    //   * `FenceHandle`      — `ID3D12Fence` + a Win32 event the host
    //                          waits on (`SetEventOnCompletion`).
    //                          `signaled` boot state matches Vulkan's
    //                          `VkFenceCreateFlagBits::VK_FENCE_CREATE_SIGNALED_BIT`.
    //   * `TimelineSemaphoreHandle` — `ID3D12Fence` exposed natively
    //                          since fences already carry a u64 counter.
    //
    // All three share an ID3D12Fence underneath; the wrapper types
    // exist for ABI parity with Vulkan code paths.

    [[nodiscard]] cd::core::Result<cd::rhi::SemaphoreHandle>
    create_semaphore() override
    {
        ComPtr<ID3D12Fence> fence;
        HRESULT hr = device_->CreateFence(0, D3D12_FENCE_FLAG_NONE,
                                          IID_PPV_ARGS(&fence));
        if (FAILED(hr))
        {
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kResourceCreationFailed,
                "CreateFence(semaphore) failed"));
        }
        const auto id = next_id_++;
        SemaphoreRecord rec;
        rec.fence = fence;
        rec.value = 0;
        semaphores_.emplace(id, std::move(rec));
        return cd::rhi::SemaphoreHandle { id, 1u };
    }
    void destroy_semaphore(cd::rhi::SemaphoreHandle h) override
    {
        semaphores_.erase(h.index());
    }

    [[nodiscard]] cd::core::Result<cd::rhi::FenceHandle>
    create_fence(bool signaled) override
    {
        ComPtr<ID3D12Fence> fence;
        const UINT64 initial = signaled ? 1u : 0u;
        HRESULT hr = device_->CreateFence(initial, D3D12_FENCE_FLAG_NONE,
                                          IID_PPV_ARGS(&fence));
        if (FAILED(hr))
        {
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kResourceCreationFailed,
                "CreateFence(fence) failed"));
        }
        HANDLE evt = ::CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (evt == nullptr)
        {
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kResourceCreationFailed,
                "CreateEvent(fence) failed"));
        }
        const auto id = next_id_++;
        FenceRecord rec;
        rec.fence = fence;
        rec.event = evt;
        rec.target_value = initial;  // matches the value the fence was created at
        fences_.emplace(id, std::move(rec));
        return cd::rhi::FenceHandle { id, 1u };
    }
    void destroy_fence(cd::rhi::FenceHandle h) override
    {
        auto it = fences_.find(h.index());
        if (it == fences_.end()) return;
        if (it->second.event != nullptr) ::CloseHandle(it->second.event);
        fences_.erase(it);
    }

    [[nodiscard]] cd::core::Result<void>
    wait_for_fence(cd::rhi::FenceHandle h, std::uint64_t timeout_ns) override
    {
        auto it = fences_.find(h.index());
        if (it == fences_.end())
        {
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kInvalidArgument,
                "wait_for_fence: unknown fence handle"));
        }
        auto& rec = it->second;
        // Fast path: already at or above target.
        if (rec.fence->GetCompletedValue() >= rec.target_value) return {};

        if (FAILED(rec.fence->SetEventOnCompletion(rec.target_value, rec.event)))
        {
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kDeviceLost,
                "SetEventOnCompletion failed"));
        }
        // ns → ms with rounding-up; INFINITE on a 0-tag timeout
        // (matching Vulkan's UINT64_MAX semantic).
        const DWORD ms = (timeout_ns == 0u || timeout_ns == ~std::uint64_t { 0 })
            ? INFINITE
            : static_cast<DWORD>((timeout_ns + 999'999u) / 1'000'000u);
        const DWORD wr = ::WaitForSingleObject(rec.event, ms);
        if (wr == WAIT_TIMEOUT)
        {
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kTimeout,
                "wait_for_fence: timed out"));
        }
        if (wr != WAIT_OBJECT_0)
        {
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kDeviceLost,
                "wait_for_fence: WaitForSingleObject failed"));
        }
        return {};
    }

    void reset_fence(cd::rhi::FenceHandle h) override
    {
        auto it = fences_.find(h.index());
        if (it == fences_.end()) return;
        // D3D12 fences are monotonic counters — "reset" means
        // setting the target value forward to the next signal we
        // expect. The caller is responsible for queueing the next
        // signal via command-queue Signal afterwards.
        ++it->second.target_value;
    }

    [[nodiscard]] bool is_fence_signaled(cd::rhi::FenceHandle h) override
    {
        auto it = fences_.find(h.index());
        if (it == fences_.end()) return false;
        return it->second.fence->GetCompletedValue() >= it->second.target_value;
    }

    [[nodiscard]] cd::core::Result<cd::rhi::TimelineSemaphoreHandle>
    create_timeline_semaphore(std::uint64_t initial_value) override
    {
        ComPtr<ID3D12Fence> fence;
        HRESULT hr = device_->CreateFence(initial_value, D3D12_FENCE_FLAG_NONE,
                                          IID_PPV_ARGS(&fence));
        if (FAILED(hr))
        {
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kResourceCreationFailed,
                "CreateFence(timeline) failed"));
        }
        HANDLE evt = ::CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (evt == nullptr)
        {
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kResourceCreationFailed,
                "CreateEvent(timeline) failed"));
        }
        const auto id = next_id_++;
        TimelineRecord rec;
        rec.fence = fence;
        rec.event = evt;
        timelines_.emplace(id, std::move(rec));
        return cd::rhi::TimelineSemaphoreHandle { id, 1u };
    }
    void destroy_timeline_semaphore(cd::rhi::TimelineSemaphoreHandle h) override
    {
        auto it = timelines_.find(h.index());
        if (it == timelines_.end()) return;
        if (it->second.event != nullptr) ::CloseHandle(it->second.event);
        timelines_.erase(it);
    }

    [[nodiscard]] cd::core::Result<void>
    wait_timeline_semaphore(cd::rhi::TimelineSemaphoreHandle h,
                            std::uint64_t value,
                            std::uint64_t timeout_ns) override
    {
        auto it = timelines_.find(h.index());
        if (it == timelines_.end())
        {
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kInvalidArgument,
                "wait_timeline_semaphore: unknown handle"));
        }
        auto& rec = it->second;
        if (rec.fence->GetCompletedValue() >= value) return {};
        if (FAILED(rec.fence->SetEventOnCompletion(value, rec.event)))
        {
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kDeviceLost,
                "SetEventOnCompletion(timeline) failed"));
        }
        const DWORD ms = (timeout_ns == 0u || timeout_ns == ~std::uint64_t { 0 })
            ? INFINITE
            : static_cast<DWORD>((timeout_ns + 999'999u) / 1'000'000u);
        const DWORD wr = ::WaitForSingleObject(rec.event, ms);
        if (wr == WAIT_TIMEOUT)
        {
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kTimeout,
                "wait_timeline_semaphore: timed out"));
        }
        if (wr != WAIT_OBJECT_0)
        {
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kDeviceLost,
                "WaitForSingleObject(timeline) failed"));
        }
        return {};
    }

    [[nodiscard]] cd::core::Result<void>
    signal_timeline_semaphore(cd::rhi::TimelineSemaphoreHandle h, std::uint64_t value) override
    {
        auto it = timelines_.find(h.index());
        if (it == timelines_.end())
        {
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kInvalidArgument,
                "signal_timeline_semaphore: unknown handle"));
        }
        if (FAILED(it->second.fence->Signal(value)))
        {
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kDeviceLost,
                "Signal(timeline) failed"));
        }
        return {};
    }

    [[nodiscard]] std::uint64_t timeline_semaphore_value(cd::rhi::TimelineSemaphoreHandle h) const override
    {
        auto it = timelines_.find(h.index());
        if (it == timelines_.end()) return 0;
        return it->second.fence->GetCompletedValue();
    }

    // ---- acquire / present (REAL — Phase 13.C v0.32.0) --------------------

    [[nodiscard]] cd::core::Result<std::uint32_t>
    acquire_next_image(cd::rhi::SwapchainHandle h, cd::rhi::SemaphoreHandle, cd::rhi::FenceHandle, std::uint64_t) override
    {
        auto it = swapchains_.find(h.index());
        if (it == swapchains_.end())
        {
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kInvalidArgument, "acquire_next_image: unknown swapchain"));
        }
        // DXGI flip-model: GetCurrentBackBufferIndex returns the index
        // the *next* Present will publish. No semaphore signaling — D3D12
        // synchronization is driven by command-queue fences, not by
        // Vulkan-style binary semaphores. The semaphore/fence parameters
        // are accepted for interface parity and ignored.
        return it->second.swap->GetCurrentBackBufferIndex();
    }

    [[nodiscard]] cd::core::Result<void>
    present(cd::rhi::SwapchainHandle h, std::uint32_t, std::span<const cd::rhi::SemaphoreHandle>) override
    {
        auto it = swapchains_.find(h.index());
        if (it == swapchains_.end())
        {
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kInvalidArgument, "present: unknown swapchain"));
        }
        const UINT sync_interval = it->second.vsync ? 1u : 0u;
        const HRESULT hr = it->second.swap->Present(sync_interval, 0);

        // D-SWAPCHAIN-RESIZE (Backend-to-100 Wave 1): the historical path
        // mapped EVERY non-S_OK Present result to kDeviceLost, so a window
        // resize / occlusion never produced the kSwapchainOutOfDate contract
        // the engine consumes (hello_triangle / hello_engine recreate the
        // swapchain on that code) — on D3D12 a resize silently looked like a
        // lost device. Mirror the Vulkan kSwapchainOutOfDate path
        // (VulkanDevice.cpp:2840, VK_ERROR_OUT_OF_DATE / VK_SUBOPTIMAL):
        //
        //   * DXGI_STATUS_OCCLUDED — a SUCCEEDED status (window minimised /
        //     covered): not an error; signal out-of-date so the caller backs
        //     off + retries (matches VK_SUBOPTIMAL semantics).
        //   * DXGI_ERROR_DEVICE_REMOVED / _RESET — a genuinely lost device.
        //   * Any other failure where the back-buffer extent no longer
        //     matches the window — the engine must recreate; report
        //     out-of-date rather than a fatal device-lost.
        if (hr == DXGI_STATUS_OCCLUDED)
        {
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kSwapchainOutOfDate,
                "present: swapchain occluded (out of date)"));
        }
        if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET)
        {
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kDeviceLost,
                "present: device removed/reset"));
        }
        if (FAILED(hr))
        {
            // A non-removal failure on Present is almost always an
            // out-of-date back-buffer (extent/format drift after a resize the
            // app didn't yet service). Treat it as recoverable
            // out-of-date — the caller recreates — instead of a fatal
            // device-lost.
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kSwapchainOutOfDate,
                "present: swapchain out of date (recreate required)"));
        }
        return {};
    }

    // D-SWAPCHAIN-RESIZE: in-place back-buffer resize via ResizeBuffers. The
    // engine's recreate-on-kSwapchainOutOfDate normally destroys + recreates
    // the whole swapchain, but a flip-model DXGI chain supports an in-place
    // ResizeBuffers that re-allocates the back-buffers at the new extent
    // WITHOUT tearing down the IDXGISwapChain3 (cheaper; preserves the
    // colour-space + present queue binding). This IDevice::resize_swapchain
    // override is the D3D12 implementation: it releases the engine-side
    // back-buffer Texture/View
    // records (they alias the soon-to-be-freed ID3D12Resources), calls
    // ResizeBuffers(0 = keep count, new w/h, UNKNOWN = keep format, keep
    // flags), then re-registers the new back-buffers + RTVs. Returns
    // kSwapchainOutOfDate (not kDeviceLost) if the resize is rejected so the
    // caller can fall back to full recreate. Width/height 0 = no-op refresh of
    // the current extent (the canonical "validate the chain is healthy" call).
    [[nodiscard]] cd::core::Result<void>
    resize_swapchain(cd::rhi::SwapchainHandle h,
                     std::uint32_t new_width,
                     std::uint32_t new_height) override
    {
        auto it = swapchains_.find(h.index());
        if (it == swapchains_.end())
        {
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kInvalidArgument,
                "resize_swapchain_buffers: unknown swapchain"));
        }
        SwapchainRecord& rec = it->second;

        // The back-buffers must be fully idle before ResizeBuffers — the
        // engine fences the queue before calling this; we also wait_idle to
        // be safe against the one-shot test path.
        (void)wait_idle_internal();

        // Drop the engine-side records that ALIAS the back-buffer resources
        // (ResizeBuffers frees the underlying ID3D12Resources; a dangling
        // ComPtr in textures_ would keep a stale resource alive and the RTVs
        // would point at freed memory).
        for (auto th : rec.image_handles)
            textures_.erase(th.index());
        for (auto vh : rec.image_view_handles)
            texture_views_.erase(vh.index());
        rec.images.clear();

        const UINT w = (new_width  == 0u) ? rec.extent.width  : new_width;
        const UINT hgt = (new_height == 0u) ? rec.extent.height : new_height;
        const HRESULT hr = rec.swap->ResizeBuffers(
            0,                       // keep current back-buffer count
            w, hgt,
            DXGI_FORMAT_UNKNOWN,     // keep current format
            0);                      // keep current flags
        if (FAILED(hr))
        {
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kSwapchainOutOfDate,
                "resize_swapchain_buffers: ResizeBuffers rejected — full "
                "recreate required"));
        }
        rec.extent = { w, hgt };

        // Re-register the freshly-allocated back-buffers + RTVs into the same
        // RTV heap (slot order preserved) and re-fill the handle vectors.
        const auto count = static_cast<std::uint32_t>(rec.image_handles.size());
        rec.images.resize(count);
        D3D12_CPU_DESCRIPTOR_HANDLE rtv_cpu =
            rec.rtv_heap->GetCPUDescriptorHandleForHeapStart();
        for (std::uint32_t i = 0; i < count; ++i)
        {
            ComPtr<ID3D12Resource> back;
            if (FAILED(rec.swap->GetBuffer(i, IID_PPV_ARGS(&back))))
            {
                return std::unexpected(cd::rhi::rhi_errors::make(
                    cd::rhi::rhi_errors::Code::kSwapchainOutOfDate,
                    "resize_swapchain_buffers: GetBuffer failed post-resize"));
            }
            rec.images[i] = back;

            D3D12_RENDER_TARGET_VIEW_DESC rtv_desc {};
            rtv_desc.Format = rec.format;
            rtv_desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
            device_->CreateRenderTargetView(back.Get(), &rtv_desc, rtv_cpu);

            const auto tex_id = next_id_++;
            TextureRecord trec;
            trec.resource = back;
            trec.format = rec.format;
            trec.extent = { w, hgt, 1 };
            trec.usage = cd::rhi::TextureUsage::kColorAttachment;
            trec.is_swapchain_image = true;
            trec.rtv_cpu = rtv_cpu;
            textures_.emplace(tex_id, std::move(trec));
            rec.image_handles[i] = cd::rhi::TextureHandle { tex_id, 1u };

            const auto view_id = next_id_++;
            TextureViewRecord vrec;
            vrec.parent = rec.image_handles[i];
            vrec.format = rec.format;
            vrec.rtv_cpu = rtv_cpu;
            texture_views_.emplace(view_id, vrec);
            rec.image_view_handles[i] = cd::rhi::TextureViewHandle { view_id, 1u };

            rtv_cpu.ptr += rec.rtv_descriptor_size;
        }
        return {};
    }

    // ---- Swapchain (REAL — Phase 13.C v0.32.0) ----------------------------

    [[nodiscard]] cd::core::Result<cd::rhi::SwapchainHandle>
    create_swapchain(const cd::rhi::SwapchainDesc& desc) override
    {
        if (desc.window_handle == nullptr)
        {
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kInvalidArgument,
                "SwapchainDesc.window_handle is null"));
        }
        const DXGI_FORMAT fmt = to_dxgi_format(desc.format);
        if (fmt == DXGI_FORMAT_UNKNOWN)
        {
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kInvalidArgument,
                "swapchain format unsupported"));
        }

        // DXGI flip-model can't be used with SRGB back-buffer formats —
        // create the swapchain with the linear sibling and let the
        // rendering layer pick an SRGB RTV. The caller's intent is
        // preserved by tracking `desc.format` in the SwapchainRecord
        // so RTV creation can honor it.
        DXGI_FORMAT swap_fmt = fmt;
        if (swap_fmt == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB)
            swap_fmt = DXGI_FORMAT_R8G8B8A8_UNORM;
        else if (swap_fmt == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB)
            swap_fmt = DXGI_FORMAT_B8G8R8A8_UNORM;

        DXGI_SWAP_CHAIN_DESC1 sd {};
        sd.Width = desc.extent.width;
        sd.Height = desc.extent.height;
        sd.Format = swap_fmt;
        sd.Stereo = FALSE;
        sd.SampleDesc.Count = 1;
        sd.SampleDesc.Quality = 0;
        sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        sd.BufferCount = desc.image_count;
        sd.Scaling = DXGI_SCALING_STRETCH;
        sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        sd.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
        sd.Flags = 0;

        ComPtr<IDXGISwapChain1> chain1;
        HRESULT hr = factory_->CreateSwapChainForHwnd(
            graphics_queue_.Get(),
            static_cast<HWND>(desc.window_handle),
            &sd, nullptr, nullptr, &chain1);
        if (FAILED(hr))
        {
            char msg[160] {};
            std::snprintf(msg, sizeof(msg),
                          "CreateSwapChainForHwnd failed: HRESULT 0x%08lx",
                          static_cast<unsigned long>(hr));
            return std::unexpected(cd::rhi::rhi_errors::make_owning(
                cd::rhi::rhi_errors::Code::kResourceCreationFailed,
                std::string { msg }));
        }
        ComPtr<IDXGISwapChain3> chain3;
        hr = chain1.As(&chain3);
        if (FAILED(hr))
        {
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kResourceCreationFailed,
                "IDXGISwapChain3 QueryInterface failed"));
        }

        // D-HDR-SWAPCHAIN (Backend-to-100 Wave 1): honor desc.colour_space.
        // The historical path created the chain and NEVER read colour_space
        // nor called SetColorSpace1 — an HDR10 / scRGB request was silently
        // downgraded to SDR (the IDXGISwapChain3 was already in hand but
        // unused for colour management). The Vulkan reference selects the
        // VkColorSpaceKHR at surface-format pick time; mirror that by mapping
        // the engine ColorSpace → DXGI_COLOR_SPACE_TYPE, probing
        // CheckColorSpaceSupport, and applying SetColorSpace1 when the surface
        // supports it. scRGB linear requires an FP16 back-buffer; an SDR-only
        // panel reports no support and we leave the default sRGB space (the
        // documented fall-back-to-kSrgbNonlinear contract), never failing the
        // swapchain create over a display capability.
        const DXGI_COLOR_SPACE_TYPE requested_cs =
            to_dxgi_color_space(desc.colour_space);
        UINT cs_support = 0;
        if (SUCCEEDED(chain3->CheckColorSpaceSupport(requested_cs, &cs_support)) &&
            (cs_support & DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_PRESENT) != 0)
        {
            // Ignore the HRESULT of the apply: a driver that advertised
            // support but transiently rejects the call leaves the chain in
            // its (valid) default space rather than aborting creation.
            (void)chain3->SetColorSpace1(requested_cs);
        }

        SwapchainRecord rec;
        rec.swap = chain3;
        rec.format = fmt;
        rec.swap_format = swap_fmt;
        rec.extent = desc.extent;
        rec.vsync = desc.vsync;
        rec.images.resize(desc.image_count);
        rec.image_handles.resize(desc.image_count);
        rec.image_view_handles.resize(desc.image_count);

        // Build a single CPU-visible RTV heap to expose RTV CPU descriptors
        // for the back-buffers. The dxgi sample backbuffer images become
        // engine-side TextureHandles + TextureViewHandles so consumer code
        // can hand them to the trivial clear command-list path.
        D3D12_DESCRIPTOR_HEAP_DESC rtv_heap_desc {};
        rtv_heap_desc.NumDescriptors = desc.image_count;
        rtv_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        rtv_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        hr = device_->CreateDescriptorHeap(&rtv_heap_desc, IID_PPV_ARGS(&rec.rtv_heap));
        if (FAILED(hr))
        {
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kResourceCreationFailed,
                "RTV descriptor heap creation failed"));
        }
        rec.rtv_descriptor_size =
            device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

        D3D12_CPU_DESCRIPTOR_HANDLE rtv_cpu = rec.rtv_heap->GetCPUDescriptorHandleForHeapStart();
        for (std::uint32_t i = 0; i < desc.image_count; ++i)
        {
            ComPtr<ID3D12Resource> back;
            hr = chain3->GetBuffer(i, IID_PPV_ARGS(&back));
            if (FAILED(hr))
            {
                return std::unexpected(cd::rhi::rhi_errors::make(
                    cd::rhi::rhi_errors::Code::kResourceCreationFailed,
                    "IDXGISwapChain3::GetBuffer failed"));
            }
            rec.images[i] = back;

            // The RTV format is the user-requested format (potentially
            // SRGB); the underlying back-buffer is the linear sibling.
            // D3D12 supports this mismatch via DXGI_FORMAT_*_SRGB RTVs
            // over UNORM resources for flip-model swapchains.
            D3D12_RENDER_TARGET_VIEW_DESC rtv_desc {};
            rtv_desc.Format = fmt;
            rtv_desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
            device_->CreateRenderTargetView(back.Get(), &rtv_desc, rtv_cpu);

            // Register the back-buffer as an engine TextureHandle so
            // sample code can hand it to the command-buffer clear path.
            const auto tex_id = next_id_++;
            TextureRecord trec;
            trec.resource = back;
            trec.format = fmt;
            trec.extent = { desc.extent.width, desc.extent.height, 1 };
            trec.usage = cd::rhi::TextureUsage::kColorAttachment;
            trec.is_swapchain_image = true;
            trec.rtv_cpu = rtv_cpu;
            textures_.emplace(tex_id, std::move(trec));
            rec.image_handles[i] = cd::rhi::TextureHandle { tex_id, 1u };

            const auto view_id = next_id_++;
            TextureViewRecord vrec;
            vrec.parent = rec.image_handles[i];
            vrec.format = fmt;
            vrec.rtv_cpu = rtv_cpu;
            texture_views_.emplace(view_id, vrec);
            rec.image_view_handles[i] = cd::rhi::TextureViewHandle { view_id, 1u };

            rtv_cpu.ptr += rec.rtv_descriptor_size;
        }

        const auto id = next_id_++;
        swapchains_.emplace(id, std::move(rec));
        return cd::rhi::SwapchainHandle { id, 1u };
    }

    void destroy_swapchain(cd::rhi::SwapchainHandle h) override
    {
        auto it = swapchains_.find(h.index());
        if (it == swapchains_.end())
            return;
        for (auto th : it->second.image_handles)
            textures_.erase(th.index());
        for (auto vh : it->second.image_view_handles)
            texture_views_.erase(vh.index());
        swapchains_.erase(it);
    }

    [[nodiscard]] cd::rhi::TextureViewHandle swapchain_image_view(cd::rhi::SwapchainHandle h, std::uint32_t i) const override
    {
        auto it = swapchains_.find(h.index());
        if (it == swapchains_.end() || i >= it->second.image_view_handles.size())
            return {};
        return it->second.image_view_handles[i];
    }
    [[nodiscard]] std::uint32_t swapchain_image_count(cd::rhi::SwapchainHandle h) const override
    {
        auto it = swapchains_.find(h.index());
        return it == swapchains_.end() ? 0u : static_cast<std::uint32_t>(it->second.images.size());
    }
    [[nodiscard]] cd::rhi::TextureHandle swapchain_image(cd::rhi::SwapchainHandle h, std::uint32_t i) const override
    {
        auto it = swapchains_.find(h.index());
        if (it == swapchains_.end() || i >= it->second.image_handles.size())
            return {};
        return it->second.image_handles[i];
    }

    // ---- Image readback (phase377-B-infra2) ---------------------------------
    //
    // One-shot CommandAllocator + GraphicsCommandList: transition src_image
    // to COPY_SOURCE state (using its tracked state), issue
    // CopyTextureRegion into the readback buffer, transition back, execute,
    // wait_idle. dst_buffer must be READBACK heap (kGpuToCpu).
    [[nodiscard]] cd::core::Result<void> copy_image_to_buffer(
        cd::rhi::TextureHandle      src_image,
        cd::rhi::BufferHandle       dst_buffer,
        std::uint64_t               dst_offset,
        const cd::rhi::IDevice::ImageRegion& region
    ) override
    {
        auto* trec = find_texture(src_image);
        if (trec == nullptr)
        {
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kInvalidArgument,
                "copy_image_to_buffer: unknown src_image handle"));
        }
        auto buf_it = buffers_.find(dst_buffer.index());
        if (buf_it == buffers_.end())
        {
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kInvalidArgument,
                "copy_image_to_buffer: unknown dst_buffer handle"));
        }
        const auto& brec = buf_it->second;
        if (brec.heap_type != D3D12_HEAP_TYPE_READBACK)
        {
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kInvalidArgument,
                "copy_image_to_buffer: dst_buffer is not READBACK heap (use kGpuToCpu)"));
        }

        // One-shot allocator + command list.
        ComPtr<ID3D12CommandAllocator> alloc;
        HRESULT hr = device_->CreateCommandAllocator(
            D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&alloc));
        if (FAILED(hr))
        {
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kResourceCreationFailed,
                "copy_image_to_buffer: CreateCommandAllocator failed"));
        }
        ComPtr<ID3D12GraphicsCommandList> list;
        hr = device_->CreateCommandList(
            0, D3D12_COMMAND_LIST_TYPE_DIRECT,
            alloc.Get(), nullptr, IID_PPV_ARGS(&list));
        if (FAILED(hr))
        {
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kResourceCreationFailed,
                "copy_image_to_buffer: CreateCommandList failed"));
        }

        // Transition to COPY_SOURCE if not already there.
        if (trec->state != D3D12_RESOURCE_STATE_COPY_SOURCE)
        {
            D3D12_RESOURCE_BARRIER bar {};
            bar.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            bar.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
            bar.Transition.pResource = trec->resource.Get();
            bar.Transition.StateBefore = trec->state;
            bar.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
            bar.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            list->ResourceBarrier(1, &bar);
        }

        // phase1127 (X4-B parity): D3D12 placed-footprint copies REQUIRE a
        // 256-byte-aligned row pitch, while the IDevice contract (and the
        // Vulkan backend, bufferRowLength = 0) deliver TIGHTLY-PACKED rows
        // in dst_buffer. Recording the pitched copy straight into the
        // caller's buffer both failed Close() for small buffers AND would
        // have produced a different byte layout than Vulkan — exactly the
        // drift the X4 parity gate exists to catch. Copy into an internal
        // pitched staging buffer sized from a region-shaped footprint,
        // then de-pitch row-by-row into the caller's buffer after the
        // GPU wait.
        D3D12_RESOURCE_DESC src_desc = trec->resource->GetDesc();
        D3D12_RESOURCE_DESC region_desc = src_desc;
        region_desc.Width     = region.width;
        region_desc.Height    = region.height;
        region_desc.MipLevels = 1;
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint {};
        UINT row_count = 0;
        UINT64 row_bytes = 0;
        UINT64 staging_bytes = 0;
        device_->GetCopyableFootprints(
            &region_desc, 0, 1, 0,
            &footprint, &row_count, &row_bytes, &staging_bytes);

        D3D12_HEAP_PROPERTIES staging_heap {};
        staging_heap.Type = D3D12_HEAP_TYPE_READBACK;
        D3D12_RESOURCE_DESC staging_desc {};
        staging_desc.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
        staging_desc.Width            = staging_bytes;
        staging_desc.Height           = 1;
        staging_desc.DepthOrArraySize = 1;
        staging_desc.MipLevels        = 1;
        staging_desc.Format           = DXGI_FORMAT_UNKNOWN;
        staging_desc.SampleDesc       = { 1, 0 };
        staging_desc.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        ComPtr<ID3D12Resource> staging;
        hr = device_->CreateCommittedResource(
            &staging_heap, D3D12_HEAP_FLAG_NONE, &staging_desc,
            D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&staging));
        if (FAILED(hr))
        {
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kResourceCreationFailed,
                "copy_image_to_buffer: staging CreateCommittedResource failed"));
        }

        D3D12_TEXTURE_COPY_LOCATION src_loc {};
        src_loc.pResource        = trec->resource.Get();
        src_loc.Type             = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        src_loc.SubresourceIndex = region.mip_level +
            (region.base_layer * static_cast<UINT>(src_desc.MipLevels));

        D3D12_TEXTURE_COPY_LOCATION dst_loc {};
        dst_loc.pResource       = staging.Get();
        dst_loc.Type            = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        dst_loc.PlacedFootprint = footprint;

        const D3D12_BOX src_box {
            .left   = region.x,
            .top    = region.y,
            .front  = 0u,
            .right  = region.x + region.width,
            .bottom = region.y + region.height,
            .back   = 1u,
        };
        list->CopyTextureRegion(&dst_loc, 0, 0, 0, &src_loc, &src_box);

        // Transition back to COMMON so the texture remains usable afterwards.
        {
            D3D12_RESOURCE_BARRIER bar {};
            bar.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            bar.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
            bar.Transition.pResource = trec->resource.Get();
            bar.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
            bar.Transition.StateAfter  = D3D12_RESOURCE_STATE_COMMON;
            bar.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            list->ResourceBarrier(1, &bar);
        }
        trec->state = D3D12_RESOURCE_STATE_COMMON;

        hr = list->Close();
        if (FAILED(hr))
        {
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kResourceCreationFailed,
                "copy_image_to_buffer: Close() failed"));
        }
        ID3D12CommandList* lists[] = { list.Get() };
        graphics_queue_->ExecuteCommandLists(1, lists);
        if (FAILED(wait_idle_internal()))
        {
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kDeviceLost,
                "copy_image_to_buffer: wait_idle_internal failed"));
        }

        // De-pitch: staging (RowPitch-aligned rows) -> caller's buffer
        // (tightly-packed rows starting at dst_offset) — matches the
        // Vulkan backend's bufferRowLength = 0 layout byte-for-byte.
        void* src_mapped = nullptr;
        const D3D12_RANGE src_range { 0, static_cast<SIZE_T>(staging_bytes) };
        hr = staging->Map(0, &src_range, &src_mapped);
        if (FAILED(hr) || src_mapped == nullptr)
        {
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kResourceCreationFailed,
                "copy_image_to_buffer: staging Map failed"));
        }
        void* dst_mapped = nullptr;
        const D3D12_RANGE no_read { 0, 0 };
        hr = brec.resource->Map(0, &no_read, &dst_mapped);
        if (FAILED(hr) || dst_mapped == nullptr)
        {
            staging->Unmap(0, nullptr);
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kResourceCreationFailed,
                "copy_image_to_buffer: dst_buffer Map failed"));
        }
        const auto* src_bytes = static_cast<const std::uint8_t*>(src_mapped);
        auto* dst_bytes = static_cast<std::uint8_t*>(dst_mapped) +
                          static_cast<std::size_t>(dst_offset);
        for (UINT row = 0; row < row_count; ++row)
        {
            std::memcpy(
                dst_bytes + static_cast<std::size_t>(row) *
                                static_cast<std::size_t>(row_bytes),
                src_bytes + static_cast<std::size_t>(row) *
                                static_cast<std::size_t>(footprint.Footprint.RowPitch),
                static_cast<std::size_t>(row_bytes));
        }
        const D3D12_RANGE wrote {
            static_cast<SIZE_T>(dst_offset),
            static_cast<SIZE_T>(dst_offset +
                                row_bytes * static_cast<UINT64>(row_count)),
        };
        brec.resource->Unmap(0, &wrote);
        staging->Unmap(0, nullptr);
        return {};
    }

    // ---- Buffer upload / download (REAL — Phase 13.C v0.32.0) -------------

    [[nodiscard]] cd::core::Result<void>
    upload_buffer(cd::rhi::BufferHandle h, std::uint64_t offset, std::span<const std::byte> data) override
    {
        auto it = buffers_.find(h.index());
        if (it == buffers_.end())
        {
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kInvalidArgument, "upload_buffer: unknown buffer handle"));
        }
        const auto& rec = it->second;
        if (offset + data.size() > rec.size)
        {
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kInvalidArgument, "upload_buffer: range out of bounds"));
        }
        if (rec.heap_type != D3D12_HEAP_TYPE_UPLOAD)
        {
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kInvalidArgument,
                "upload_buffer: target heap is not UPLOAD (staging path lands in a later phase)"));
        }
        void* mapped = nullptr;
        const D3D12_RANGE no_read { 0, 0 };
        HRESULT hr = rec.resource->Map(0, &no_read, &mapped);
        if (FAILED(hr) || mapped == nullptr)
        {
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kResourceCreationFailed, "ID3D12Resource::Map failed"));
        }
        std::memcpy(static_cast<std::byte*>(mapped) + offset, data.data(), data.size());
        rec.resource->Unmap(0, nullptr);
        return {};
    }

    [[nodiscard]] cd::core::Result<void>
    download_buffer(cd::rhi::BufferHandle h, std::uint64_t offset, std::span<std::byte> data) override
    {
        auto it = buffers_.find(h.index());
        if (it == buffers_.end())
        {
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kInvalidArgument, "download_buffer: unknown buffer handle"));
        }
        const auto& rec = it->second;
        if (offset + data.size() > rec.size)
        {
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kInvalidArgument, "download_buffer: range out of bounds"));
        }
        if (rec.heap_type != D3D12_HEAP_TYPE_READBACK)
        {
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kInvalidArgument,
                "download_buffer: target heap is not READBACK (staging path lands in a later phase)"));
        }
        void* mapped = nullptr;
        const D3D12_RANGE read_range { 0, rec.size };
        HRESULT hr = rec.resource->Map(0, &read_range, &mapped);
        if (FAILED(hr) || mapped == nullptr)
        {
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kResourceCreationFailed, "ID3D12Resource::Map failed"));
        }
        std::memcpy(data.data(), static_cast<const std::byte*>(mapped) + offset, data.size());
        const D3D12_RANGE no_write { 0, 0 };
        rec.resource->Unmap(0, &no_write);
        return {};
    }

    [[nodiscard]] std::unique_ptr<cd::rhi::ICommandBuffer>
    do_create_command_buffer(cd::rhi::QueueType) override;  // defined out-of-line below

    void submit(cd::rhi::ICommandBuffer& cb) override;   // defined out-of-line below

    // Phase 397 — SubmitDesc semaphore-based path.
    // Defined out-of-line below (after D3D12CommandBuffer is complete).
    [[nodiscard]] cd::core::Result<void>
    submit(const cd::rhi::SubmitDesc& desc) override;

    // ---- Phase 142 step 2 — DXR acceleration-structure create/destroy -----
    //
    // Builds the result + scratch UAV buffers per
    // GetRaytracingAccelerationStructurePrebuildInfo. The actual
    // BuildRaytracingAccelerationStructure happens on a command list
    // (step 3); this method only allocates the GPU memory.
    [[nodiscard]] cd::core::Result<cd::rhi::AccelStructureHandle>
    create_acceleration_structure(const cd::rhi::AccelStructureDesc& desc) override
    {
        if (!features_.ray_tracing || !device5_)
        {
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kNotImplemented,
                "create_acceleration_structure: adapter lacks DXR"));
        }

        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs {};
        inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
        inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;

        std::vector<D3D12_RAYTRACING_GEOMETRY_DESC> geos;
        if (desc.kind == cd::rhi::AccelStructureKind::kBottomLevel)
        {
            if (desc.triangles.empty())
            {
                return std::unexpected(cd::rhi::rhi_errors::make(
                    cd::rhi::rhi_errors::Code::kInvalidArgument,
                    "create_acceleration_structure: BLAS must have >=1 triangle"));
            }
            geos.reserve(desc.triangles.size());
            for (const auto& t : desc.triangles)
            {
                auto vb_it = buffers_.find(t.vertex_buffer.index());
                if (vb_it == buffers_.end())
                {
                    return std::unexpected(cd::rhi::rhi_errors::make(
                        cd::rhi::rhi_errors::Code::kInvalidArgument,
                        "create_acceleration_structure: unknown vertex buffer"));
                }
                D3D12_RAYTRACING_GEOMETRY_DESC g {};
                g.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
                g.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;
                g.Triangles.Transform3x4 = 0;
                g.Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
                g.Triangles.VertexCount = t.vertex_count;
                g.Triangles.VertexBuffer.StartAddress =
                    vb_it->second.resource->GetGPUVirtualAddress() + t.vertex_offset;
                g.Triangles.VertexBuffer.StrideInBytes = t.vertex_stride;
                // D-BLAS-INDEXED (Backend-to-100 Wave 1): honor the desc's
                // index buffer instead of dropping it. The historical code
                // hardcoded IndexFormat=UNKNOWN + IndexBuffer=0, silently
                // truncating every indexed BLAS to a non-indexed build (the
                // BLAS-geo-cap silent-truncation bug class). The Vulkan
                // reference (VulkanDevice.cpp:4022) maps IndexType→VkIndexType
                // and feeds indexData.deviceAddress; mirror that here:
                // resolve the index buffer GPU-VA + map kUInt16/kUInt32 →
                // R16_UINT/R32_UINT and set IndexCount. A zero index_count is
                // a non-indexed geometry (IndexFormat=UNKNOWN, IndexBuffer=0).
                if (t.index_count > 0u && t.index_buffer.is_valid())
                {
                    auto ib_it = buffers_.find(t.index_buffer.index());
                    if (ib_it == buffers_.end())
                    {
                        return std::unexpected(cd::rhi::rhi_errors::make(
                            cd::rhi::rhi_errors::Code::kInvalidArgument,
                            "create_acceleration_structure: unknown index buffer"));
                    }
                    g.Triangles.IndexFormat =
                        (t.index_type == cd::rhi::IndexType::kUInt16)
                            ? DXGI_FORMAT_R16_UINT
                            : DXGI_FORMAT_R32_UINT;
                    g.Triangles.IndexCount = t.index_count;
                    g.Triangles.IndexBuffer =
                        ib_it->second.resource->GetGPUVirtualAddress() +
                        t.index_offset;
                }
                else
                {
                    g.Triangles.IndexFormat = DXGI_FORMAT_UNKNOWN;
                    g.Triangles.IndexCount = 0;
                    g.Triangles.IndexBuffer = 0;
                }
                geos.push_back(g);
            }
            inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
            inputs.NumDescs = static_cast<UINT>(geos.size());
            inputs.pGeometryDescs = geos.data();
        }
        else  // TLAS
        {
            if (desc.instances.empty())
            {
                return std::unexpected(cd::rhi::rhi_errors::make(
                    cd::rhi::rhi_errors::Code::kInvalidArgument,
                    "create_acceleration_structure: TLAS must have >=1 instance"));
            }
            for (const auto& inst : desc.instances)
            {
                if (!inst.blas.is_valid() || !accels_.contains(inst.blas.index()))
                {
                    return std::unexpected(cd::rhi::rhi_errors::make(
                        cd::rhi::rhi_errors::Code::kInvalidArgument,
                        "create_acceleration_structure: TLAS instance "
                        "references invalid or unknown BLAS handle"));
                }
            }
            inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
            inputs.NumDescs = static_cast<UINT>(desc.instances.size());
            inputs.InstanceDescs = 0;  // placeholder — build step fills this
        }

        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO info {};
        device5_->GetRaytracingAccelerationStructurePrebuildInfo(&inputs, &info);
        if (info.ResultDataMaxSizeInBytes == 0 || info.ScratchDataSizeInBytes == 0)
        {
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kResourceCreationFailed,
                "create_acceleration_structure: zero-size prebuild info"));
        }

        // Helper: allocate a UAV-state default-heap buffer at requested size.
        auto alloc_uav = [this](UINT64 size, D3D12_RESOURCE_STATES initial_state)
            -> cd::core::Result<ComPtr<ID3D12Resource>>
        {
            D3D12_HEAP_PROPERTIES hp {};
            hp.Type = D3D12_HEAP_TYPE_DEFAULT;
            D3D12_RESOURCE_DESC rd {};
            rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
            rd.Width = size;
            rd.Height = 1;
            rd.DepthOrArraySize = 1;
            rd.MipLevels = 1;
            rd.Format = DXGI_FORMAT_UNKNOWN;
            rd.SampleDesc.Count = 1;
            rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            rd.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
            ComPtr<ID3D12Resource> res;
            const HRESULT hr = device_->CreateCommittedResource(
                &hp, D3D12_HEAP_FLAG_NONE, &rd, initial_state, nullptr,
                IID_PPV_ARGS(&res));
            if (FAILED(hr))
            {
                return std::unexpected(cd::rhi::rhi_errors::make(
                    cd::rhi::rhi_errors::Code::kResourceCreationFailed,
                    "CreateCommittedResource (AS buffer) failed"));
            }
            return res;
        };

        auto result_r = alloc_uav(info.ResultDataMaxSizeInBytes,
            D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE);
        if (!result_r.has_value()) return std::unexpected(result_r.error());
        auto scratch_r = alloc_uav(info.ScratchDataSizeInBytes,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        if (!scratch_r.has_value()) return std::unexpected(scratch_r.error());

        AccelRecord rec;
        rec.kind = desc.kind;
        rec.result = *result_r;
        rec.scratch = *scratch_r;
        rec.result_gva = rec.result->GetGPUVirtualAddress();
        rec.scratch_gva = rec.scratch->GetGPUVirtualAddress();
        rec.result_size = info.ResultDataMaxSizeInBytes;
        rec.scratch_size = info.ScratchDataSizeInBytes;
        rec.num_descs = inputs.NumDescs;

        // phase466 — retain BLAS geometry descriptors so a subsequent
        // build_acceleration_structure can re-issue BuildRTAS with the
        // correct inputs (without forcing the caller to keep them alive).
        if (desc.kind == cd::rhi::AccelStructureKind::kBottomLevel)
        {
            rec.blas_geos = std::move(geos);
        }
        else  // TLAS: allocate an upload buffer for instance descs.
        {
            const UINT64 inst_bytes = static_cast<UINT64>(
                desc.instances.size()) * sizeof(D3D12_RAYTRACING_INSTANCE_DESC);
            D3D12_HEAP_PROPERTIES hp_up {};
            hp_up.Type = D3D12_HEAP_TYPE_UPLOAD;
            D3D12_RESOURCE_DESC rd_up {};
            rd_up.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
            rd_up.Width  = inst_bytes;
            rd_up.Height = 1;
            rd_up.DepthOrArraySize = 1;
            rd_up.MipLevels = 1;
            rd_up.Format = DXGI_FORMAT_UNKNOWN;
            rd_up.SampleDesc.Count = 1;
            rd_up.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            rd_up.Flags  = D3D12_RESOURCE_FLAG_NONE;
            ComPtr<ID3D12Resource> inst_res;
            HRESULT hr = device_->CreateCommittedResource(
                &hp_up, D3D12_HEAP_FLAG_NONE, &rd_up,
                D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                IID_PPV_ARGS(&inst_res));
            if (SUCCEEDED(hr) && inst_res)
            {
                void* mapped = nullptr;
                const D3D12_RANGE no_read { 0, 0 };
                if (SUCCEEDED(inst_res->Map(0, &no_read, &mapped)) && mapped)
                {
                    auto* dst = static_cast<D3D12_RAYTRACING_INSTANCE_DESC*>(mapped);
                    for (std::size_t i = 0; i < desc.instances.size(); ++i)
                    {
                        const auto& src = desc.instances[i];
                        D3D12_RAYTRACING_INSTANCE_DESC d {};
                        // Row-major 3x4 transform → D3D12 column-major 3x4.
                        // The cd::rhi::AccelInstance carries 3x4 row-major
                        // floats; D3D12 takes them in (col,row) packing.
                        for (int r = 0; r < 3; ++r)
                            for (int c = 0; c < 4; ++c)
                                d.Transform[r][c] = src.transform[r * 4 + c];
                        d.InstanceID   = src.instance_id & 0xFFFFFFu;
                        d.InstanceMask = src.mask;
                        d.InstanceContributionToHitGroupIndex =
                            src.hit_offset & 0xFFFFFFu;
                        d.Flags = src.flags & 0xFFu;
                        if (auto bit = accels_.find(src.blas.index()); bit != accels_.end())
                            d.AccelerationStructure = bit->second.result_gva;
                        dst[i] = d;
                    }
                    inst_res->Unmap(0, nullptr);
                    rec.tlas_instances = inst_res;
                    rec.tlas_instances_gva = inst_res->GetGPUVirtualAddress();
                }
            }
        }

        const auto id = next_id_++;
        accels_.emplace(id, std::move(rec));
        return cd::rhi::AccelStructureHandle { id, 1u };
    }

    void destroy_acceleration_structure(cd::rhi::AccelStructureHandle h) override
    {
        accels_.erase(h.index());
    }

    // ---- Resource records (Phase 13.C) ------------------------------------

    struct BufferRecord
    {
        ComPtr<ID3D12Resource> resource;
        std::uint64_t size { 0 };
        D3D12_HEAP_TYPE heap_type { D3D12_HEAP_TYPE_UPLOAD };
    };

    struct TextureRecord
    {
        ComPtr<ID3D12Resource> resource;
        DXGI_FORMAT format { DXGI_FORMAT_UNKNOWN };
        cd::rhi::Extent3D extent {};
        cd::rhi::TextureUsage usage { cd::rhi::TextureUsage::kNone };
        cd::rhi::TextureType type { cd::rhi::TextureType::k2D };
        bool is_swapchain_image { false };
        D3D12_CPU_DESCRIPTOR_HANDLE rtv_cpu {};
        D3D12_RESOURCE_STATES state { D3D12_RESOURCE_STATE_COMMON };
        // D1: validated MSAA sample count (1 for non-MSAA). The RTV/DSV created
        // for an MSAA resource MUST use the TEXTURE2DMS dimension or the view is
        // invalid; the SRV/UAV likewise need TEXTURE2DMS for sampling.
        UINT sample_count { 1u };
        // D-MIPSTATE (Backend-to-100 Wave 1): mip/layer counts + per-subresource
        // state. The `state` field above is the WHOLE-RESOURCE state used by the
        // implicit render-pass/present transitions (ALL_SUBRESOURCES). Vulkan
        // tracks layout per subresource (VkImageSubresourceRange), which is what
        // makes a mip-chain generate (read mip N as SRV / write mip N+1 as
        // render-target/UAV) expressible. `subresource_states`, when non-empty,
        // holds the per-subresource D3D12_RESOURCE_STATES indexed by
        // (mip + layer * mip_levels); it is lazily materialised the first time a
        // SUBSET barrier targets the texture, seeded from `state`. While empty,
        // the texture is whole-resource-coherent and `state` alone is authority.
        UINT mip_levels { 1u };
        UINT array_layers { 1u };
        std::vector<D3D12_RESOURCE_STATES> subresource_states;
    };

    struct TextureViewRecord
    {
        cd::rhi::TextureHandle parent {};
        DXGI_FORMAT format { DXGI_FORMAT_UNKNOWN };
        D3D12_CPU_DESCRIPTOR_HANDLE rtv_cpu {};
        /// Phase 126 — true when the view was created with
        /// `TextureType::kCube`. `update_descriptor_set` uses this hint
        /// to pick `D3D12_SRV_DIMENSION_TEXTURECUBE` instead of TEX2D
        /// when (re-)creating the SRV at descriptor-set update time.
        bool is_cube { false };
        /// Phase 393/395 — true when the view was created with k1D/k3D.
        bool is_1d { false };
        bool is_3d { false };
        /// D-SRV-MS (Backend-to-100 Wave 1) — true when the parent texture is
        /// multisampled (sample_count > 1). update_descriptor_set reads this
        /// to pick D3D12_SRV_DIMENSION_TEXTURE2DMS instead of TEXTURE2D when
        /// (re-)creating the SRV at descriptor-set update time, so an MSAA
        /// texture can be sampled through the generic descriptor surface too.
        bool is_ms { false };
    };

    struct ShaderModuleRecord
    {
        std::vector<std::uint8_t> bytecode;
        cd::rhi::ShaderStage stage { cd::rhi::ShaderStage::kNone };
        std::string entry_point;
    };

    // Phase 125 — sync primitive storage. All three are backed by
    // ID3D12Fence; the wrappers differ in surface semantics.
    struct SemaphoreRecord
    {
        ComPtr<ID3D12Fence> fence;
        std::uint64_t       value { 0 };
    };
    struct FenceRecord
    {
        ComPtr<ID3D12Fence> fence;
        HANDLE              event { nullptr };
        std::uint64_t       target_value { 0 };
    };
    struct TimelineRecord
    {
        ComPtr<ID3D12Fence> fence;
        HANDLE              event { nullptr };
    };

    struct DescriptorSetLayoutRecord
    {
        std::vector<cd::rhi::DescriptorSetLayoutBinding> bindings;
        std::uint32_t view_count { 0 };       // CBV + SRV + UAV total
        std::uint32_t sampler_count { 0 };
    };

    struct PipelineLayoutRecord
    {
        ComPtr<ID3D12RootSignature> root_sig;
        /// Index of the first table parameter per descriptor-set slot.
        /// table_params[i] == root-signature parameter index of the
        /// descriptor table that backs descriptor-set index i.
        std::vector<std::uint32_t> table_params;
        /// D2 (parity with Vulkan kSampler write path): root-signature
        /// parameter index of the SAMPLER descriptor table that backs the
        /// set's bare-`kSampler` bindings, per descriptor-set slot.
        /// sampler_table_params[i] == UINT32_MAX when set i declares no
        /// `kSampler` binding (the classic combined-sampler / bindless paths
        /// keep their own sampler handling). A separate vector from
        /// table_params because a SAMPLER range cannot share a descriptor
        /// table with CBV/SRV/UAV ranges on D3D12.
        std::vector<std::uint32_t> sampler_table_params;
        /// phase466 — root-signature parameter index for the 32-bit
        /// constants slot that backs push_constants. UINT32_MAX means
        /// "no push-constant range declared at layout creation".
        std::uint32_t push_constants_param { ~std::uint32_t { 0 } };
        std::uint32_t push_constants_dwords { 0 };  // total Num32BitValues
        /// B1b — root-signature parameter index of the SAMPLER descriptor table
        /// backing the bindless sampler half (s-register array). UINT32_MAX when
        /// the layout has no bindless sampler binding. bind_bindless_texture_array
        /// points this table at the device's shader-visible sampler heap.
        std::uint32_t bindless_sampler_param { ~std::uint32_t { 0 } };
        /// Serialized root-signature blob. The graphics root_sig above carries
        /// D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT, which
        /// a DXR GLOBAL root signature MUST NOT have (CreateStateObject rejects
        /// it with E_INVALIDARG). create_rt_pipeline deserializes this blob,
        /// strips the IA flag, and re-serializes a DXR-compatible variant.
        ComPtr<ID3DBlob> root_sig_blob;
    };

    struct DescriptorSetRecord
    {
        cd::rhi::DescriptorSetLayoutHandle layout_handle {};
        /// CPU-heap slot range that holds this set's descriptors. The
        /// values are written into the CPU-visible heap by
        /// update_descriptor_set and copied into the per-frame GPU
        /// heap by bind_descriptor_set.
        std::uint32_t cpu_heap_offset { 0 };
        std::uint32_t view_count { 0 };
        /// D2 (parity with Vulkan kSampler write path): CPU-staging
        /// sampler-heap slot range that holds this set's bare-`kSampler`
        /// descriptors. update_descriptor_set's kSampler case copies the
        /// SamplerRecord's descriptor into [sampler_cpu_offset,
        /// sampler_cpu_offset + sampler_count); bind_descriptor_set copies
        /// that range into the per-frame shader-visible sampler ring.
        /// sampler_count == 0 → no dynamic sampler (classic static-sampler
        /// or bindless path).
        std::uint32_t sampler_cpu_offset { 0 };
        std::uint32_t sampler_count { 0 };
    };

    struct GraphicsPipelineRecord
    {
        ComPtr<ID3D12PipelineState> pso;
        cd::rhi::PipelineLayoutHandle layout_handle {};
        D3D_PRIMITIVE_TOPOLOGY d3d_topology { D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST };
        /// phase766 — true when this PSO was built via the mesh-shading
        /// PIPELINE_STATE_STREAM path. The command-list bind path skips
        /// IASetPrimitiveTopology for mesh PSOs (the IA is inert), and
        /// draw_mesh_tasks gates DispatchMesh on this flag.
        bool is_mesh_shader { false };
        /// D6 (phase1187) — per-binding vertex stride captured from the
        /// pipeline's VertexBinding[] at PSO creation. `bind_vertex_buffer`
        /// reads `binding_strides[binding]` for D3D12_VERTEX_BUFFER_VIEW.
        /// StrideInBytes instead of the legacy hardcoded 24. The PSO's input
        /// layout supplies attribute offsets but D3D12 still requires the
        /// stride on the VBV, so it must be cached here. Indexed by the
        /// engine `VertexBinding::binding` slot; a binding absent from the map
        /// falls back to 0 (a no-stride/degenerate VBV) which surfaces the
        /// miswiring loudly rather than silently fetching wrong vertices.
        std::unordered_map<std::uint32_t, std::uint32_t> binding_strides;
    };

    // phase466 — compute pipeline record.
    struct ComputePipelineRecord
    {
        ComPtr<ID3D12PipelineState>   pso;
        cd::rhi::PipelineLayoutHandle layout_handle {};
    };

    struct SwapchainRecord
    {
        ComPtr<IDXGISwapChain3> swap;
        DXGI_FORMAT format { DXGI_FORMAT_UNKNOWN };
        DXGI_FORMAT swap_format { DXGI_FORMAT_UNKNOWN };
        cd::rhi::Extent2D extent {};
        bool vsync { true };
        std::vector<ComPtr<ID3D12Resource>> images;
        std::vector<cd::rhi::TextureHandle> image_handles;
        std::vector<cd::rhi::TextureViewHandle> image_view_handles;
        ComPtr<ID3D12DescriptorHeap> rtv_heap;
        UINT rtv_descriptor_size { 0 };
    };

    [[nodiscard]] ID3D12Device* native_device() noexcept { return device_.Get(); }
    [[nodiscard]] ID3D12CommandQueue* native_queue() noexcept { return graphics_queue_.Get(); }
    [[nodiscard]] TextureRecord* find_texture(cd::rhi::TextureHandle h) noexcept
    {
        auto it = textures_.find(h.index());
        return it == textures_.end() ? nullptr : &it->second;
    }
    [[nodiscard]] TextureViewRecord* find_texture_view(cd::rhi::TextureViewHandle h) noexcept
    {
        auto it = texture_views_.find(h.index());
        return it == texture_views_.end() ? nullptr : &it->second;
    }
    [[nodiscard]] ID3D12Device5* device5() const noexcept { return device5_.Get(); }

    // D-MIPSTATE: expose the tracked per-subresource state for tests. Returns
    // the D3D12_RESOURCE_STATES value of (mip, layer); when the texture is
    // whole-resource-coherent (no divergent map) every subresource reports the
    // single `state`. Sentinel ~0u for an unknown handle.
    [[nodiscard]] std::uint32_t
    debug_texture_subresource_state(cd::rhi::TextureHandle texture,
                                    std::uint32_t mip,
                                    std::uint32_t layer) const noexcept override
    {
        auto it = textures_.find(texture.index());
        if (it == textures_.end())
            return ~std::uint32_t { 0 };
        const TextureRecord& tr = it->second;
        if (tr.subresource_states.empty())
            return static_cast<std::uint32_t>(tr.state);
        const std::size_t sub =
            static_cast<std::size_t>(mip) +
            static_cast<std::size_t>(layer) * tr.mip_levels;
        if (sub >= tr.subresource_states.size())
            return static_cast<std::uint32_t>(tr.state);
        return static_cast<std::uint32_t>(tr.subresource_states[sub]);
    }

private:
    [[nodiscard]] HRESULT wait_idle_internal()
    {
        const UINT64 sig = ++idle_value_;
        HRESULT hr = graphics_queue_->Signal(idle_fence_.Get(), sig);
        if (FAILED(hr))
            return hr;
        if (idle_fence_->GetCompletedValue() < sig)
        {
            hr = idle_fence_->SetEventOnCompletion(sig, idle_event_);
            if (FAILED(hr))
                return hr;
            WaitForSingleObject(idle_event_, INFINITE);
        }
        return S_OK;
    }

    ComPtr<IDXGIFactory6> factory_;
    ComPtr<ID3D12Device> device_;
    ComPtr<ID3D12Device5> device5_;  // DXR entry points (Phase 142)
    ComPtr<ID3D12Device2> device2_;  // CreatePipelineState(stream) for mesh PSOs (phase766)
    ComPtr<ID3D12CommandQueue> graphics_queue_;
    ComPtr<ID3D12Fence> idle_fence_;
    UINT64 idle_value_ { 0 };
    HANDLE idle_event_ { nullptr };
    DXGI_ADAPTER_DESC1 adapter_desc_ {};
    std::string adapter_name_;

    cd::rhi::DeviceLimits limits_ {};
    cd::rhi::DeviceFeatures features_ {};

    std::uint32_t next_id_ { 1u };
    std::unordered_map<std::uint32_t, BufferRecord> buffers_;
    std::unordered_map<std::uint32_t, TextureRecord> textures_;
    std::unordered_map<std::uint32_t, TextureViewRecord> texture_views_;
    std::unordered_map<std::uint32_t, SwapchainRecord> swapchains_;
    std::unordered_map<std::uint32_t, ShaderModuleRecord> shader_modules_;
    std::unordered_map<std::uint32_t, DescriptorSetLayoutRecord> descriptor_set_layouts_;
    std::unordered_map<std::uint32_t, PipelineLayoutRecord> pipeline_layouts_;
    std::unordered_map<std::uint32_t, GraphicsPipelineRecord> graphics_pipelines_;
    std::unordered_map<std::uint32_t, ComputePipelineRecord>  compute_pipelines_;  // phase466
    std::unordered_map<std::uint32_t, DescriptorSetRecord> descriptor_sets_;

    // Phase 142 step 2 — DXR acceleration-structure record.
    struct AccelRecord
    {
        cd::rhi::AccelStructureKind kind { cd::rhi::AccelStructureKind::kBottomLevel };
        ComPtr<ID3D12Resource>      result;   // result-data buffer (also the AS object)
        ComPtr<ID3D12Resource>      scratch;  // scratch-data buffer (build-time)
        D3D12_GPU_VIRTUAL_ADDRESS   result_gva { 0 };
        D3D12_GPU_VIRTUAL_ADDRESS   scratch_gva { 0 };
        UINT64                      result_size { 0 };
        UINT64                      scratch_size { 0 };
        // phase466 — retained build inputs for BLAS/TLAS BuildRaytracingAccelerationStructure.
        // BLAS: triangle geometry descriptors with cached GPU VAs.
        // TLAS: instance descriptor staging buffer (uploaded once at create).
        std::vector<D3D12_RAYTRACING_GEOMETRY_DESC>     blas_geos;
        ComPtr<ID3D12Resource>                          tlas_instances;  // upload heap
        D3D12_GPU_VIRTUAL_ADDRESS                       tlas_instances_gva { 0 };
        UINT                                            num_descs { 0 };
    };
    std::unordered_map<std::uint32_t, AccelRecord> accels_;

    // phase466 — sampler record (slot in sampler_heap_).
    struct SamplerRecord
    {
        std::uint32_t              heap_slot  { 0 };
        D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle {};
    };
    std::unordered_map<std::uint32_t, SamplerRecord> samplers_;

    // Phase 398 — DXR RTPSO record.
    struct RtPipelineRecord
    {
        ComPtr<ID3D12StateObject>           state_obj;
        ComPtr<ID3D12StateObjectProperties> props;
        UINT                                group_handle_size { 32u };
        // Per shader-group export name, in the SAME first-occurrence group
        // order create_rt_pipeline emits them (general groups carry the
        // shader export name; hit groups carry the synthesised hit-group
        // export name). get_rt_shader_group_handles indexes this list and
        // calls GetShaderIdentifier(name) to author the SBT. Wide strings:
        // GetShaderIdentifier takes LPCWSTR.
        std::vector<std::wstring>           group_export_names;
        // The GLOBAL root signature baked into the state object. DXR binds
        // root arguments through the compute root-binding model, so
        // bind_rt_pipeline must SetComputeRootSignature(this) for a later
        // bind_descriptor_set to land its descriptor table on the RTPSO.
        ComPtr<ID3D12RootSignature>         global_root_sig;
        cd::rhi::PipelineLayoutHandle       layout_handle {};
    };
    std::unordered_map<std::uint32_t, RtPipelineRecord> rt_pipelines_;

    // D12 (phase1184) — lazily-created glslang front-end for the in-device
    // GLSL → SPIR-V → HLSL → DXIL cross-compile path. Built on first GLSL
    // create_shader_module and reused (so the SPIR-V half is amortised).
    // Null when CD_ENABLE_GLSLANG=OFF — a GLSL request then returns a typed
    // kBackendUnavailable error instead of crashing.
    std::unique_ptr<cd::shader::ICompiler> glsl_compiler_;

    /// Return the cached glslang compiler, building it on first use. May
    /// return nullptr (glslang backend not compiled in).
    [[nodiscard]] cd::shader::ICompiler* glsl_compiler()
    {
        if (glsl_compiler_ == nullptr)
            glsl_compiler_ = cd::shader::make_glslang_compiler();
        return glsl_compiler_.get();
    }

public:
    // Phase 142 step 3 — AccelRecord accessor for D3D12CommandBuffer.
    // Placed in public after AccelRecord is fully declared so the
    // return type doesn't trip incomplete-type lookup.
    [[nodiscard]] AccelRecord* find_accel(cd::rhi::AccelStructureHandle h) noexcept
    {
        auto it = accels_.find(h.index());
        return it == accels_.end() ? nullptr : &it->second;
    }
private:
    // CPU-visible descriptor heap (CBV/SRV/UAV) — slot-bump allocator.
    ComPtr<ID3D12DescriptorHeap> cpu_heap_;
    UINT cpu_heap_increment_ { 0 };
    std::uint32_t cpu_heap_cursor_ { 0 };

    // Phase 125 — sync primitive registries.
    std::unordered_map<std::uint32_t, SemaphoreRecord> semaphores_;
    std::unordered_map<std::uint32_t, FenceRecord>     fences_;
    std::unordered_map<std::uint32_t, TimelineRecord>  timelines_;

    // Phase 124 — device-level RTV / DSV pools for create_texture_view.
    // Bump allocators. The swapchain RTV path uses its own per-swapchain
    // heap (Phase 13.C); these pools are for non-swapchain attachments
    // (depth target, offscreen colour pass, IBL render).
    static constexpr std::uint32_t kViewPoolCap = 256;
    ComPtr<ID3D12DescriptorHeap> rtv_pool_;
    UINT                          rtv_increment_ { 0 };
    std::uint32_t                 rtv_cursor_    { 0 };
    ComPtr<ID3D12DescriptorHeap> dsv_pool_;
    UINT                          dsv_increment_ { 0 };
    std::uint32_t                 dsv_cursor_    { 0 };

    // phase466 — sampler descriptor heap (CPU-visible, bump allocator).
    // This is the per-create_sampler STAGING heap: each create_sampler bumps
    // one slot here and the SamplerRecord caches the resulting CPU handle. It
    // is the CopyDescriptors SOURCE for the kSampler write path (D2).
    static constexpr std::uint32_t kSamplerHeapCap = 256;
    ComPtr<ID3D12DescriptorHeap> sampler_heap_;
    UINT                          sampler_heap_increment_ { 0 };
    std::uint32_t                 sampler_cursor_         { 0 };

    // D2 (parity with Vulkan kSampler) — per-DESCRIPTOR-SET sampler staging +
    // a shader-visible sampler RING.
    //
    //   * sampler_set_cpu_heap_ (CPU-only): update_descriptor_set's kSampler
    //     case copies each set's bound sampler descriptors here (one slot per
    //     declared sampler binding), reserved at allocate_descriptor_set time.
    //     Mirrors how cpu_heap_ stages CBV/SRV/UAV writes.
    //   * gpu_sampler_heap_ (SHADER-VISIBLE): bind_descriptor_set copies the
    //     set's staged sampler descriptors into this ring and points the
    //     layout's SAMPLER descriptor table at the resulting GPU region.
    //     Mirrors copy_set_to_gpu_heap / unified_heap_ for the CBV/SRV/UAV ring.
    //
    // Both are separate from bindless_sampler_heap_ (the bindless `sampler2D[]`
    // default-LINEAR pool) — D3D12 binds at most ONE SAMPLER heap per draw, so
    // a set carrying real dynamic samplers binds THIS ring instead.
    static constexpr std::uint32_t kSamplerSetCpuCap = 1024;
    static constexpr UINT          kGpuSamplerRingCap = 2048;  // sampler heaps cap at 2048
    ComPtr<ID3D12DescriptorHeap> sampler_set_cpu_heap_;
    std::uint32_t                 sampler_set_cpu_cursor_ { 0 };
    ComPtr<ID3D12DescriptorHeap> gpu_sampler_heap_;
    std::uint32_t                 gpu_sampler_cursor_     { 0 };

    [[nodiscard]] cd::core::Result<void> ensure_rtv_pool_()
    {
        if (rtv_pool_ != nullptr) return {};
        D3D12_DESCRIPTOR_HEAP_DESC hd {};
        hd.NumDescriptors = kViewPoolCap;
        hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        hd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        if (FAILED(device_->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&rtv_pool_))))
        {
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kResourceCreationFailed,
                "RTV view-pool heap creation failed"));
        }
        rtv_increment_ = device_->GetDescriptorHandleIncrementSize(
            D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        return {};
    }
    [[nodiscard]] cd::core::Result<void> ensure_dsv_pool_()
    {
        if (dsv_pool_ != nullptr) return {};
        D3D12_DESCRIPTOR_HEAP_DESC hd {};
        hd.NumDescriptors = kViewPoolCap;
        hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
        hd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        if (FAILED(device_->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&dsv_pool_))))
        {
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kResourceCreationFailed,
                "DSV view-pool heap creation failed"));
        }
        dsv_increment_ = device_->GetDescriptorHandleIncrementSize(
            D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
        return {};
    }
    [[nodiscard]] cd::core::Result<void> ensure_cpu_heap_()
    {
        if (cpu_heap_ != nullptr) return {};
        D3D12_DESCRIPTOR_HEAP_DESC hd {};
        hd.NumDescriptors = 4096;
        hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        hd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        if (FAILED(device_->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&cpu_heap_))))
        {
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kResourceCreationFailed,
                "CPU descriptor heap creation failed (view path)"));
        }
        cpu_heap_increment_ = device_->GetDescriptorHandleIncrementSize(
            D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        return {};
    }
    // D2 — CPU-only staging heap that holds per-descriptor-set sampler
    // descriptors written by update_descriptor_set's kSampler case. Idempotent.
    [[nodiscard]] cd::core::Result<void> ensure_sampler_set_cpu_heap_()
    {
        if (sampler_set_cpu_heap_ != nullptr) return {};
        if (sampler_heap_increment_ == 0)
            sampler_heap_increment_ = device_->GetDescriptorHandleIncrementSize(
                D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
        D3D12_DESCRIPTOR_HEAP_DESC hd {};
        hd.NumDescriptors = kSamplerSetCpuCap;
        hd.Type  = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
        hd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        if (FAILED(device_->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&sampler_set_cpu_heap_))))
        {
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kResourceCreationFailed,
                "per-set sampler staging heap creation failed"));
        }
        return {};
    }
    // D2 — shader-visible sampler RING that bind_descriptor_set copies each
    // set's staged samplers into (the SAMPLER-heap analogue of unified_heap_).
    [[nodiscard]] cd::core::Result<void> ensure_gpu_sampler_heap_()
    {
        if (gpu_sampler_heap_ != nullptr) return {};
        if (sampler_heap_increment_ == 0)
            sampler_heap_increment_ = device_->GetDescriptorHandleIncrementSize(
                D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
        D3D12_DESCRIPTOR_HEAP_DESC hd {};
        hd.NumDescriptors = kGpuSamplerRingCap;
        hd.Type  = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
        hd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        if (FAILED(device_->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&gpu_sampler_heap_))))
        {
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kResourceCreationFailed,
                "shader-visible sampler ring heap creation failed"));
        }
        return {};
    }
    // UNIFIED shader-visible CBV/SRV/UAV heap (phase1190 B6 follow-up). D3D12
    // allows exactly ONE shader-visible CBV/SRV/UAV heap bound per draw, so the
    // per-set ring and the persistent bindless pool MUST live in the SAME heap —
    // otherwise a draw that binds a classic set-0 table AND the bindless set-1
    // array silently unbinds whichever heap was set last (wrong/garbage
    // sampling). Layout of the one heap (size kGpuHeapCap + kBindlessHeapCap):
    //
    //   [0, kGpuHeapCap)                          — the per-set RING (unchanged
    //                                               wrap/copy logic, offset 0)
    //   [kGpuHeapCap, kGpuHeapCap+kBindlessHeapCap) — the PERSISTENT bindless pool
    //
    // SetDescriptorHeaps always binds THIS one heap (+ the sampler heap), never
    // swapped between classic and bindless. The ring's wrap test stays bounded by
    // kGpuHeapCap so it can never stomp the bindless region. Both gpu_heap() and
    // bindless_heap() return this same heap; their RootDescriptorTable GPU
    // handles point into their respective sub-regions.
    ComPtr<ID3D12DescriptorHeap> unified_heap_;

    // GPU-visible descriptor heap — populated per-bind by copying from
    // the CPU heap. Ring-buffer style allocator (16k slots) so frames
    // don't trample each other. The ring is the [0, kGpuHeapCap) sub-region of
    // unified_heap_.
    UINT gpu_heap_increment_ { 0 };
    std::uint32_t gpu_heap_cursor_ { 0 };
    static constexpr UINT kGpuHeapCap = 16384;

    // D10 — persistent SHADER-VISIBLE bindless pool (CBV/SRV/UAV). It is the
    // [kGpuHeapCap, kGpuHeapCap+kBindlessHeapCap) sub-region of unified_heap_
    // (NOT a separate heap — see the unification rationale above). Bindless slots
    // must PERSIST across frames (they are written once and dynamic-indexed by
    // the shader), so they cannot live in the ring that wraps per-bind.
    // write_bindless_texture_slot writes SRVs directly here at
    // (kGpuHeapCap + array.base + slot); the command path points the set-1 root
    // table at the array's GPU base. 1024 descriptors comfortably covers the
    // per-prim showcase scenes (Khronos Sponza is 103).
    static constexpr UINT kBindlessHeapCap = 1024;
    static constexpr UINT kUnifiedHeapCap  = kGpuHeapCap + kBindlessHeapCap;
    UINT bindless_increment_ { 0 };
    std::uint32_t bindless_cursor_ { 0 };

    // One contiguous bindless slot range allocated by create_bindless_texture_array.
    struct BindlessArrayRecord
    {
        std::uint32_t base_slot { 0 };
        std::uint32_t slot_count { 0 };
    };
    std::unordered_map<std::uint32_t, BindlessArrayRecord> bindless_arrays_;

    // B1b — shader-visible SAMPLER heap backing the bindless sampler half. The
    // bindless `sampler2D[]` emits a SamplerState ARRAY `register(sM, spaceN)[]`
    // that the DXIL dynamic-indexes by the SAME index as the texture, so the heap
    // is pre-filled with the default LINEAR-clamp sampler at every slot — any
    // in-range index resolves to it. Sampler heaps cap at 2048; 1024 fits.
    ComPtr<ID3D12DescriptorHeap> bindless_sampler_heap_;

    // Lazily create the ONE unified shader-visible CBV/SRV/UAV heap that backs
    // both the per-set ring [0, kGpuHeapCap) and the persistent bindless pool
    // [kGpuHeapCap, kUnifiedHeapCap). Idempotent; safe to call from either the
    // ring path (copy_set_to_gpu_heap) or the bindless path (ensure_bindless_heap_).
    [[nodiscard]] cd::core::Result<void> ensure_unified_heap_()
    {
        if (unified_heap_ != nullptr) return {};
        D3D12_DESCRIPTOR_HEAP_DESC hd {};
        hd.NumDescriptors = kUnifiedHeapCap;
        hd.Type  = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        hd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        if (FAILED(device_->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&unified_heap_))))
        {
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kResourceCreationFailed,
                "unified shader-visible CBV/SRV/UAV heap creation failed"));
        }
        const UINT inc = device_->GetDescriptorHandleIncrementSize(
            D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        gpu_heap_increment_ = inc;
        bindless_increment_ = inc;
        return {};
    }

    // Lazily create the bindless pool — now a sub-region of the unified heap —
    // plus its co-resident SAMPLER heap. Ensures the unified heap first so the
    // bindless sub-region exists, then sets up the sampler half.
    [[nodiscard]] cd::core::Result<void> ensure_bindless_heap_()
    {
        if (bindless_sampler_heap_ != nullptr) return {};
        if (auto r = ensure_unified_heap_(); !r.has_value())
            return std::unexpected(r.error());

        // Co-create the sampler heap, pre-filled with the default sampler.
        D3D12_DESCRIPTOR_HEAP_DESC sd {};
        sd.NumDescriptors = kBindlessHeapCap;
        sd.Type  = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
        sd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        if (FAILED(device_->CreateDescriptorHeap(&sd,
                                                 IID_PPV_ARGS(&bindless_sampler_heap_))))
        {
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kResourceCreationFailed,
                "bindless sampler heap creation failed"));
        }
        const UINT samp_inc = device_->GetDescriptorHandleIncrementSize(
            D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
        D3D12_SAMPLER_DESC dsd {};
        dsd.Filter         = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        dsd.AddressU       = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        dsd.AddressV       = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        dsd.AddressW       = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        dsd.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
        dsd.MaxLOD         = D3D12_FLOAT32_MAX;
        D3D12_CPU_DESCRIPTOR_HANDLE sc =
            bindless_sampler_heap_->GetCPUDescriptorHandleForHeapStart();
        for (UINT i = 0; i < kBindlessHeapCap; ++i)
        {
            device_->CreateSampler(&dsd, sc);
            sc.ptr += samp_inc;
        }
        return {};
    }

public:
    // D10 — bindless heap accessors for the command path + smokes. The bindless
    // pool is a sub-region of the unified heap, so this returns the SAME heap as
    // gpu_heap() — binding either one binds the unified heap (the whole point of
    // the B6 follow-up unification: classic set-0 + bindless set-1 co-bind).
    [[nodiscard]] ID3D12DescriptorHeap* bindless_heap() noexcept
    {
        return unified_heap_.Get();
    }
    [[nodiscard]] ID3D12DescriptorHeap* bindless_sampler_heap() noexcept
    {
        return bindless_sampler_heap_.Get();
    }
    [[nodiscard]] UINT bindless_increment() const noexcept
    {
        return bindless_increment_;
    }
    /// GPU descriptor handle for a bindless array's slot 0 — the base the
    /// space1 unbounded SRV table resolves `register(t0, space1)[slot]` against.
    /// The bindless pool lives at [kGpuHeapCap, ...) of the unified heap, so the
    /// region offset kGpuHeapCap is added on top of the array's base slot.
    /// Returns ptr==0 for an unknown handle (or no heap yet).
    [[nodiscard]] D3D12_GPU_DESCRIPTOR_HANDLE
    bindless_array_gpu_base(cd::rhi::BindlessTextureArrayHandle h) noexcept
    {
        if (unified_heap_ == nullptr) return D3D12_GPU_DESCRIPTOR_HANDLE { 0 };
        auto it = bindless_arrays_.find(h.index());
        if (it == bindless_arrays_.end()) return D3D12_GPU_DESCRIPTOR_HANDLE { 0 };
        D3D12_GPU_DESCRIPTOR_HANDLE out =
            unified_heap_->GetGPUDescriptorHandleForHeapStart();
        out.ptr += static_cast<UINT64>(kGpuHeapCap + it->second.base_slot) *
                   bindless_increment_;
        return out;
    }

public:
    [[nodiscard]] GraphicsPipelineRecord* find_graphics_pipeline(cd::rhi::GraphicsPipelineHandle h) noexcept
    {
        auto it = graphics_pipelines_.find(h.index());
        return it == graphics_pipelines_.end() ? nullptr : &it->second;
    }
    // phase466 — compute pipeline accessor for D3D12CommandBuffer.
    [[nodiscard]] ComputePipelineRecord* find_compute_pipeline(cd::rhi::ComputePipelineHandle h) noexcept
    {
        auto it = compute_pipelines_.find(h.index());
        return it == compute_pipelines_.end() ? nullptr : &it->second;
    }
    [[nodiscard]] PipelineLayoutRecord* find_pipeline_layout(cd::rhi::PipelineLayoutHandle h) noexcept
    {
        auto it = pipeline_layouts_.find(h.index());
        return it == pipeline_layouts_.end() ? nullptr : &it->second;
    }
    [[nodiscard]] BufferRecord* find_buffer(cd::rhi::BufferHandle h) noexcept
    {
        auto it = buffers_.find(h.index());
        return it == buffers_.end() ? nullptr : &it->second;
    }
    // D8 (DXR command path) — RT pipeline accessor for D3D12CommandBuffer so
    // bind_rt_pipeline can pull the ID3D12StateObject created by
    // create_rt_pipeline (the RTPSO is bound via SetPipelineState1, NOT
    // SetPipelineState — a state object is not an ID3D12PipelineState).
    [[nodiscard]] RtPipelineRecord* find_rt_pipeline(cd::rhi::RtPipelineHandle h) noexcept
    {
        auto it = rt_pipelines_.find(h.index());
        return it == rt_pipelines_.end() ? nullptr : &it->second;
    }
    [[nodiscard]] DescriptorSetRecord* find_descriptor_set(cd::rhi::DescriptorSetHandle h) noexcept
    {
        auto it = descriptor_sets_.find(h.index());
        return it == descriptor_sets_.end() ? nullptr : &it->second;
    }

    /// Copy `view_count` descriptors from this set's CPU heap slots into
    /// the per-frame GPU-visible heap and return the resulting GPU
    /// descriptor handle. Allocates a fresh GPU heap on first call.
    /// Returns an invalid handle (ptr == 0) on failure.
    [[nodiscard]] D3D12_GPU_DESCRIPTOR_HANDLE
    copy_set_to_gpu_heap(const DescriptorSetRecord& set)
    {
        // The per-set ring is the [0, kGpuHeapCap) sub-region of the unified
        // heap (phase1190 B6 follow-up). Allocate the unified heap on first use.
        if (unified_heap_ == nullptr)
        {
            if (auto r = ensure_unified_heap_(); !r.has_value())
                return D3D12_GPU_DESCRIPTOR_HANDLE { 0 };
        }
        // B6b safety: a single set whose view_count exceeds kGpuHeapCap would
        // wrap past the end of the ring and overflow into the bindless region
        // at [kGpuHeapCap, kUnifiedHeapCap), violating the non-overlap invariant.
        // Assert structurally so the bug surfaces at the allocation site rather
        // than as silent corruption of bindless descriptors.
        assert(set.view_count <= kGpuHeapCap &&
               "D3D12 descriptor set view_count exceeds kGpuHeapCap — would overflow into bindless region");
        // Wrap stays bounded by kGpuHeapCap so the ring NEVER stomps the bindless
        // region at [kGpuHeapCap, kUnifiedHeapCap); rely on wait_idle() between
        // frames to keep the ring sane.
        if (gpu_heap_cursor_ + set.view_count > kGpuHeapCap)
            gpu_heap_cursor_ = 0;
        const auto slot = gpu_heap_cursor_;
        gpu_heap_cursor_ += set.view_count;
        D3D12_CPU_DESCRIPTOR_HANDLE src = cpu_heap_->GetCPUDescriptorHandleForHeapStart();
        src.ptr += static_cast<SIZE_T>(set.cpu_heap_offset) * cpu_heap_increment_;
        D3D12_CPU_DESCRIPTOR_HANDLE gpu_cpu = unified_heap_->GetCPUDescriptorHandleForHeapStart();
        gpu_cpu.ptr += static_cast<SIZE_T>(slot) * gpu_heap_increment_;
        device_->CopyDescriptorsSimple(set.view_count, gpu_cpu, src,
                                       D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        D3D12_GPU_DESCRIPTOR_HANDLE out = unified_heap_->GetGPUDescriptorHandleForHeapStart();
        out.ptr += static_cast<UINT64>(slot) * gpu_heap_increment_;
        return out;
    }
    [[nodiscard]] ID3D12DescriptorHeap* gpu_heap() noexcept { return unified_heap_.Get(); }

    /// D2 — copy this set's staged sampler descriptors from the CPU sampler
    /// staging heap into the shader-visible sampler ring and return the GPU
    /// handle of the set's sampler-table base. Mirrors copy_set_to_gpu_heap
    /// for the SAMPLER heap type. Returns ptr == 0 on failure or when the set
    /// has no dynamic samplers (caller falls back to the bindless sampler heap).
    [[nodiscard]] D3D12_GPU_DESCRIPTOR_HANDLE
    copy_set_samplers_to_gpu_heap(const DescriptorSetRecord& set)
    {
        if (set.sampler_count == 0 || sampler_set_cpu_heap_ == nullptr)
            return D3D12_GPU_DESCRIPTOR_HANDLE { 0 };
        if (gpu_sampler_heap_ == nullptr)
        {
            if (auto r = ensure_gpu_sampler_heap_(); !r.has_value())
                return D3D12_GPU_DESCRIPTOR_HANDLE { 0 };
        }
        // Ring wrap (bounded by the heap cap). wait_idle() between frames keeps
        // the ring sane, same contract as the CBV/SRV/UAV ring.
        if (gpu_sampler_cursor_ + set.sampler_count > kGpuSamplerRingCap)
            gpu_sampler_cursor_ = 0;
        const auto slot = gpu_sampler_cursor_;
        gpu_sampler_cursor_ += set.sampler_count;
        D3D12_CPU_DESCRIPTOR_HANDLE src =
            sampler_set_cpu_heap_->GetCPUDescriptorHandleForHeapStart();
        src.ptr += static_cast<SIZE_T>(set.sampler_cpu_offset) * sampler_heap_increment_;
        D3D12_CPU_DESCRIPTOR_HANDLE gpu_cpu =
            gpu_sampler_heap_->GetCPUDescriptorHandleForHeapStart();
        gpu_cpu.ptr += static_cast<SIZE_T>(slot) * sampler_heap_increment_;
        device_->CopyDescriptorsSimple(set.sampler_count, gpu_cpu, src,
                                       D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
        D3D12_GPU_DESCRIPTOR_HANDLE out =
            gpu_sampler_heap_->GetGPUDescriptorHandleForHeapStart();
        out.ptr += static_cast<UINT64>(slot) * sampler_heap_increment_;
        return out;
    }
    [[nodiscard]] ID3D12DescriptorHeap* gpu_sampler_heap() noexcept
    {
        return gpu_sampler_heap_.Get();
    }
};

// ---- Trivial command buffer (Phase 13.C — clear-only) ----------------------
//
// A pared-down ICommandBuffer specialization sufficient for
// `hello_d3d12_clear`. It supports begin/end + begin_render_pass
// (records LoadOp::kClear → ClearRenderTargetView and the surrounding
// COMMON↔RENDER_TARGET↔PRESENT transitions) + end_render_pass.
// Everything else surfaces a soft no-op or a NotImplemented Result;
// the PSO/draw surface lands in a later phase.

class D3D12CommandBuffer final : public cd::rhi::ICommandBuffer
{
public:
    D3D12CommandBuffer(D3D12Device* owner, ComPtr<ID3D12CommandAllocator> alloc, ComPtr<ID3D12GraphicsCommandList> list) noexcept
        : owner_ { owner }, alloc_ { std::move(alloc) }, list_ { std::move(list) }
    {}
    ~D3D12CommandBuffer() override = default;
    D3D12CommandBuffer(const D3D12CommandBuffer&) = delete;
    D3D12CommandBuffer& operator=(const D3D12CommandBuffer&) = delete;
    D3D12CommandBuffer(D3D12CommandBuffer&&) = delete;
    D3D12CommandBuffer& operator=(D3D12CommandBuffer&&) = delete;

    [[nodiscard]] ID3D12GraphicsCommandList* native() noexcept { return list_.Get(); }

    void begin() override
    {
        alloc_->Reset();
        list_->Reset(alloc_.Get(), nullptr);
        target_view_ = {};
        target_texture_ = {};
        bound_is_mesh_shader_ = false;
        // The prior frame's upload staging is no longer needed once the
        // allocator is safe to reset (engine waited on its fence).
        retained_uploads_.clear();
        // Reset per-recording debug-group depth so a recycled command buffer
        // (begin→record→end→submit→begin again) never inherits a stale depth
        // from a previous recording that left unmatched push_debug_group calls.
        // Without this reset the pop underflow-guard silently eats the first
        // EndEvent of a subsequent recording, causing PIX/RenderDoc event-tree
        // desync (phase1189 B5 fix).
        debug_group_depth_ = 0;
    }
    void end() override
    {
        // Final transition: RENDER_TARGET → PRESENT if we wrote to a
        // swapchain image. After end() the consumer queues a Present().
        if (target_texture_.value() != 0u)
        {
            if (auto* trec = owner_->find_texture(target_texture_))
            {
                D3D12_RESOURCE_BARRIER bar {};
                bar.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                bar.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
                bar.Transition.pResource = trec->resource.Get();
                bar.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
                bar.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
                bar.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
                list_->ResourceBarrier(1, &bar);
                trec->state = D3D12_RESOURCE_STATE_PRESENT;
            }
        }
        list_->Close();
    }

    void begin_render_pass(const cd::rhi::RenderPassBeginInfo& info) override
    {
        // D3 (phase1186): bind ALL color attachments + the optional depth
        // attachment, honouring each load-op — matching the Vulkan dynamic-
        // rendering reference (VulkanCommandBuffer::begin_rendering_). MRT
        // (count > 1) and depth testing both flow through here now; the
        // legacy single-RTV / null-DSV path is gone.
        //
        // Like the Vulkan path the *caller* owns most layout transitions via
        // explicit barrier() calls; we keep the historical implicit
        // COMMON/PRESENT -> RENDER_TARGET (and -> DEPTH_WRITE) transition for
        // the swapchain present path that hello_engine relies on, guarded so
        // it is a no-op when the resource is already in the right state.
        std::array<D3D12_CPU_DESCRIPTOR_HANDLE, 8> rtvs {};
        UINT rtv_count = 0;

        for (const auto& a : info.color_attachments)
        {
            if (rtv_count >= rtvs.size())
                break;  // D3D12 caps simultaneous render targets at 8.
            auto* vrec = owner_->find_texture_view(a.view);
            if (vrec == nullptr)
                continue;
            auto* trec = owner_->find_texture(vrec->parent);
            if (trec == nullptr)
                continue;

            // The first color attachment is the "primary" slot the present
            // path in end() reads back for the RENDER_TARGET -> PRESENT
            // transition (swapchain image).
            if (rtv_count == 0)
            {
                target_view_    = a.view;
                target_texture_ = vrec->parent;
            }

            if (trec->state != D3D12_RESOURCE_STATE_RENDER_TARGET)
            {
                D3D12_RESOURCE_BARRIER bar {};
                bar.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                bar.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
                bar.Transition.pResource = trec->resource.Get();
                bar.Transition.StateBefore = trec->state;
                bar.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
                bar.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
                list_->ResourceBarrier(1, &bar);
                trec->state = D3D12_RESOURCE_STATE_RENDER_TARGET;
            }

            const D3D12_CPU_DESCRIPTOR_HANDLE rtv = vrec->rtv_cpu;
            rtvs[rtv_count] = rtv;
            ++rtv_count;
            if (a.load_op == cd::rhi::LoadOp::kClear)
                list_->ClearRenderTargetView(rtv, a.clear_color.f32, 0, nullptr);
        }

        // Depth-stencil attachment (optional). The DSV CPU handle lives in
        // the view record's rtv_cpu slot (create_texture_view's is_depth
        // branch reuses that field). A null DSV is bound only when there is
        // genuinely no depth attachment.
        D3D12_CPU_DESCRIPTOR_HANDLE dsv {};
        bool has_dsv = false;
        if (info.depth_stencil != nullptr)
        {
            if (auto* dview = owner_->find_texture_view(info.depth_stencil->view))
            {
                if (auto* dtex = owner_->find_texture(dview->parent))
                {
                    if (dtex->state != D3D12_RESOURCE_STATE_DEPTH_WRITE)
                    {
                        D3D12_RESOURCE_BARRIER bar {};
                        bar.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                        bar.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
                        bar.Transition.pResource = dtex->resource.Get();
                        bar.Transition.StateBefore = dtex->state;
                        bar.Transition.StateAfter = D3D12_RESOURCE_STATE_DEPTH_WRITE;
                        bar.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
                        list_->ResourceBarrier(1, &bar);
                        dtex->state = D3D12_RESOURCE_STATE_DEPTH_WRITE;
                    }
                    dsv = dview->rtv_cpu;
                    has_dsv = true;

                    // Clear flags: depth and/or stencil, per the per-aspect
                    // load-op. Stencil clear only fires for formats that
                    // carry a stencil aspect.
                    D3D12_CLEAR_FLAGS clear_flags {};
                    if (info.depth_stencil->depth_load == cd::rhi::LoadOp::kClear)
                        clear_flags |= D3D12_CLEAR_FLAG_DEPTH;
                    if (info.depth_stencil->stencil_load == cd::rhi::LoadOp::kClear &&
                        dsv_format_has_stencil(dtex->format))
                        clear_flags |= D3D12_CLEAR_FLAG_STENCIL;
                    if (clear_flags != 0)
                    {
                        list_->ClearDepthStencilView(
                            dsv, clear_flags,
                            info.depth_stencil->clear.depth,
                            static_cast<UINT8>(info.depth_stencil->clear.stencil),
                            0, nullptr);
                    }
                }
            }
        }

        list_->OMSetRenderTargets(
            rtv_count, rtv_count > 0 ? rtvs.data() : nullptr,
            FALSE, has_dsv ? &dsv : nullptr);

        // D11 — capture the RESOLVED pass RTV/DSV state so a parallel-pass
        // recorder can re-bind it per lane. A D3D12 command list inherits NO
        // state, so each parallel lane must (re)set OMSetRenderTargets with
        // exactly these handles before it draws. We snapshot here (after the
        // clears + barriers have fired once on the primary) so the lanes use
        // LOAD semantics — they never re-clear.
        pass_state_.rtv_count = rtv_count;
        pass_state_.rtvs      = rtvs;
        pass_state_.has_dsv   = has_dsv;
        pass_state_.dsv       = dsv;
    }
    void end_render_pass() override {}

    // D11 — re-establish the captured render-pass output-merger state on THIS
    // command list. Called by D3D12ParallelPassRecorder at the head of every
    // lane replay because a freshly-recorded lane inherits no OM/RS state. On
    // the sequential-replay-onto-primary path this is a benign redundant set
    // (the primary already has it bound from begin_render_pass), but it makes
    // the per-lane state-isolation contract explicit and keeps the path robust
    // if a lane's pipeline mutates render-target/viewport state.
    void rebind_parallel_pass_state(const cd::rhi::Rect2D& render_area)
    {
        list_->OMSetRenderTargets(
            pass_state_.rtv_count,
            pass_state_.rtv_count > 0 ? pass_state_.rtvs.data() : nullptr,
            FALSE,
            pass_state_.has_dsv ? &pass_state_.dsv : nullptr);
        // D16 (parity): same NEGATIVE-HEIGHT Y-flip as set_viewport so the
        // implicit parallel-pass re-bind viewport matches Vulkan's NDC-Y-down
        // convention (anchor at the bottom edge, flip the height sign). Without
        // this, a parallel pass that does not re-issue set_viewport would render
        // un-flipped versus the serial path.
        D3D12_VIEWPORT vp {};
        vp.TopLeftX = static_cast<float>(render_area.offset.x);
        vp.TopLeftY = static_cast<float>(render_area.offset.y) +
                      static_cast<float>(render_area.extent.height);
        vp.Width    = static_cast<float>(render_area.extent.width);
        vp.Height   = -static_cast<float>(render_area.extent.height);
        vp.MinDepth = 0.0F;
        vp.MaxDepth = 1.0F;
        list_->RSSetViewports(1, &vp);
        D3D12_RECT sc {};
        sc.left   = render_area.offset.x;
        sc.top    = render_area.offset.y;
        sc.right  = render_area.offset.x + static_cast<LONG>(render_area.extent.width);
        sc.bottom = render_area.offset.y + static_cast<LONG>(render_area.extent.height);
        list_->RSSetScissorRects(1, &sc);
    }

    // D11 — parallel render pass over per-lane deferred command recording.
    // Definition is out-of-line (after D3D12ParallelPassRecorder) because the
    // recorder type is declared below this class.
    [[nodiscard]] std::unique_ptr<cd::rhi::IParallelPassRecorder>
    begin_parallel_render_pass(const cd::rhi::RenderPassBeginInfo& info,
                               std::uint32_t lane_count) override;

    // PSO + draw surface (Phase 14.C / v0.36.0). The rest of the
    // ICommandBuffer surface (compute dispatch, descriptor sets,
    // copies, barriers) remains no-op for v0.36.0 — wired by later
    // phases.
    void bind_graphics_pipeline(cd::rhi::GraphicsPipelineHandle h) override
    {
        if (auto* rec = owner_->find_graphics_pipeline(h))
        {
            list_->SetPipelineState(rec->pso.Get());
            if (auto* layout = owner_->find_pipeline_layout(rec->layout_handle))
                list_->SetGraphicsRootSignature(layout->root_sig.Get());
            // phase766 — mesh PSOs ignore IA topology; skip the call so we
            // don't generate spurious debug-layer chatter on mesh-only paths.
            if (!rec->is_mesh_shader)
                list_->IASetPrimitiveTopology(rec->d3d_topology);
            bound_compute_layout_ = {};
            bound_graphics_layout_ = rec->layout_handle;
            bound_graphics_pipeline_ = h;
            bound_is_mesh_shader_ = rec->is_mesh_shader;
        }
    }
    // phase466 — compute pipeline bind.
    void bind_compute_pipeline(cd::rhi::ComputePipelineHandle h) override
    {
        if (owner_ == nullptr) return;
        auto* rec = owner_->find_compute_pipeline(h);
        if (rec == nullptr) return;
        list_->SetPipelineState(rec->pso.Get());
        if (auto* layout = owner_->find_pipeline_layout(rec->layout_handle))
            list_->SetComputeRootSignature(layout->root_sig.Get());
        bound_compute_layout_  = rec->layout_handle;
        bound_graphics_layout_ = {};
        bound_graphics_pipeline_ = {};
    }
    void bind_descriptor_set(std::uint32_t set_index, cd::rhi::DescriptorSetHandle set) override
    {
        // Phase 15.B real implementation: copy this set's descriptors
        // into the per-frame GPU-visible heap and bind it as the root
        // descriptor table for parameter `set_index`.
        auto* rec = owner_->find_descriptor_set(set);
        if (rec == nullptr) return;
        // A sampler-only set (view_count == 0) has no CBV/SRV/UAV table to
        // bind — only the SAMPLER table below. Skip the view-heap copy then.
        D3D12_GPU_DESCRIPTOR_HANDLE gpu { 0 };
        if (rec->view_count > 0)
        {
            gpu = owner_->copy_set_to_gpu_heap(*rec);
            if (gpu.ptr == 0) return;
        }

        const bool is_compute = bound_compute_layout_.value() != 0u;

        // D2 — when the set carries bare dynamic samplers (kSampler bindings),
        // stage them into the shader-visible sampler RING and bind THAT heap
        // (D3D12 binds at most one SAMPLER heap per draw). A set with no dynamic
        // sampler keeps the prior behaviour BYTE-FOR-BYTE: bind the bindless
        // default-LINEAR sampler heap (classic combined samplers are served by
        // static samplers baked into the root signature, so the golden does not
        // move). Resolve the layout up front so both the heap choice and the
        // root-table indices use the SAME layout record.
        const auto* lrec = owner_->find_pipeline_layout(
            is_compute ? bound_compute_layout_ : bound_graphics_layout_);
        D3D12_GPU_DESCRIPTOR_HANDLE samp_gpu { 0 };
        UINT samp_param = ~UINT { 0 };
        if (rec->sampler_count > 0 && lrec != nullptr &&
            set_index < lrec->sampler_table_params.size() &&
            lrec->sampler_table_params[set_index] != ~std::uint32_t { 0 })
        {
            samp_gpu   = owner_->copy_set_samplers_to_gpu_heap(*rec);
            samp_param = static_cast<UINT>(lrec->sampler_table_params[set_index]);
        }

        // Bind the unified CBV/SRV/UAV heap (gpu_heap() and bindless_heap() now
        // return the SAME heap, so a later bind_bindless_texture_array in the
        // same draw does NOT swap it out — the B6 follow-up co-bind fix). The
        // SAMPLER heap that rides alongside is either this set's dynamic sampler
        // ring (D2) or the bindless default-LINEAR pool (preserved default).
        ID3D12DescriptorHeap* samp0 = (samp_gpu.ptr != 0)
            ? owner_->gpu_sampler_heap()
            : owner_->bindless_sampler_heap();
        if (samp0 != nullptr)
        {
            ID3D12DescriptorHeap* heaps[] = { owner_->gpu_heap(), samp0 };
            list_->SetDescriptorHeaps(2, heaps);
        }
        else
        {
            ID3D12DescriptorHeap* heaps[] = { owner_->gpu_heap() };
            list_->SetDescriptorHeaps(1, heaps);
        }

        // Resolve the CBV/SRV/UAV table's actual root-param index from the
        // layout (a sampler table emitted for an earlier set can shift the view
        // table off the set ordinal). Fall back to set_index when the layout /
        // mapping is unavailable — identical to the pre-D2 value for every set
        // whose root-param index already equals its ordinal (the golden path).
        UINT view_param = set_index;
        if (lrec != nullptr && set_index < lrec->table_params.size() &&
            lrec->table_params[set_index] != ~std::uint32_t { 0 })
            view_param = static_cast<UINT>(lrec->table_params[set_index]);

        // phase466 — route to compute or graphics based on the last-bound pipeline.
        if (is_compute)
        {
            if (gpu.ptr != 0)
                list_->SetComputeRootDescriptorTable(view_param, gpu);
            if (samp_gpu.ptr != 0)
                list_->SetComputeRootDescriptorTable(samp_param, samp_gpu);
        }
        else
        {
            if (gpu.ptr != 0)
                list_->SetGraphicsRootDescriptorTable(view_param, gpu);
            if (samp_gpu.ptr != 0)
                list_->SetGraphicsRootDescriptorTable(samp_param, samp_gpu);
        }
    }
    // D10 — point root param `set_index` at the array's GPU base inside the
    // unified heap's bindless sub-region. The bindless pool is NOW a sub-region
    // of the SAME shader-visible CBV/SRV/UAV heap the per-set ring uses
    // (phase1190 B6 follow-up), so this SetDescriptorHeaps call binds the identical
    // CBV/SRV/UAV heap that bind_descriptor_set bound — a draw that binds a classic
    // set-0 table AND this bindless set-1 array keeps BOTH live (no silent unbind).
    void bind_bindless_texture_array(std::uint32_t set_index,
                                     cd::rhi::BindlessTextureArrayHandle array) override
    {
        if (owner_ == nullptr) return;
        ID3D12DescriptorHeap* heap = owner_->bindless_heap();
        if (heap == nullptr) return;
        const auto gpu = owner_->bindless_array_gpu_base(array);
        if (gpu.ptr == 0) return;
        const bool is_compute = bound_compute_layout_.value() != 0u;
        // Bind the unified CBV/SRV/UAV heap AND its sampler heap together — D3D12
        // allows one CBV/SRV/UAV + one SAMPLER heap bound simultaneously, so the
        // bindless texture half (in the unified heap) and its sampler half are
        // both live for the draw.
        ID3D12DescriptorHeap* samp = owner_->bindless_sampler_heap();
        if (samp != nullptr)
        {
            ID3D12DescriptorHeap* heaps[] = { heap, samp };
            list_->SetDescriptorHeaps(2, heaps);
        }
        else
        {
            ID3D12DescriptorHeap* heaps[] = { heap };
            list_->SetDescriptorHeaps(1, heaps);
        }
        // Resolve the SRV table's actual root-parameter index from the bound
        // layout's table_params (an empty earlier set produces no table, so the
        // set ordinal != root-param index). Fall back to set_index when the
        // layout/mapping is unavailable.
        const auto* lrec = owner_->find_pipeline_layout(
            is_compute ? bound_compute_layout_ : bound_graphics_layout_);
        UINT srv_param = set_index;
        if (lrec != nullptr && set_index < lrec->table_params.size())
            srv_param = static_cast<UINT>(lrec->table_params[set_index]);
        // Point the SRV table param at the array's GPU base.
        if (is_compute)
            list_->SetComputeRootDescriptorTable(srv_param, gpu);
        else
            list_->SetGraphicsRootDescriptorTable(srv_param, gpu);
        // Point the bindless sampler table (if any) at the sampler heap base.
        if (lrec != nullptr &&
            lrec->bindless_sampler_param != ~std::uint32_t { 0 } &&
            samp != nullptr)
        {
            const D3D12_GPU_DESCRIPTOR_HANDLE samp_base =
                samp->GetGPUDescriptorHandleForHeapStart();
            if (is_compute)
                list_->SetComputeRootDescriptorTable(
                    lrec->bindless_sampler_param, samp_base);
            else
                list_->SetGraphicsRootDescriptorTable(
                    lrec->bindless_sampler_param, samp_base);
        }
    }
    void bind_vertex_buffer(std::uint32_t binding, cd::rhi::BufferHandle buffer, std::uint64_t offset) override
    {
        if (auto* buf = owner_->find_buffer(buffer))
        {
            D3D12_VERTEX_BUFFER_VIEW vbv {};
            vbv.BufferLocation = buf->resource->GetGPUVirtualAddress() + offset;
            vbv.SizeInBytes = static_cast<UINT>(buf->size - offset);
            // D6 (phase1187): StrideInBytes comes from the bound graphics
            // PSO's per-binding VertexBinding[].stride (captured at PSO
            // creation), not a hardcoded 24. The engine API doesn't pass
            // stride to bind_vertex_buffer — the PSO carries it — so we look
            // it up via the last-bound pipeline's `binding_strides` map keyed
            // by this VB slot. Handles multiple bindings with distinct
            // strides. Absent slot / no pipeline bound → 0 (degenerate VBV
            // that surfaces a miswire loudly instead of fetching wrong data).
            UINT stride = 0;
            if (auto* rec = owner_->find_graphics_pipeline(bound_graphics_pipeline_))
            {
                auto sit = rec->binding_strides.find(binding);
                if (sit != rec->binding_strides.end())
                    stride = static_cast<UINT>(sit->second);
            }
            vbv.StrideInBytes = stride;
            list_->IASetVertexBuffers(binding, 1, &vbv);
        }
    }
    void bind_index_buffer(cd::rhi::BufferHandle buffer, std::uint64_t offset, cd::rhi::IndexType type) override
    {
        if (auto* buf = owner_->find_buffer(buffer))
        {
            D3D12_INDEX_BUFFER_VIEW ibv {};
            ibv.BufferLocation = buf->resource->GetGPUVirtualAddress() + offset;
            ibv.SizeInBytes = static_cast<UINT>(buf->size - offset);
            ibv.Format = (type == cd::rhi::IndexType::kUInt16)
                ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT;
            list_->IASetIndexBuffer(&ibv);
        }
    }
    // phase466 — push constants via D3D12 root 32-bit constants slot.
    //
    // The PipelineLayout stores `push_constants_param` (root-param index)
    // and `push_constants_dwords` (max 32-bit-value count). We route
    // through SetGraphics/ComputeRoot32BitConstants based on which pipeline
    // type is currently bound. Vulkan offset is in BYTES; D3D12 takes a
    // DEST_OFFSET_IN_32BIT_VALUES (DWORD). For values beyond the 128B
    // Vulkan minimum-guarantee, the slot can hold up to ~60 DWORDs (240B),
    // far above the 128B push-constant baseline — large blocks are
    // accommodated up to the D3D12 root-cost budget (64 DWORDs total).
    void push_constants(cd::rhi::PipelineLayoutHandle layout,
                        cd::rhi::ShaderStage /*stages*/,
                        std::uint32_t offset,
                        std::uint32_t size,
                        const void*   data) override
    {
        if (owner_ == nullptr || data == nullptr || size == 0u) return;
        auto* lrec = owner_->find_pipeline_layout(layout);
        if (lrec == nullptr) return;
        if (lrec->push_constants_param == ~std::uint32_t { 0 }) return;

        const std::uint32_t dst_dword_offset = offset / 4u;
        const std::uint32_t num_dwords       = (size + 3u) / 4u;
        if (dst_dword_offset + num_dwords > lrec->push_constants_dwords)
        {
            // Caller exceeded declared range; clamp.
            const auto clamped = (lrec->push_constants_dwords > dst_dword_offset)
                ? (lrec->push_constants_dwords - dst_dword_offset)
                : 0u;
            if (clamped == 0u) return;
            // Re-evaluate locally so the call is still safe.
            if (bound_compute_layout_.value() != 0u)
            {
                list_->SetComputeRoot32BitConstants(
                    lrec->push_constants_param, clamped, data, dst_dword_offset);
            }
            else
            {
                list_->SetGraphicsRoot32BitConstants(
                    lrec->push_constants_param, clamped, data, dst_dword_offset);
            }
            return;
        }
        if (bound_compute_layout_.value() != 0u)
        {
            list_->SetComputeRoot32BitConstants(
                lrec->push_constants_param, num_dwords, data, dst_dword_offset);
        }
        else
        {
            list_->SetGraphicsRoot32BitConstants(
                lrec->push_constants_param, num_dwords, data, dst_dword_offset);
        }
    }
    void set_viewport(const cd::rhi::Viewport& vp) override
    {
        // D16 (cross-backend parity): the engine authors clip space in the
        // Vulkan convention (NDC +Y points DOWN in framebuffer space — the
        // reference backend). D3D12's NDC +Y points UP, so the SAME clip-space
        // triangle would rasterize VERTICALLY MIRRORED versus Vulkan. The
        // cross-API-standard cure is a NEGATIVE-HEIGHT viewport: anchor at the
        // bottom edge (TopLeftY = y + height) and flip the height sign so D3D12
        // maps NDC +Y downward too, matching Vulkan pixel-for-pixel. (D3D12 has
        // supported negative viewport height on all feature levels since the
        // Windows 10 Anniversary update; the engine's min target is well past
        // that.) This is the fix the D16 pixel-parity test surfaced — it makes
        // the D3D12 readback byte-flip-identical to the Vulkan reference.
        D3D12_VIEWPORT v {};
        v.TopLeftX = vp.x;
        v.TopLeftY = vp.y + vp.height;  // anchor at the bottom edge…
        v.Width = vp.width;
        v.Height = -vp.height;          // …and flip Y so +Y NDC goes downward
        v.MinDepth = vp.min_depth;
        v.MaxDepth = vp.max_depth;
        list_->RSSetViewports(1, &v);
    }
    void set_scissor(const cd::rhi::Rect2D& rect) override
    {
        D3D12_RECT r {};
        r.left = rect.offset.x;
        r.top = rect.offset.y;
        r.right = rect.offset.x + static_cast<LONG>(rect.extent.width);
        r.bottom = rect.offset.y + static_cast<LONG>(rect.extent.height);
        list_->RSSetScissorRects(1, &r);
    }
    void draw(std::uint32_t vertex_count, std::uint32_t instance_count,
              std::uint32_t first_vertex, std::uint32_t first_instance) override
    {
        list_->DrawInstanced(vertex_count, instance_count, first_vertex, first_instance);
    }
    void draw_indexed(std::uint32_t index_count, std::uint32_t instance_count,
                      std::uint32_t first_index, std::int32_t vertex_offset,
                      std::uint32_t first_instance) override
    {
        list_->DrawIndexedInstanced(index_count, instance_count, first_index,
                                    vertex_offset, first_instance);
    }
    // phase466 — compute dispatch (gates on bound_compute_layout_).
    void dispatch(std::uint32_t gx, std::uint32_t gy, std::uint32_t gz) override
    {
        if (gx == 0u || gy == 0u || gz == 0u) return;
        list_->Dispatch(gx, gy, gz);
    }
    // phase766 — mesh-shading dispatch via ID3D12GraphicsCommandList6::DispatchMesh.
    // Caller MUST have a mesh-shading PSO bound (bound_is_mesh_shader_)
    // and the device MUST report features().mesh_shader = true. We probe
    // for ID3D12GraphicsCommandList6 lazily; on hosts where the SDK runtime
    // lacks the interface (older Windows 10 builds), the cast fails and
    // we skip the call. This mirrors the Vulkan path which gates on
    // vkCmdDrawMeshTasksEXT being present in the dispatch table.
    void draw_mesh_tasks(std::uint32_t gx, std::uint32_t gy, std::uint32_t gz) override
    {
        if (!bound_is_mesh_shader_) return;
        if (gx == 0u || gy == 0u || gz == 0u) return;
        ComPtr<ID3D12GraphicsCommandList6> list6;
        if (FAILED(list_.As(&list6)) || !list6) return;
        list6->DispatchMesh(gx, gy, gz);
    }
    // phase466 — buffer-to-buffer copies via CopyBufferRegion.
    void copy_buffer(cd::rhi::BufferHandle src,
                     cd::rhi::BufferHandle dst,
                     std::span<const cd::rhi::BufferCopyRegion> regions) override
    {
        if (owner_ == nullptr) return;
        auto* src_b = owner_->find_buffer(src);
        auto* dst_b = owner_->find_buffer(dst);
        if (src_b == nullptr || dst_b == nullptr) return;
        for (const auto& r : regions)
        {
            UINT64 sz = r.size;
            if (sz == 0u)
            {
                // size 0 = "rest of the source buffer" (clamped at 0).
                sz = (src_b->size > r.src_offset) ? src_b->size - r.src_offset
                                                  : 0u;
            }
            if (sz == 0u) continue;
            list_->CopyBufferRegion(
                dst_b->resource.Get(), r.dst_offset,
                src_b->resource.Get(), r.src_offset,
                sz);
        }
    }
    // phase1185 (D1+D15) — texture UPLOAD. The caller owns the surrounding
    // barriers (dst already in COPY_DEST / kTransferDst) exactly like the
    // Vulkan backend's vkCmdCopyBufferToImage. D3D12 placed-footprint copies
    // REQUIRE a 256-byte-aligned source row pitch
    // (D3D12_TEXTURE_DATA_PITCH_ALIGNMENT), while the IDevice contract feeds
    // a TIGHTLY-PACKED source buffer (bufferRowLength = 0, like Vulkan). We
    // therefore allocate a per-copy UPLOAD staging buffer sized from
    // GetCopyableFootprints, re-pitch the tightly-packed source rows into it,
    // then CopyTextureRegion(dst texture <- placed-footprint staging). The
    // staging ComPtr is retained until the next begin() so it outlives the
    // GPU execution of this command list.
    //
    // SCOPE: correctness for uncompressed single-plane formats (RGBA8 / BGRA8
    // / R8 / etc.). Block-compressed (BC*) and multi-plane (depth+stencil)
    // copies are NOT handled here — GetCopyableFootprints would report a
    // block/plane-shaped footprint that the tight-row re-pitch loop below does
    // not honour, so they are rejected by a no-op rather than silently
    // producing wrong bytes (see the bpp==0 guard).
    void copy_buffer_to_image(cd::rhi::BufferHandle src,
                              cd::rhi::TextureHandle dst,
                              std::span<const cd::rhi::BufferImageCopyRegion> regions) override
    {
        if (owner_ == nullptr || regions.empty()) return;
        auto* src_b = owner_->find_buffer(src);
        auto* dst_t = owner_->find_texture(dst);
        if (src_b == nullptr || dst_t == nullptr) return;
        // Source must be CPU-mappable (UPLOAD/READBACK) so we can re-pitch its
        // bytes into the aligned staging. A kGpuOnly source has no CPU pointer;
        // that path would need a GPU-side buffer→buffer re-pitch (out of scope).
        if (src_b->heap_type != D3D12_HEAP_TYPE_UPLOAD &&
            src_b->heap_type != D3D12_HEAP_TYPE_READBACK)
            return;

        ID3D12Device* device = owner_->native_device();
        const D3D12_RESOURCE_DESC tex_desc = dst_t->resource->GetDesc();
        const UINT mip_levels = std::max<UINT>(1u, tex_desc.MipLevels);

        // Map the source buffer once; all regions read from it.
        void* src_mapped = nullptr;
        const D3D12_RANGE src_read { 0, static_cast<SIZE_T>(src_b->size) };
        if (FAILED(src_b->resource->Map(0, &src_read, &src_mapped)) ||
            src_mapped == nullptr)
            return;
        const auto* src_base = static_cast<const std::uint8_t*>(src_mapped);

        for (const auto& r : regions)
        {
            // Region-shaped single-subresource footprint.
            D3D12_RESOURCE_DESC region_desc = tex_desc;
            region_desc.Width  = r.image_extent.width;
            region_desc.Height = r.image_extent.height;
            region_desc.DepthOrArraySize =
                static_cast<UINT16>(std::max<std::uint32_t>(1u, r.image_extent.depth));
            region_desc.MipLevels = 1;

            D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint {};
            UINT   row_count   = 0;
            UINT64 row_bytes   = 0;
            UINT64 total_bytes = 0;
            device->GetCopyableFootprints(&region_desc, 0, 1, 0,
                                          &footprint, &row_count, &row_bytes,
                                          &total_bytes);
            // Guard BC / multi-plane (and degenerate) footprints: the tight
            // re-pitch below assumes contiguous row_bytes per row.
            if (row_count == 0 || row_bytes == 0 || total_bytes == 0)
                continue;

            // Source bounds guard (symmetric with the download de-pitch): the
            // re-pitch loop reads src_base + buffer_offset + flat*row_bytes for
            // every tight row/slice. Reject if that TIGHT range would overrun
            // the source buffer rather than perform an OOB read.
            const UINT   depth_src = std::max<UINT>(1u, footprint.Footprint.Depth);
            const UINT64 tight_src =
                row_bytes * static_cast<UINT64>(row_count) * depth_src;
            if (r.buffer_offset + tight_src > src_b->size)
                continue;

            ComPtr<ID3D12Resource> staging;
            D3D12_HEAP_PROPERTIES up_heap {};
            up_heap.Type = D3D12_HEAP_TYPE_UPLOAD;
            D3D12_RESOURCE_DESC st_desc {};
            st_desc.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
            st_desc.Width            = total_bytes;
            st_desc.Height           = 1;
            st_desc.DepthOrArraySize = 1;
            st_desc.MipLevels        = 1;
            st_desc.Format           = DXGI_FORMAT_UNKNOWN;
            st_desc.SampleDesc       = { 1, 0 };
            st_desc.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            if (FAILED(device->CreateCommittedResource(
                    &up_heap, D3D12_HEAP_FLAG_NONE, &st_desc,
                    D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                    IID_PPV_ARGS(&staging))))
                continue;

            void* st_mapped = nullptr;
            const D3D12_RANGE no_read { 0, 0 };
            if (FAILED(staging->Map(0, &no_read, &st_mapped)) ||
                st_mapped == nullptr)
                continue;
            // Re-pitch: tight source rows (row_bytes each, starting at
            // buffer_offset) -> aligned staging rows (RowPitch each). depth
            // slices are stacked tightly on the source side too.
            auto* st_base = static_cast<std::uint8_t*>(st_mapped);
            const UINT64 row_pitch  = footprint.Footprint.RowPitch;
            const UINT   rows_per_slice = row_count;
            for (UINT slice = 0; slice < depth_src; ++slice)
            {
                for (UINT row = 0; row < rows_per_slice; ++row)
                {
                    const UINT64 flat = static_cast<UINT64>(slice) * rows_per_slice + row;
                    std::memcpy(
                        st_base + flat * row_pitch,
                        src_base + r.buffer_offset +
                            flat * row_bytes,
                        static_cast<std::size_t>(row_bytes));
                }
            }
            staging->Unmap(0, nullptr);

            D3D12_TEXTURE_COPY_LOCATION dst_loc {};
            dst_loc.pResource        = dst_t->resource.Get();
            dst_loc.Type             = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            dst_loc.SubresourceIndex = r.mip_level + (r.base_layer * mip_levels);

            D3D12_TEXTURE_COPY_LOCATION src_loc {};
            src_loc.pResource       = staging.Get();
            src_loc.Type            = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            src_loc.PlacedFootprint = footprint;

            list_->CopyTextureRegion(
                &dst_loc,
                static_cast<UINT>(r.image_offset.x),
                static_cast<UINT>(r.image_offset.y),
                static_cast<UINT>(r.image_offset.z),
                &src_loc, nullptr);

            retained_uploads_.push_back(std::move(staging));
        }
        src_b->resource->Unmap(0, nullptr);
    }
    // phase1185 (D2) — cmd-level texture DOWNLOAD (in-frame readback). The RHI
    // BufferImageCopyRegion contract is Vulkan-style TIGHT (bufferRowLength = 0):
    // the dst buffer must receive contiguous rows of exactly row_bytes, with no
    // 256-byte padding, so a tight-row consumer (e.g. GoldenCapture's
    // w*h*bpp-sized readback) lands byte-identical to vkCmdCopyImageToBuffer.
    //
    // D3D12 CopyTextureRegion into a placed-footprint buffer can ONLY write
    // 256-aligned (D3D12_TEXTURE_DATA_PITCH_ALIGNMENT) rows, so a direct
    // texture->dst copy would emit PITCHED rows — wrong layout for the tight
    // consumer whenever width*bpp % 256 != 0, and potentially larger than the
    // tightly-sized dst (silent skip on the bounds guard, leaving stale data).
    //
    // FIX (deferred-safe, no compute shader): record
    //   1) CopyTextureRegion(src texture -> RETAINED intermediate PITCHED buffer)
    //   2) a per-(slice,row) loop of CopyBufferRegion(dst tight-row offset <-
    //      intermediate pitched-row offset, row_bytes each)
    // so the dst ends up with TIGHT rows. The intermediate buffer is retained
    // for the command list's lifetime (same pattern as retained_uploads_) so it
    // outlives the deferred GPU execution. The bounds check below uses the TIGHT
    // total (row_bytes * row_count * depth), not the pitched total.
    //
    // The caller owns the surrounding barriers (src already in COPY_SOURCE /
    // kTransferSrc), symmetric with copy_buffer_to_image and Vulkan.
    //
    // SCOPE: same uncompressed single-plane formats as the upload path; BC /
    // multi-plane footprints are rejected by the bpp/row guard.
    void copy_image_to_buffer(cd::rhi::TextureHandle src,
                              cd::rhi::BufferHandle dst,
                              std::span<const cd::rhi::BufferImageCopyRegion> regions) override
    {
        if (owner_ == nullptr || regions.empty()) return;
        auto* src_t = owner_->find_texture(src);
        auto* dst_b = owner_->find_buffer(dst);
        if (src_t == nullptr || dst_b == nullptr) return;

        ID3D12Device* device = owner_->native_device();
        const D3D12_RESOURCE_DESC tex_desc = src_t->resource->GetDesc();
        const UINT mip_levels = std::max<UINT>(1u, tex_desc.MipLevels);

        for (const auto& r : regions)
        {
            D3D12_RESOURCE_DESC region_desc = tex_desc;
            region_desc.Width  = r.image_extent.width;
            region_desc.Height = r.image_extent.height;
            region_desc.DepthOrArraySize =
                static_cast<UINT16>(std::max<std::uint32_t>(1u, r.image_extent.depth));
            region_desc.MipLevels = 1;

            D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint {};
            UINT   row_count   = 0;
            UINT64 row_bytes   = 0;
            UINT64 total_bytes = 0;  // PITCHED total (256-aligned rows).
            device->GetCopyableFootprints(&region_desc, 0, 1, 0,
                                          &footprint, &row_count, &row_bytes,
                                          &total_bytes);
            if (row_count == 0 || row_bytes == 0 || total_bytes == 0)
                continue;

            const UINT64 row_pitch = footprint.Footprint.RowPitch;
            const UINT   depth     = std::max<UINT>(1u, footprint.Footprint.Depth);
            const UINT64 tight_total =
                row_bytes * static_cast<UINT64>(row_count) * depth;

            // The dst buffer must hold the TIGHT rows starting at buffer_offset
            // (the Vulkan contract size), not the pitched footprint total.
            if (r.buffer_offset + tight_total > dst_b->size)
                continue;

            // Step 1: copy the texture into a retained PITCHED intermediate
            // buffer (footprint anchored at 0). A DEFAULT-heap buffer is fine —
            // the de-pitch CopyBufferRegion is a GPU-side buffer->buffer copy.
            ComPtr<ID3D12Resource> intermediate;
            D3D12_HEAP_PROPERTIES def_heap {};
            def_heap.Type = D3D12_HEAP_TYPE_DEFAULT;
            D3D12_RESOURCE_DESC mid_desc {};
            mid_desc.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
            mid_desc.Width            = total_bytes;
            mid_desc.Height           = 1;
            mid_desc.DepthOrArraySize = 1;
            mid_desc.MipLevels        = 1;
            mid_desc.Format           = DXGI_FORMAT_UNKNOWN;
            mid_desc.SampleDesc       = { 1, 0 };
            mid_desc.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            if (FAILED(device->CreateCommittedResource(
                    &def_heap, D3D12_HEAP_FLAG_NONE, &mid_desc,
                    D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                    IID_PPV_ARGS(&intermediate))))
                continue;

            footprint.Offset = 0;

            D3D12_TEXTURE_COPY_LOCATION src_loc {};
            src_loc.pResource        = src_t->resource.Get();
            src_loc.Type             = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            src_loc.SubresourceIndex = r.mip_level + (r.base_layer * mip_levels);

            D3D12_TEXTURE_COPY_LOCATION mid_loc {};
            mid_loc.pResource       = intermediate.Get();
            mid_loc.Type            = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            mid_loc.PlacedFootprint = footprint;

            const D3D12_BOX src_box {
                .left   = static_cast<UINT>(r.image_offset.x),
                .top    = static_cast<UINT>(r.image_offset.y),
                .front  = static_cast<UINT>(r.image_offset.z),
                .right  = static_cast<UINT>(r.image_offset.x) + r.image_extent.width,
                .bottom = static_cast<UINT>(r.image_offset.y) + r.image_extent.height,
                .back   = static_cast<UINT>(r.image_offset.z) +
                          std::max<std::uint32_t>(1u, r.image_extent.depth),
            };
            list_->CopyTextureRegion(&mid_loc, 0, 0, 0, &src_loc, &src_box);

            // Step 2: barrier the intermediate COPY_DEST -> COPY_SOURCE, then
            // de-pitch each row into the dst at TIGHT stride.
            D3D12_RESOURCE_BARRIER to_src {};
            to_src.Type  = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            to_src.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
            to_src.Transition.pResource   = intermediate.Get();
            to_src.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
            to_src.Transition.StateAfter  = D3D12_RESOURCE_STATE_COPY_SOURCE;
            to_src.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            list_->ResourceBarrier(1, &to_src);

            for (UINT slice = 0; slice < depth; ++slice)
            {
                for (UINT row = 0; row < row_count; ++row)
                {
                    const UINT64 flat =
                        static_cast<UINT64>(slice) * row_count + row;
                    list_->CopyBufferRegion(
                        dst_b->resource.Get(),
                        r.buffer_offset + flat * row_bytes,
                        intermediate.Get(),
                        flat * row_pitch,
                        row_bytes);
                }
            }

            retained_uploads_.push_back(std::move(intermediate));
        }
    }
    // phase466 — explicit state-transition barriers. Vulkan ResourceState
    // is mapped to the matching D3D12_RESOURCE_STATES bitmask; we batch
    // all transitions into a single ResourceBarrier call.
    void barrier(std::span<const cd::rhi::BufferBarrier> buffer_barriers,
                 std::span<const cd::rhi::TextureBarrier> texture_barriers) override
    {
        if (owner_ == nullptr) return;
        if (buffer_barriers.empty() && texture_barriers.empty()) return;

        std::vector<D3D12_RESOURCE_BARRIER> bars;
        bars.reserve(buffer_barriers.size() + texture_barriers.size());

        auto rs_to_d3d12 = [](cd::rhi::ResourceState s) noexcept -> D3D12_RESOURCE_STATES {
            using R = cd::rhi::ResourceState;
            switch (s)
            {
                case R::kUndefined:        return D3D12_RESOURCE_STATE_COMMON;
                case R::kCommon:           return D3D12_RESOURCE_STATE_COMMON;
                case R::kVertexBuffer:     return D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
                case R::kIndexBuffer:      return D3D12_RESOURCE_STATE_INDEX_BUFFER;
                case R::kConstantBuffer:   return D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
                case R::kShaderResource:   return D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
                                               | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
                case R::kUnorderedAccess:  return D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
                case R::kColorAttachment:  return D3D12_RESOURCE_STATE_RENDER_TARGET;
                case R::kDepthRead:        return D3D12_RESOURCE_STATE_DEPTH_READ;
                case R::kDepthWrite:       return D3D12_RESOURCE_STATE_DEPTH_WRITE;
                case R::kTransferSrc:      return D3D12_RESOURCE_STATE_COPY_SOURCE;
                case R::kTransferDst:      return D3D12_RESOURCE_STATE_COPY_DEST;
                case R::kPresent:          return D3D12_RESOURCE_STATE_PRESENT;
                case R::kIndirectArgument: return D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;
            }
            return D3D12_RESOURCE_STATE_COMMON;
        };

        for (const auto& b : buffer_barriers)
        {
            auto* br = owner_->find_buffer(b.buffer);
            if (br == nullptr) continue;
            D3D12_RESOURCE_BARRIER bb {};
            bb.Type  = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            bb.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
            bb.Transition.pResource   = br->resource.Get();
            bb.Transition.StateBefore = rs_to_d3d12(b.from);
            bb.Transition.StateAfter  = rs_to_d3d12(b.to);
            bb.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            if (bb.Transition.StateBefore != bb.Transition.StateAfter)
                bars.push_back(bb);
        }
        for (const auto& t : texture_barriers)
        {
            auto* tr = owner_->find_texture(t.texture);
            if (tr == nullptr) continue;
            const D3D12_RESOURCE_STATES after = rs_to_d3d12(t.to);

            // D-MIPSTATE (Backend-to-100 Wave 1): honor the barrier's
            // subresource range. The historical code ALWAYS used
            // ALL_SUBRESOURCES, so a per-mip transition (read mip N as a
            // shader resource while writing mip N+1 as a render target — the
            // mip-generation pattern) was impossible: the whole resource
            // flipped to one state, the debug layer flagged the mismatch, and
            // a mip-gen pass produced wrong pixels. Vulkan tracks layout per
            // VkImageSubresourceRange; mirror that here.
            const std::uint32_t total_mips   = tr->mip_levels;
            const std::uint32_t total_layers = tr->array_layers;
            const std::uint32_t base_mip   = t.range.base_mip;
            const std::uint32_t base_layer = t.range.base_layer;
            const std::uint32_t mip_count =
                (t.range.mip_count == 0u)
                    ? (total_mips - std::min(base_mip, total_mips))
                    : t.range.mip_count;
            const std::uint32_t layer_count =
                (t.range.layer_count == 0u)
                    ? (total_layers - std::min(base_layer, total_layers))
                    : t.range.layer_count;
            const bool whole_resource =
                base_mip == 0u && base_layer == 0u &&
                mip_count >= total_mips && layer_count >= total_layers;

            if (whole_resource)
            {
                if (tr->subresource_states.empty())
                {
                    // Fast path — the resource is whole-resource-coherent, so
                    // one ALL_SUBRESOURCES barrier suffices and `state` alone
                    // stays authoritative.
                    D3D12_RESOURCE_BARRIER tb {};
                    tb.Type  = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                    tb.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
                    tb.Transition.pResource   = tr->resource.Get();
                    tb.Transition.StateBefore = tr->state;
                    tb.Transition.StateAfter  = after;
                    tb.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
                    if (tb.Transition.StateBefore != tb.Transition.StateAfter)
                    {
                        bars.push_back(tb);
                        tr->state = after;
                    }
                    continue;
                }
                // The resource has DIVERGENT per-subresource states from an
                // earlier subset barrier. A single ALL_SUBRESOURCES barrier
                // with `state` as the before-state would lie about the
                // subresources that diverged — emit one barrier PER
                // subresource from its tracked state, then re-collapse to a
                // coherent whole-resource state (`state` = after, drop map).
                for (std::uint32_t layer = 0; layer < total_layers; ++layer)
                {
                    for (std::uint32_t mip = 0; mip < total_mips; ++mip)
                    {
                        const std::uint32_t sub = mip + layer * total_mips;
                        const D3D12_RESOURCE_STATES before =
                            tr->subresource_states[sub];
                        if (before == after) continue;
                        D3D12_RESOURCE_BARRIER tb {};
                        tb.Type  = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                        tb.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
                        tb.Transition.pResource   = tr->resource.Get();
                        tb.Transition.StateBefore = before;
                        tb.Transition.StateAfter  = after;
                        tb.Transition.Subresource = sub;
                        bars.push_back(tb);
                    }
                }
                tr->state = after;
                tr->subresource_states.clear();
                continue;
            }

            // Subset path — materialise the per-subresource state map (seeded
            // from the whole-resource state) and emit one barrier per
            // (mip, layer) in the range, using the D3D12 subresource index
            // = mip + layer * mip_levels.
            if (tr->subresource_states.empty())
            {
                tr->subresource_states.assign(
                    static_cast<std::size_t>(total_mips) * total_layers,
                    tr->state);
            }
            const std::uint32_t mip_end =
                std::min(base_mip + mip_count, total_mips);
            const std::uint32_t layer_end =
                std::min(base_layer + layer_count, total_layers);
            for (std::uint32_t layer = base_layer; layer < layer_end; ++layer)
            {
                for (std::uint32_t mip = base_mip; mip < mip_end; ++mip)
                {
                    const std::uint32_t sub = mip + layer * total_mips;
                    const D3D12_RESOURCE_STATES before =
                        tr->subresource_states[sub];
                    if (before == after) continue;
                    D3D12_RESOURCE_BARRIER tb {};
                    tb.Type  = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                    tb.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
                    tb.Transition.pResource   = tr->resource.Get();
                    tb.Transition.StateBefore = before;
                    tb.Transition.StateAfter  = after;
                    tb.Transition.Subresource = sub;
                    bars.push_back(tb);
                    tr->subresource_states[sub] = after;
                }
            }
        }
        if (!bars.empty())
            list_->ResourceBarrier(static_cast<UINT>(bars.size()), bars.data());
    }
    // ---- D14: PIX/RenderDoc debug groups ----------------------------------
    //
    // The WinPixEventRuntime header/lib is not vendored, so we use the raw
    // PIX op that ships in d3d12.h itself: ID3D12GraphicsCommandList::
    // BeginEvent / EndEvent. With metadata == PIX_EVENT_ANSI_VERSION (1) the
    // payload is a null-terminated ANSI string; this is exactly the encoding
    // PIX / RenderDoc / Nsight decode when no event runtime is linked. The
    // call is a safe no-op on a tool-less run (the driver ignores it), so it
    // NEVER crashes. A depth counter keeps push/pop balanced and guarantees
    // we never call EndEvent more than BeginEvent (mirrors the Vulkan
    // backend's null-function-pointer guard).
    void push_debug_group(std::string_view name) override
    {
        if (list_ == nullptr) return;
        // PIX_EVENT_ANSI_VERSION == 1 (from pix3.h). Defined locally because
        // WinPixEventRuntime is not a build dependency.
        constexpr UINT kPixEventAnsiVersion = 1u;
        // BeginEvent consumes the payload synchronously at record time, so a
        // local null-terminated copy is sufficient and safe.
        const std::string label { name };
        list_->BeginEvent(
            kPixEventAnsiVersion,
            label.c_str(),
            static_cast<UINT>(label.size() + 1));  // include NUL terminator
        ++debug_group_depth_;
    }
    void pop_debug_group() override
    {
        if (list_ == nullptr || debug_group_depth_ == 0) return;
        list_->EndEvent();
        --debug_group_depth_;
    }
    [[nodiscard]] std::uint32_t debug_group_depth() const noexcept override
    {
        return debug_group_depth_;
    }

    // ---- DXR AS build (REAL — phase466 v0.99.93 M4-parity-closeout) -------
    //
    // phase466 wires cached inputs: BLAS replays the stored geometry desc
    // array; TLAS feeds the stored instance-desc upload buffer GVA. Both
    // emit a UAV barrier so subsequent reads (TLAS reading BLAS result,
    // raygen reading TLAS result) wait for the build to drain.
    void build_acceleration_structure(cd::rhi::AccelStructureHandle h) override
    {
        if (owner_ == nullptr) return;
        auto* rec = owner_->find_accel(h);
        if (rec == nullptr) return;

        // Get the command-list-4 interface (DXR build entry point).
        ComPtr<ID3D12GraphicsCommandList4> list4;
        if (FAILED(list_.As(&list4)) || !list4) return;

        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC bd {};
        bd.DestAccelerationStructureData    = rec->result_gva;
        bd.ScratchAccelerationStructureData = rec->scratch_gva;
        bd.Inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
        bd.Inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;

        if (rec->kind == cd::rhi::AccelStructureKind::kBottomLevel)
        {
            bd.Inputs.Type           = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
            bd.Inputs.NumDescs       = static_cast<UINT>(rec->blas_geos.size());
            bd.Inputs.pGeometryDescs = rec->blas_geos.empty()
                                       ? nullptr
                                       : rec->blas_geos.data();
        }
        else  // TLAS
        {
            bd.Inputs.Type          = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
            bd.Inputs.NumDescs      = rec->num_descs;
            bd.Inputs.InstanceDescs = rec->tlas_instances_gva;
        }

        list4->BuildRaytracingAccelerationStructure(&bd, 0, nullptr);

        // UAV barrier on the result buffer so subsequent reads (e.g. a
        // TLAS build that consumes a BLAS result) wait for this build.
        D3D12_RESOURCE_BARRIER bar {};
        bar.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
        bar.UAV.pResource = rec->result.Get();
        list_->ResourceBarrier(1, &bar);
    }

    // ---- DXR command path: D8 bind_rt_pipeline ----------------------------
    //
    // Bind a ray-tracing state object created by create_rt_pipeline. The DXR
    // contract differs from graphics/compute: an RTPSO is an
    // ID3D12StateObject (NOT an ID3D12PipelineState), so it is bound via
    // ID3D12GraphicsCommandList4::SetPipelineState1 — SetPipelineState would
    // reject the object. The RTPSO's GLOBAL root signature is a subobject of
    // the state object itself (D3D12_GLOBAL_ROOT_SIGNATURE, set at creation),
    // so DXR resource bindings are still driven by SetComputeRootSignature +
    // SetComputeRoot* / SetDescriptorHeaps (the same root-binding model
    // compute uses — bind_descriptor_set already routes to the compute root
    // path). We therefore do NOT re-set a root signature here; binding the
    // state object is sufficient and mirrors the Vulkan reference
    // (vkCmdBindPipeline with VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR).
    //
    // Mirrors the Vulkan backend's null-function-pointer guard: if the
    // runtime list does not expose ID3D12GraphicsCommandList4 (no DXR), the
    // QueryInterface fails and this is a typed no-op — never a crash, never a
    // silent success that pretends a trace was bound.
    void bind_rt_pipeline(cd::rhi::RtPipelineHandle h) override
    {
        if (owner_ == nullptr) return;
        auto* rec = owner_->find_rt_pipeline(h);
        if (rec == nullptr || !rec->state_obj) return;
        ComPtr<ID3D12GraphicsCommandList4> list4;
        if (FAILED(list_.As(&list4)) || !list4) return;
        list4->SetPipelineState1(rec->state_obj.Get());
        // DXR root arguments are bound through the COMPUTE root-binding model.
        // The RTPSO's global root signature is a state-object subobject, but
        // the list still needs SetComputeRootSignature(global) for a later
        // SetComputeRootDescriptorTable (issued by bind_descriptor_set) to
        // land — otherwise the table binds against a stale/empty root sig.
        // Marking bound_compute_layout_ routes bind_descriptor_set to the
        // compute path (mirrors how bind_compute_pipeline behaves).
        if (rec->global_root_sig)
        {
            list_->SetComputeRootSignature(rec->global_root_sig.Get());
            bound_compute_layout_  = rec->layout_handle;
            bound_graphics_layout_ = {};
            bound_graphics_pipeline_ = {};
        }
    }

    // ---- DXR command path: D7 dispatch_rays --------------------------------
    //
    // Build a D3D12_DISPATCH_RAYS_DESC from the SBT regions and trace.
    //
    // The engine's SbtRegion mirrors VkStridedDeviceAddressRegionKHR
    // {buffer, offset, stride_bytes, size_bytes}; we resolve each region's
    // GPU virtual address as (buffer base GVA + offset). The four DXR table
    // fields map as:
    //   * RayGenerationShaderRecord {StartAddress, SizeInBytes}  -- no stride
    //     (exactly one ray-gen record per dispatch); StartAddress must be
    //     D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT (64)-aligned, which the
    //     SBT builder guarantees (rt_shader_group_base_alignment() == 64).
    //   * MissShaderTable / HitGroupTable / CallableShaderTable
    //     {StartAddress, SizeInBytes, StrideInBytes} -- the per-record stride
    //     is the SBT record size (handle 32 B rounded up to the 64-B record
    //     alignment by the builder), passed straight through from
    //     SbtRegion::stride_bytes so the desc matches EXACTLY how the SBT was
    //     authored. A region whose buffer is invalid is skipped (left zeroed),
    //     same policy as the Vulkan resolve().
    //
    // Width/Height/Depth come from the dispatch dims. Requires
    // ID3D12GraphicsCommandList4::DispatchRays; on a non-DXR list the
    // QueryInterface fails and this is a typed no-op (never a crash).
    void dispatch_rays(const cd::rhi::DispatchRaysDesc& desc) override
    {
        if (owner_ == nullptr) return;
        ComPtr<ID3D12GraphicsCommandList4> list4;
        if (FAILED(list_.As(&list4)) || !list4) return;

        auto region_gva = [this](const cd::rhi::SbtRegion& r) -> D3D12_GPU_VIRTUAL_ADDRESS {
            if (!r.buffer.is_valid()) return 0;
            auto* b = owner_->find_buffer(r.buffer);
            if (b == nullptr || !b->resource) return 0;
            return b->resource->GetGPUVirtualAddress() + r.offset;
        };

        D3D12_DISPATCH_RAYS_DESC drd {};

        // Ray-gen: exactly one record, so no stride field.
        const auto rg = region_gva(desc.raygen);
        if (rg != 0)
        {
            drd.RayGenerationShaderRecord.StartAddress = rg;
            drd.RayGenerationShaderRecord.SizeInBytes  = desc.raygen.size_bytes;
        }

        // Miss / hit-group / callable tables carry a per-record stride.
        const auto ms = region_gva(desc.miss);
        if (ms != 0)
        {
            drd.MissShaderTable.StartAddress  = ms;
            drd.MissShaderTable.SizeInBytes   = desc.miss.size_bytes;
            drd.MissShaderTable.StrideInBytes = desc.miss.stride_bytes;
        }
        const auto hi = region_gva(desc.hit);
        if (hi != 0)
        {
            drd.HitGroupTable.StartAddress  = hi;
            drd.HitGroupTable.SizeInBytes   = desc.hit.size_bytes;
            drd.HitGroupTable.StrideInBytes = desc.hit.stride_bytes;
        }
        const auto ca = region_gva(desc.callable);
        if (ca != 0)
        {
            drd.CallableShaderTable.StartAddress  = ca;
            drd.CallableShaderTable.SizeInBytes   = desc.callable.size_bytes;
            drd.CallableShaderTable.StrideInBytes = desc.callable.stride_bytes;
        }

        drd.Width  = desc.width;
        drd.Height = desc.height;
        drd.Depth  = desc.depth;

        list4->DispatchRays(&drd);
    }

    // ---- DXR command path: D9 acceleration_structure_barrier ---------------
    //
    // Make a BLAS/TLAS build visible to a subsequent build or trace on the
    // same command list. The Vulkan reference uses a memory barrier between
    // ACCELERATION_STRUCTURE_BUILD stages; the D3D12 equivalent is a global
    // UAV barrier (pResource == nullptr) — AS results live in UAV-state
    // DEFAULT-heap buffers, so a UAV barrier orders every prior AS write
    // before every subsequent AS read/write. This complements the
    // per-build UAV barrier build_acceleration_structure already emits on the
    // result buffer: callers that rebuild a BLAS in-place every frame and
    // reference it from a TLAS rebuilt later in the SAME submission emit this
    // between the two builds (the explicit barrier the IDevice contract
    // documents for the skinned-mesh case).
    //
    // A null/UAV barrier is always valid on a direct command list, so this is
    // safe on any device — there is no DXR feature gate to fail here (the gate
    // is on the AS build / trace, not on the global memory barrier).
    void acceleration_structure_barrier() override
    {
        if (list_ == nullptr) return;
        D3D12_RESOURCE_BARRIER bar {};
        bar.Type          = D3D12_RESOURCE_BARRIER_TYPE_UAV;
        bar.Flags         = D3D12_RESOURCE_BARRIER_FLAG_NONE;
        bar.UAV.pResource = nullptr;  // global UAV barrier
        list_->ResourceBarrier(1, &bar);
    }

private:
    D3D12Device* owner_ { nullptr };
    ComPtr<ID3D12CommandAllocator> alloc_;
    ComPtr<ID3D12GraphicsCommandList> list_;
    cd::rhi::TextureViewHandle target_view_ {};
    cd::rhi::TextureHandle target_texture_ {};
    // phase466 — last-bound pipeline-layout handles so push_constants and
    // bind_descriptor_set can pick Graphics vs Compute root-signature
    // entry point without an additional API surface change.
    cd::rhi::PipelineLayoutHandle bound_graphics_layout_ {};
    cd::rhi::PipelineLayoutHandle bound_compute_layout_  {};
    // D6 (phase1187) — last-bound graphics PSO so bind_vertex_buffer can
    // look up the per-binding vertex stride captured at PSO creation.
    cd::rhi::GraphicsPipelineHandle bound_graphics_pipeline_ {};
    // phase766 — true after bind_graphics_pipeline on a mesh-shading PSO.
    // draw_mesh_tasks consults this flag before issuing DispatchMesh.
    bool bound_is_mesh_shader_ { false };
    // phase1185 (D1) — per-copy UPLOAD staging buffers created by
    // copy_buffer_to_image. They must outlive the GPU execution of this
    // command list; cleared at the next begin() (the engine has waited on the
    // prior frame's fence by then, mirroring how command-allocator reset is
    // safe at begin()).
    std::vector<ComPtr<ID3D12Resource>> retained_uploads_;
    // D14 (phase1188) — nesting depth so pop_debug_group never calls
    // EndEvent more times than push_debug_group called BeginEvent.
    std::uint32_t debug_group_depth_ { 0 };
    // D11 (phase1191) — resolved OM render-target state of the most recent
    // begin_render_pass, snapshotted so a parallel-pass recorder can re-bind
    // it per lane (a D3D12 list inherits no state). See rebind_parallel_pass_state.
    struct PassState
    {
        UINT rtv_count { 0 };
        std::array<D3D12_CPU_DESCRIPTOR_HANDLE, 8> rtvs {};
        bool has_dsv { false };
        D3D12_CPU_DESCRIPTOR_HANDLE dsv {};
    };
    PassState pass_state_ {};
};

// ===========================================================================
// D11 (phase1191) — D3D12 parallel render pass.
//
// MODEL: sequential-replay-onto-primary fallback (the roadmap-sanctioned
// CORRECTNESS-parity option). D3D12 has no Vulkan-style secondary/inheriting
// command lists — a direct list cannot be replayed *inside* another list's
// render-pass scope, and a bundle forbids OMSetRenderTargets/RSSetViewports,
// so the Vulkan vkCmdExecuteCommands-into-a-RENDER_PASS_CONTINUE secondary has
// no direct D3D12 analogue. Instead each lane is a thread-confined RECORDER
// that captures its draw-subset calls (bind pipeline / VB / IB / descriptor /
// push-constants / viewport / scissor / draw / debug-group) into its own
// std::function command list. Lanes can therefore be filled fully in PARALLEL
// (no shared mutable state). finish() then REPLAYS the lane command lists onto
// the primary command list IN LANE ORDER (lane 0, then lane 1, ...), so the
// emitted command stream is BYTE-IDENTICAL to a single thread that recorded
// the same draws in lane order. Each lane replay is prefixed with a
// rebind_parallel_pass_state() so the per-lane state-isolation contract is
// honoured explicitly (each lane re-sets the pass RTVs/DSV + viewport +
// scissor before its draws).
//
// This gives true parallel RECORDING (the X1-FU-F scaling win) with a
// deterministic, single-threaded-identical RESULT — which is exactly the
// correctness bar the task sets. A future true-parallel variant would need
// per-lane ID3D12GraphicsCommandLists; that is deferred (see report).
// ===========================================================================
class D3D12ParallelPassRecorder final : public cd::rhi::IParallelPassRecorder
{
public:
    // A single lane: an IDrawRecorder that defers every call into a
    // thread-confined command vector. Captures only by value / handle so the
    // replay is independent of caller lifetime within the pass.
    class LaneRecorder final : public cd::rhi::IDrawRecorder
    {
    public:
        using Cmd = std::function<void(D3D12CommandBuffer&)>;

        void bind_graphics_pipeline(cd::rhi::GraphicsPipelineHandle p) override
        {
            cmds_.emplace_back([p](D3D12CommandBuffer& c) { c.bind_graphics_pipeline(p); });
        }
        void bind_rt_pipeline(cd::rhi::RtPipelineHandle p) override
        {
            cmds_.emplace_back([p](D3D12CommandBuffer& c) { c.bind_rt_pipeline(p); });
        }
        void bind_descriptor_set(std::uint32_t set, cd::rhi::DescriptorSetHandle h) override
        {
            cmds_.emplace_back([set, h](D3D12CommandBuffer& c) { c.bind_descriptor_set(set, h); });
        }
        void bind_bindless_texture_array(std::uint32_t set,
                                         cd::rhi::BindlessTextureArrayHandle a) override
        {
            cmds_.emplace_back([set, a](D3D12CommandBuffer& c) { c.bind_bindless_texture_array(set, a); });
        }
        void bind_vertex_buffer(std::uint32_t binding, cd::rhi::BufferHandle b,
                                std::uint64_t offset) override
        {
            cmds_.emplace_back([binding, b, offset](D3D12CommandBuffer& c) { c.bind_vertex_buffer(binding, b, offset); });
        }
        void bind_index_buffer(cd::rhi::BufferHandle b, std::uint64_t offset,
                               cd::rhi::IndexType t) override
        {
            cmds_.emplace_back([b, offset, t](D3D12CommandBuffer& c) { c.bind_index_buffer(b, offset, t); });
        }
        void push_constants(cd::rhi::PipelineLayoutHandle layout, cd::rhi::ShaderStage stages,
                            std::uint32_t offset, std::uint32_t size, const void* data) override
        {
            // The source bytes are NOT guaranteed to outlive recording, so the
            // lane OWNS a copy that the deferred replay reads.
            std::vector<std::byte> bytes(size);
            if (data != nullptr && size != 0u)
                std::memcpy(bytes.data(), data, size);
            cmds_.emplace_back(
                [layout, stages, offset, size, store = std::move(bytes)](D3D12CommandBuffer& c)
                { c.push_constants(layout, stages, offset, size, store.data()); });
        }
        void set_viewport(const cd::rhi::Viewport& vp) override
        {
            cmds_.emplace_back([vp](D3D12CommandBuffer& c) { c.set_viewport(vp); });
        }
        void set_scissor(const cd::rhi::Rect2D& r) override
        {
            cmds_.emplace_back([r](D3D12CommandBuffer& c) { c.set_scissor(r); });
        }
        void draw(std::uint32_t vc, std::uint32_t ic, std::uint32_t fv,
                  std::uint32_t fi) override
        {
            cmds_.emplace_back([vc, ic, fv, fi](D3D12CommandBuffer& c) { c.draw(vc, ic, fv, fi); });
        }
        void draw_indexed(std::uint32_t ic, std::uint32_t inst, std::uint32_t fi,
                          std::int32_t vo, std::uint32_t finst) override
        {
            cmds_.emplace_back([ic, inst, fi, vo, finst](D3D12CommandBuffer& c) { c.draw_indexed(ic, inst, fi, vo, finst); });
        }
        void draw_mesh_tasks(std::uint32_t gx, std::uint32_t gy, std::uint32_t gz) override
        {
            cmds_.emplace_back([gx, gy, gz](D3D12CommandBuffer& c) { c.draw_mesh_tasks(gx, gy, gz); });
        }
        void push_debug_group(std::string_view name) override
        {
            cmds_.emplace_back([s = std::string { name }](D3D12CommandBuffer& c) { c.push_debug_group(s); });
        }
        void pop_debug_group() override
        {
            cmds_.emplace_back([](D3D12CommandBuffer& c) { c.pop_debug_group(); });
        }

        void replay(D3D12CommandBuffer& primary) const
        {
            for (const auto& cmd : cmds_)
                cmd(primary);
        }

    private:
        std::vector<Cmd> cmds_;
    };

    D3D12ParallelPassRecorder(D3D12CommandBuffer& primary,
                              const cd::rhi::Rect2D& render_area,
                              std::uint32_t lane_count)
        : primary_ { &primary }, render_area_ { render_area }
    {
        lanes_.reserve(lane_count);
        for (std::uint32_t i = 0; i < lane_count; ++i)
            lanes_.push_back(std::make_unique<LaneRecorder>());
    }
    ~D3D12ParallelPassRecorder() override = default;
    D3D12ParallelPassRecorder(const D3D12ParallelPassRecorder&) = delete;
    D3D12ParallelPassRecorder& operator=(const D3D12ParallelPassRecorder&) = delete;
    D3D12ParallelPassRecorder(D3D12ParallelPassRecorder&&) = delete;
    D3D12ParallelPassRecorder& operator=(D3D12ParallelPassRecorder&&) = delete;

    [[nodiscard]] std::uint32_t lane_count() const noexcept override
    {
        return static_cast<std::uint32_t>(lanes_.size());
    }
    [[nodiscard]] cd::rhi::IDrawRecorder& lane(std::uint32_t i) noexcept override
    {
        // Mirror the Vulkan contract: out-of-range is a caller bug — assert in
        // debug, clamp to the last lane in release.
        assert(i < lanes_.size() && "lane index out of range");
        const auto idx = i < lanes_.size() ? i : lanes_.size() - 1u;
        return *lanes_[idx];
    }
    void finish() override
    {
        if (finished_)
            return;
        // Join lanes onto the primary IN LANE ORDER. Each lane re-binds the
        // captured pass state first (D3D12 lists inherit nothing); on this
        // shared-primary path that is a benign redundant set, but it makes the
        // per-lane isolation contract explicit and replay-order-independent.
        for (const auto& l : lanes_)
        {
            primary_->rebind_parallel_pass_state(render_area_);
            l->replay(*primary_);
        }
        primary_->end_render_pass();
        finished_ = true;
    }

private:
    D3D12CommandBuffer* primary_ { nullptr };
    cd::rhi::Rect2D render_area_ {};
    std::vector<std::unique_ptr<LaneRecorder>> lanes_;
    bool finished_ { false };
};

std::unique_ptr<cd::rhi::IParallelPassRecorder>
D3D12CommandBuffer::begin_parallel_render_pass(
    const cd::rhi::RenderPassBeginInfo& info, std::uint32_t lane_count)
{
    if (lane_count == 0)
        lane_count = 1;
    // Open the pass on the primary exactly like the serial path: RTV/DSV bind +
    // clears + implicit barriers fire ONCE here (lanes draw with LOAD
    // semantics). This also snapshots pass_state_ for per-lane rebinding.
    begin_render_pass(info);
    return std::make_unique<D3D12ParallelPassRecorder>(
        *this, info.render_area, lane_count);
}

std::unique_ptr<cd::rhi::ICommandBuffer>
D3D12Device::do_create_command_buffer(cd::rhi::QueueType)
{
    ComPtr<ID3D12CommandAllocator> alloc;
    if (FAILED(device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&alloc))))
        return nullptr;
    ComPtr<ID3D12GraphicsCommandList> list;
    if (FAILED(device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, alloc.Get(), nullptr, IID_PPV_ARGS(&list))))
        return nullptr;
    list->Close();  // start closed; begin() resets it
    return std::make_unique<D3D12CommandBuffer>(this, alloc, list);
}

void D3D12Device::submit(cd::rhi::ICommandBuffer& cb)
{
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-static-cast-downcast) — safe by construction: only D3D12CommandBuffer instances are submitted on the D3D12 device path.
    auto* d3d_cb = static_cast<D3D12CommandBuffer*>(&cb);
    ID3D12CommandList* lists[] = { d3d_cb->native() };
    graphics_queue_->ExecuteCommandLists(1, lists);
}

// Phase 397 — SubmitDesc semaphore-based path (out-of-line; needs
// complete D3D12CommandBuffer for ExecuteCommandLists integration).
cd::core::Result<void> D3D12Device::submit(const cd::rhi::SubmitDesc& desc)
{
    // --- Wait semaphores (binary) ---
    for (const auto& ws : desc.wait_semaphores)
    {
        auto it = semaphores_.find(ws.semaphore.index());
        if (it == semaphores_.end()) continue;
        auto& sem = it->second;
        if (sem.value > 0)
        {
            HRESULT hr = graphics_queue_->Wait(sem.fence.Get(), sem.value);
            if (FAILED(hr))
            {
                return std::unexpected(cd::rhi::rhi_errors::make(
                    cd::rhi::rhi_errors::Code::kDeviceLost,
                    "submit: Wait(binary semaphore) failed"));
            }
        }
    }
    // --- Wait timeline semaphores ---
    for (const auto& wt : desc.wait_timeline_semaphores)
    {
        auto it = timelines_.find(wt.semaphore.index());
        if (it == timelines_.end()) continue;
        HRESULT hr = graphics_queue_->Wait(it->second.fence.Get(), wt.value);
        if (FAILED(hr))
        {
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kDeviceLost,
                "submit: Wait(timeline semaphore) failed"));
        }
    }

    // --- Execute command buffers ---
    if (!desc.command_buffers.empty())
    {
        std::vector<ID3D12CommandList*> lists;
        lists.reserve(desc.command_buffers.size());
        for (auto* cb : desc.command_buffers)
        {
            if (cb == nullptr) continue;
            // NOLINTNEXTLINE(cppcoreguidelines-pro-type-static-cast-downcast) — safe by construction: all ICommandBuffer* in SubmitDesc::command_buffers are D3D12CommandBuffer on this backend.
            auto* d3d_cb = static_cast<D3D12CommandBuffer*>(cb);
            lists.push_back(d3d_cb->native());
        }
        if (!lists.empty())
            graphics_queue_->ExecuteCommandLists(
                static_cast<UINT>(lists.size()), lists.data());
    }

    // --- Signal binary semaphores ---
    for (const auto& ss : desc.signal_semaphores)
    {
        auto it = semaphores_.find(ss.semaphore.index());
        if (it == semaphores_.end()) continue;
        auto& sem = it->second;
        ++sem.value;
        HRESULT hr = graphics_queue_->Signal(sem.fence.Get(), sem.value);
        if (FAILED(hr))
        {
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kDeviceLost,
                "submit: Signal(binary semaphore) failed"));
        }
    }
    // --- Signal timeline semaphores ---
    for (const auto& st : desc.signal_timeline_semaphores)
    {
        auto it = timelines_.find(st.semaphore.index());
        if (it == timelines_.end()) continue;
        HRESULT hr = graphics_queue_->Signal(it->second.fence.Get(), st.value);
        if (FAILED(hr))
        {
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kDeviceLost,
                "submit: Signal(timeline semaphore) failed"));
        }
    }
    // --- Signal host fence ---
    if (desc.signal_fence.value() != 0u)
    {
        auto it = fences_.find(desc.signal_fence.index());
        if (it != fences_.end())
        {
            auto& frec = it->second;
            ++frec.target_value;
            HRESULT hr = graphics_queue_->Signal(frec.fence.Get(),
                                                 frec.target_value);
            if (FAILED(hr))
            {
                return std::unexpected(cd::rhi::rhi_errors::make(
                    cd::rhi::rhi_errors::Code::kDeviceLost,
                    "submit: Signal(host fence) failed"));
            }
        }
    }
    return {};
}

}  // namespace

cd::core::Result<std::unique_ptr<cd::rhi::IDevice>>
create_d3d12_device(D3D12CreateInfo info)
{
    auto dev = std::make_unique<D3D12Device>();
    const HRESULT hr = dev->initialize(info);
    if (FAILED(hr))
    {
        char msg[160] {};
        std::snprintf(msg, sizeof(msg),
                      "D3D12CreateDevice / DXGI init failed: HRESULT 0x%08lx",
                      static_cast<unsigned long>(hr));
        return std::unexpected(cd::rhi::rhi_errors::make_owning(
            cd::rhi::rhi_errors::Code::kBackendInitFailed,
            std::string { msg }));
    }
    return std::unique_ptr<cd::rhi::IDevice>(std::move(dev));
}

    // End of the Windows-only block — close the diagnostic pragma we
    // pushed alongside the Win32 headers above.
#if defined(__clang__)
    #pragma clang diagnostic pop
#endif

#else  // _WIN32 - non-Windows: backend is unavailable.

cd::core::Result<std::unique_ptr<cd::rhi::IDevice>>
create_d3d12_device(D3D12CreateInfo /*info*/)
{
    return std::unexpected(cd::rhi::rhi_errors::make(
        cd::rhi::rhi_errors::Code::kBackendInitFailed,
        "D3D12 backend only available on Windows"));
}

#endif  // _WIN32

}  // namespace cd::rhi::d3d12
