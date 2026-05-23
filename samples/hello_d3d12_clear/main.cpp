// =============================================================================
// CHROMODYNAMIC — samples/hello_d3d12_clear
//
// v0.32.0 / Phase 13.C — first D3D12 sample that actually renders.
//
// Opens a 640x360 Win32 window, creates a 2-image swapchain via the
// D3D12 backend, and runs a tiny render loop that clears the
// back-buffer to a teal color and presents. Exit on Esc / window
// close, OR after CD_D3D12_CLEAR_HEADLESS_FRAMES frames (default 60
// when the env var is set) so CI can run it as a smoke test that
// definitely terminates.
//
// What this sample proves end-to-end:
//   - cd::rhi_d3d12::create_d3d12_device boots ID3D12Device+queue
//   - create_swapchain wires DXGI flip-model + per-image RTV
//   - create_command_buffer / begin / begin_render_pass with
//     LoadOp::kClear / end_render_pass / end records a CMD list
//   - submit + present + GetCurrentBackBufferIndex closes the loop
// =============================================================================
#include <cd/core/ErrorCode.hpp>
#include <cd/core/Version.hpp>
#include <cd/rhi/ICommandBuffer.hpp>
#include <cd/rhi/IDevice.hpp>
#include <cd/rhi_d3d12/D3D12Device.hpp>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <cstdio>
#include <cstdlib>

namespace
{

constexpr UINT kWidth  = 640;
constexpr UINT kHeight = 360;

bool g_quit = false;

LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM w, LPARAM l)
{
    switch (msg)
    {
        case WM_CLOSE:
        case WM_DESTROY:
            g_quit = true;
            PostQuitMessage(0);
            return 0;
        case WM_KEYDOWN:
            if (w == VK_ESCAPE)
            {
                g_quit = true;
                PostQuitMessage(0);
            }
            return 0;
        default:
            return DefWindowProcW(hwnd, msg, w, l);
    }
}

[[nodiscard]] HWND create_window()
{
    WNDCLASSEXW wc {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = wnd_proc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));  // IDC_ARROW is char*
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.lpszClassName = L"CHROMODYNAMIC.hello_d3d12_clear";
    RegisterClassExW(&wc);

    RECT rc { 0, 0, static_cast<LONG>(kWidth), static_cast<LONG>(kHeight) };
    const DWORD style = WS_OVERLAPPEDWINDOW & ~(WS_THICKFRAME | WS_MAXIMIZEBOX);
    AdjustWindowRect(&rc, style, FALSE);
    return CreateWindowExW(
        0, wc.lpszClassName, L"hello_d3d12_clear",
        style, CW_USEDEFAULT, CW_USEDEFAULT,
        rc.right - rc.left, rc.bottom - rc.top,
        nullptr, nullptr, wc.hInstance, nullptr);
}

void pump()
{
    MSG msg;
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

}  // namespace

int main()
{
    std::fprintf(stdout,
                 "CHROMODYNAMIC %u.%u.%u — hello_d3d12_clear\n",
                 static_cast<unsigned>(cd::core::kEngineVersion.major),
                 static_cast<unsigned>(cd::core::kEngineVersion.minor),
                 static_cast<unsigned>(cd::core::kEngineVersion.patch));

    cd::rhi_d3d12::D3D12CreateInfo dci {};
    dci.app_name = "hello_d3d12_clear";
    dci.enable_validation = false;

    auto dev_r = cd::rhi_d3d12::create_d3d12_device(dci);
    if (!dev_r.has_value())
    {
        std::fprintf(stderr,
                     "[d3d12] device init failed: %.*s\n",
                     static_cast<int>(dev_r.error().message.size()),
                     dev_r.error().message.data());
        return 1;
    }
    auto& device = **dev_r;
    const auto adapter = device.adapter_name();
    std::fprintf(stdout, "[d3d12] adapter=%.*s\n",
                 static_cast<int>(adapter.size()), adapter.data());

    HWND hwnd = create_window();
    if (!hwnd)
    {
        std::fprintf(stderr, "[d3d12] CreateWindowExW failed\n");
        return 1;
    }
    ShowWindow(hwnd, SW_SHOWNORMAL);

    cd::rhi::SwapchainDesc sd {};
    sd.window_handle = hwnd;
    sd.display_handle = GetModuleHandleW(nullptr);
    sd.extent = { kWidth, kHeight };
    sd.image_count = 2;
    sd.format = cd::rhi::Format::kRGBA8Unorm;  // flip-model accepts UNORM directly
    sd.vsync = true;

    auto swap_r = device.create_swapchain(sd);
    if (!swap_r.has_value())
    {
        std::fprintf(stderr,
                     "[d3d12] create_swapchain failed: %.*s\n",
                     static_cast<int>(swap_r.error().message.size()),
                     swap_r.error().message.data());
        return 1;
    }
    const auto swap = *swap_r;
    const auto image_count = device.swapchain_image_count(swap);
    std::fprintf(stdout, "[d3d12] swapchain images = %u\n", image_count);

    // Headless-friendly frame cap so CI can run this as a smoke test
    // without a human Esc. CD_D3D12_CLEAR_HEADLESS_FRAMES=0 → run
    // until the user closes the window. Default 0 (interactive).
    // MSVC marks std::getenv "deprecated"; this is the same dance the
    // rest of the samples use to silence the warning under -Werror.
#if defined(__clang__)
    #pragma clang diagnostic push
    #pragma clang diagnostic ignored "-Wdeprecated-declarations"
#elif defined(_MSC_VER)
    #pragma warning(push)
    #pragma warning(disable : 4996)
#endif
    const char* cap_env = std::getenv("CD_D3D12_CLEAR_HEADLESS_FRAMES");
#if defined(__clang__)
    #pragma clang diagnostic pop
#elif defined(_MSC_VER)
    #pragma warning(pop)
#endif
    const std::uint32_t max_frames = (cap_env && *cap_env)
        ? static_cast<std::uint32_t>(std::atoi(cap_env)) : 0u;
    std::uint32_t frame = 0;
    while (!g_quit)
    {
        pump();
        if (g_quit) break;

        auto idx_r = device.acquire_next_image(swap, {}, {}, 0);
        if (!idx_r.has_value())
        {
            std::fprintf(stderr, "[d3d12] acquire_next_image failed\n");
            break;
        }
        const std::uint32_t image_idx = *idx_r;

        auto cb = device.create_command_buffer(cd::rhi::QueueType::kGraphics);
        cb->begin();

        cd::rhi::ColorAttachmentInfo att {};
        att.view = device.swapchain_image_view(swap, image_idx);
        att.load_op = cd::rhi::LoadOp::kClear;
        att.store_op = cd::rhi::StoreOp::kStore;
        // Teal — RGB (0.0, 0.45, 0.55), alpha 1.0
        att.clear_color.f32[0] = 0.0F;
        att.clear_color.f32[1] = 0.45F;
        att.clear_color.f32[2] = 0.55F;
        att.clear_color.f32[3] = 1.0F;

        cd::rhi::RenderPassBeginInfo rpi {};
        rpi.render_area = { { 0, 0 }, { kWidth, kHeight } };
        rpi.color_attachments = std::span<const cd::rhi::ColorAttachmentInfo> { &att, 1 };

        cb->begin_render_pass(rpi);
        cb->end_render_pass();
        cb->end();

        device.submit(*cb);
        (void)device.present(swap, image_idx, {});
        device.wait_idle();  // simplest possible per-frame sync — fine for clear-only

        ++frame;
        if (max_frames > 0 && frame >= max_frames)
        {
            std::fprintf(stdout, "[d3d12] headless cap reached at frame=%u\n", frame);
            break;
        }
    }

    device.destroy_swapchain(swap);
    std::fprintf(stdout, "[d3d12] clear loop OK — rendered %u frame(s)\n", frame);
    return 0;
}
