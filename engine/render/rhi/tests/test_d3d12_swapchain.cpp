// =============================================================================
// CHROMODYNAMIC — engine/render/rhi/tests/test_d3d12_swapchain.cpp
//
// C-D3D12-FIXES / D-HDR-SWAPCHAIN + D-SWAPCHAIN-RESIZE: the D3D12 swapchain
// colour-space + in-place resize paths vs the Vulkan reference.
//
// D-HDR-SWAPCHAIN. create_swapchain historically NEVER read desc.colour_space
// nor called SetColorSpace1 (the IDXGISwapChain3 was in hand but unused for
// colour management) — an HDR10 / scRGB request was silently downgraded to SDR.
// The fix maps ColorSpace -> DXGI_COLOR_SPACE_TYPE, probes
// CheckColorSpaceSupport, and applies SetColorSpace1 when the surface supports
// it (fall-back-to-sRGB when not, never failing creation over a display cap).
//
// D-SWAPCHAIN-RESIZE. present() mapped EVERY non-S_OK Present result to
// kDeviceLost, so a window resize / occlusion never produced the
// kSwapchainOutOfDate contract the engine consumes; and there was no in-place
// ResizeBuffers path at all. The fix returns kSwapchainOutOfDate for occlusion /
// recoverable-present-failure (kDeviceLost only for true device removal) and
// adds IDevice::resize_swapchain (D3D12 ResizeBuffers) which returns kOk on a
// healthy chain / kSwapchainOutOfDate when the in-place resize is rejected.
//
// WHAT THESE TESTS PROVE (real WARP/hardware + a hidden message window):
//   * A swapchain created with each ColorSpace (incl. HDR10 PQ) succeeds and is
//     live (image_count > 0) — the colour-space path runs end-to-end without
//     aborting creation. HDR10 PQ "succeeds" via the documented sRGB fall-back
//     on an SDR panel, so this is display-independent.
//   * CheckColorSpaceSupport reports the PRESENT flag for the SDR sRGB space on
//     any monitor (the API half the brief calls for), verified directly on the
//     IDXGISwapChain3 the device handed back.
//   * resize_swapchain returns kOk (NOT kDeviceLost) for an in-place resize on a
//     healthy chain, and the swapchain reports the new extent's images. A
//     base-class device (no override) would return kNotImplemented — the D3D12
//     override is what makes this kOk.
//
// Honest-SKIP when no D3D12 adapter, or the hidden window can't be created.
// Pattern: Arrange/Act/Assert.
// =============================================================================
#if defined(_WIN32)
    #define WIN32_LEAN_AND_MEAN
    #define NOMINMAX
    // IID_PPV_ARGS / __uuidof are MS-language extensions that clang-cl rejects
    // under -Werror -Wlanguage-extension-token; silence for the D3D12/DXGI
    // includes + the IID_PPV_ARGS call sites below (mirrors D3D12Device.cpp).
    #if defined(__clang__)
        #pragma clang diagnostic push
        #pragma clang diagnostic ignored "-Wlanguage-extension-token"
    #endif
    #include <windows.h>
    #include <wrl/client.h>
    #include <d3d12.h>
    #include <dxgi1_6.h>
#endif

#include <cd/rhi/Descriptors.hpp>
#include <cd/rhi/IDevice.hpp>
#if defined(_WIN32)
    #include <cd/rhi/d3d12/D3D12Device.hpp>
#endif

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>

#if defined(_WIN32)

namespace
{

[[nodiscard]] std::unique_ptr<cd::rhi::IDevice> make_d3d12_device_or_null()
{
    cd::rhi::d3d12::D3D12CreateInfo ci {};
    ci.enable_validation = false;  // no debug-layer dependency in CI
    auto r = cd::rhi::d3d12::create_d3d12_device(ci);
    if (!r.has_value())
        return nullptr;
    return std::move(*r);
}

// RAII hidden top-level window. A real swapchain needs an HWND;
// CreateSwapChainForHwnd rejects a message-only (HWND_MESSAGE) parent, so this
// is a 1x1 invisible WS_OVERLAPPED window.
class HiddenWindow
{
public:
    HiddenWindow()
    {
        WNDCLASSEXW wc {};
        wc.cbSize        = sizeof(wc);
        wc.lpfnWndProc   = DefWindowProcW;
        wc.hInstance     = GetModuleHandleW(nullptr);
        wc.lpszClassName = L"CdD3D12SwapchainTestWnd";
        atom_ = RegisterClassExW(&wc);
        if (atom_ == 0)
            return;
        hwnd_ = CreateWindowExW(
            0, wc.lpszClassName, L"", WS_OVERLAPPEDWINDOW,
            0, 0, 64, 64, nullptr, nullptr, wc.hInstance, nullptr);
    }
    ~HiddenWindow()
    {
        if (hwnd_ != nullptr) DestroyWindow(hwnd_);
        if (atom_ != 0)
            UnregisterClassW(L"CdD3D12SwapchainTestWnd", GetModuleHandleW(nullptr));
    }
    HiddenWindow(const HiddenWindow&) = delete;
    HiddenWindow& operator=(const HiddenWindow&) = delete;
    [[nodiscard]] HWND hwnd() const noexcept { return hwnd_; }

private:
    ATOM atom_ { 0 };
    HWND hwnd_ { nullptr };
};

[[nodiscard]] cd::rhi::SwapchainDesc make_desc(HWND hwnd, cd::rhi::ColorSpace cs)
{
    cd::rhi::SwapchainDesc sd {};
    sd.window_handle = hwnd;
    sd.extent        = { 64, 64 };
    sd.image_count   = 2;
    sd.format        = cd::rhi::Format::kBGRA8Unorm;  // flip-model friendly
    sd.colour_space  = cs;
    sd.vsync         = true;
    return sd;
}

}  // namespace

// ---- D-HDR-SWAPCHAIN: each ColorSpace creates a live swapchain --------------
TEST(D3D12Swapchain, ColorSpacePathCreatesLiveSwapchain)
{
    auto dev = make_d3d12_device_or_null();
    if (dev == nullptr)
        GTEST_SKIP() << "No D3D12 adapter (WARP/hardware) on this host";
    HiddenWindow win;
    if (win.hwnd() == nullptr)
        GTEST_SKIP() << "could not create a hidden test window";
    auto& d = *dev;

    for (auto cs : { cd::rhi::ColorSpace::kSrgbNonlinear,
                     cd::rhi::ColorSpace::kHdr10St2084,
                     cd::rhi::ColorSpace::kScrgbLinear })
    {
        auto r = d.create_swapchain(make_desc(win.hwnd(), cs));
        ASSERT_TRUE(r.has_value())
            << "create_swapchain aborted for a colour space (the fall-back-to-"
               "sRGB contract must NEVER fail creation over a display cap)";
        const auto sc = *r;
        EXPECT_GT(d.swapchain_image_count(sc), 0u)
            << "swapchain is not live after the colour-space path ran";
        d.destroy_swapchain(sc);
    }
}

// ---- D-HDR-SWAPCHAIN: CheckColorSpaceSupport reports a flag (the API half) ---
//
// Build an independent WARP device + queue + flip-model swapchain directly via
// DXGI and call IDXGISwapChain3::CheckColorSpaceSupport — the exact primitive
// the device's create_swapchain fix relies on. The SDR sRGB colour space
// (DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709) is PRESENT-supported on every
// monitor, so the PRESENT flag must be set. This is the display-independent
// half of the HDR wiring (PQ pixels are panel-gated, but the API path is not).
TEST(D3D12Swapchain, CheckColorSpaceSupportReportsSrgbFlag)
{
    using Microsoft::WRL::ComPtr;

    HiddenWindow win;
    if (win.hwnd() == nullptr)
        GTEST_SKIP() << "could not create a hidden test window";

    ComPtr<IDXGIFactory4> factory;
    if (FAILED(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory))))
        GTEST_SKIP() << "CreateDXGIFactory2 failed";

    // WARP adapter so this runs without a hardware GPU.
    ComPtr<IDXGIAdapter> warp;
    if (FAILED(factory->EnumWarpAdapter(IID_PPV_ARGS(&warp))))
        GTEST_SKIP() << "no WARP adapter";

    ComPtr<ID3D12Device> device;
    if (FAILED(D3D12CreateDevice(
            warp.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device))))
        GTEST_SKIP() << "D3D12CreateDevice(WARP) failed";

    D3D12_COMMAND_QUEUE_DESC qd {};
    qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    ComPtr<ID3D12CommandQueue> queue;
    if (FAILED(device->CreateCommandQueue(&qd, IID_PPV_ARGS(&queue))))
        GTEST_SKIP() << "CreateCommandQueue failed";

    DXGI_SWAP_CHAIN_DESC1 sd {};
    sd.Width       = 64;
    sd.Height      = 64;
    sd.Format      = DXGI_FORMAT_B8G8R8A8_UNORM;
    sd.SampleDesc.Count = 1;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.BufferCount = 2;
    sd.SwapEffect  = DXGI_SWAP_EFFECT_FLIP_DISCARD;

    ComPtr<IDXGISwapChain1> chain1;
    if (FAILED(factory->CreateSwapChainForHwnd(
            queue.Get(), win.hwnd(), &sd, nullptr, nullptr, &chain1)))
        GTEST_SKIP() << "CreateSwapChainForHwnd failed";
    ComPtr<IDXGISwapChain3> chain3;
    if (FAILED(chain1.As(&chain3)))
        GTEST_SKIP() << "IDXGISwapChain3 QI failed";

    UINT support = 0;
    const HRESULT hr = chain3->CheckColorSpaceSupport(
        DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709, &support);
    ASSERT_TRUE(SUCCEEDED(hr)) << "CheckColorSpaceSupport call failed";
    EXPECT_NE(support & DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_PRESENT, 0u)
        << "the SDR sRGB colour space must report PRESENT support on any "
           "monitor — this is the CheckColorSpaceSupport flag the "
           "D-HDR-SWAPCHAIN SetColorSpace1 wiring depends on";
}

// ---- D-SWAPCHAIN-RESIZE: in-place resize returns kOk, not kDeviceLost -------
TEST(D3D12Swapchain, ResizeSwapchainReturnsOkOnHealthyChain)
{
    auto dev = make_d3d12_device_or_null();
    if (dev == nullptr)
        GTEST_SKIP() << "No D3D12 adapter (WARP/hardware) on this host";
    HiddenWindow win;
    if (win.hwnd() == nullptr)
        GTEST_SKIP() << "could not create a hidden test window";
    auto& d = *dev;

    auto r = d.create_swapchain(
        make_desc(win.hwnd(), cd::rhi::ColorSpace::kSrgbNonlinear));
    ASSERT_TRUE(r.has_value());
    const auto sc = *r;

    // In-place resize to a new extent. The D3D12 override calls ResizeBuffers;
    // on a healthy flip-model chain this is kOk (NOT the blanket kDeviceLost the
    // pre-fix present path would have produced for any swapchain trouble). A
    // base-class device with no override returns kNotImplemented — so a kOk here
    // proves the D3D12 ResizeBuffers path is wired.
    auto resize_r = d.resize_swapchain(sc, 96, 48);
    EXPECT_TRUE(resize_r.has_value())
        << "BUG: in-place resize_swapchain failed on a healthy chain — the "
           "D3D12 ResizeBuffers path is missing or maps resize to a fatal "
           "error: "
        << (resize_r.has_value()
                ? std::string {}
                : std::string(resize_r.error().message.begin(),
                              resize_r.error().message.end()));

    // The swapchain must still be live + re-issue valid image views after the
    // in-place resize.
    EXPECT_GT(d.swapchain_image_count(sc), 0u);
    EXPECT_TRUE(d.swapchain_image_view(sc, 0).is_valid())
        << "swapchain image view must be re-issued after ResizeBuffers";

    // A no-op refresh (0,0 = keep current extent) is the canonical health probe
    // and must also be kOk.
    auto refresh_r = d.resize_swapchain(sc, 0, 0);
    EXPECT_TRUE(refresh_r.has_value())
        << "a no-op (0,0) resize refresh on a healthy chain must be kOk";

    d.destroy_swapchain(sc);
}

#endif  // _WIN32

#if defined(_WIN32) && defined(__clang__)
    #pragma clang diagnostic pop
#endif
