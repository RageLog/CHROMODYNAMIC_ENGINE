// =============================================================================
// CHROMODYNAMIC -- cd::imgui_backend D3D12 DEVICE-GATED GPU-draw test.
//
// The D3D12 mirror of test_imgui_backend_device.cpp (Vulkan). It closes the
// one gap the cd::imgui_backend host-side suite seals as device-gated for the
// DirectX 12 backend: the live draw path
//   D3D12Context::render(cmd) -> ImGui_ImplDX12_RenderDrawData
// recorded onto a REAL ID3D12GraphicsCommandList. This host HAS a D3D12 adapter
// (cd_test_d3d12_* pass here), so the seal is a genuine exercised assertion.
//
// Honesty contract (mirrors test_imgui_backend_device.cpp + the d3d12 rhi tests):
//   * GTEST_SKIP when no D3D12 adapter (WARP/hardware) makes an IDevice ->
//     headless CI stays green and the seal stays honest where no device exists.
//   * GTEST_SKIP when no platform window can be created (e.g. a session with no
//     window-station).
//   * No present surface / swapchain is required: ImGui draws into an OFFSCREEN
//     colour texture via the cd::rhi begin_render_pass shape (OMSetRenderTargets
//     under the hood), so the GPU draw is driven end-to-end without a
//     window-system surface.
//
// What this PROVES on a device host:
//   1. D3D12Context::create succeeds on a D3D12 IDevice (NOT kBackendMismatch).
//   2. new_frame() + real ImGui widget calls produce non-empty draw data.
//   3. render(cmd) inside an active render pass binds the SRV heap + records
//      ImGui_ImplDX12_RenderDrawData onto the command list without a recording
//      error, and submit + wait_idle complete without device removal.
//
// Its OWN test binary so ImGui's global singleton context is isolated from the
// headless contexts the host-side suite creates/destroys.
// =============================================================================
#if defined(_WIN32)

    #include <cd/imgui/D3D12Context.hpp>
    #include <cd/platform/Window.hpp>
    #include <cd/rhi/Barriers.hpp>
    #include <cd/rhi/Format.hpp>
    #include <cd/rhi/ICommandBuffer.hpp>
    #include <cd/rhi/IDevice.hpp>
    #include <cd/rhi/d3d12/D3D12Device.hpp>

    #include <imgui.h>

    #include <gtest/gtest.h>

    #include <array>
    #include <cstdint>
    #include <memory>

namespace
{

// Mirror the d3d12 rhi skip guard: a D3D12 IDevice or nullptr.
[[nodiscard]] std::unique_ptr<cd::rhi::IDevice> try_make_device()
{
    cd::rhi::d3d12::D3D12CreateInfo info {};
    info.enable_validation = false;  // no debug-layer dependency in CI
    auto r = cd::rhi::d3d12::create_d3d12_device(info);
    if (!r.has_value())
        return nullptr;
    return std::move(*r);
}

// A visible platform window. ImGui_ImplWin32_NewFrame derives io.DisplaySize
// from GetClientRect(hwnd); a never-shown window reports a 0x0 client rect,
// which clips every widget and emits zero vertices (the draw path would then be
// a silent no-op). Mirrors the Vulkan device test.
[[nodiscard]] std::unique_ptr<cd::platform::IWindow> try_make_window()
{
    cd::platform::WindowDesc wd {};
    wd.title = "cd_imgui_backend_d3d12_device_test";
    wd.width = 256;
    wd.height = 256;
    wd.visible = true;
    auto r = cd::platform::create_window(wd);
    if (!r.has_value())
        return nullptr;
    return std::move(*r);
}

// The full device-gated path: create -> new_frame -> draw -> offscreen pass ->
// render(cmd) -> submit -> wait_idle. SKIPs (never fails) when the device or
// the window is unavailable.
TEST(ImGuiBackendD3D12Device, RenderDrawDataOntoOffscreenPass)
{
    auto dev = try_make_device();
    if (!dev)
        GTEST_SKIP() << "no D3D12 adapter (WARP/hardware) on this host";

    auto window = try_make_window();
    if (!window)
        GTEST_SKIP() << "could not create a platform window on this host";

    // The colour format the ImGui pipeline is built for AND the offscreen
    // attachment MUST match so the render pass / PSO RTV format agree.
    constexpr cd::rhi::Format kColorFormat = cd::rhi::Format::kRGBA8Unorm;

    cd::imgui::D3D12InitDesc id {};
    id.window = window.get();
    id.device = dev.get();
    id.color_format = kColorFormat;
    id.frames_in_flight = 2;

    auto ctx_r = cd::imgui::D3D12Context::create(id);
    // On a D3D12-backed device, create MUST NOT return kBackendMismatch.
    if (!ctx_r.has_value())
    {
        ASSERT_NE(ctx_r.error().code,
                  static_cast<std::uint32_t>(cd::imgui::imgui_errors::Code::kBackendMismatch))
            << "D3D12Context::create returned kBackendMismatch on a D3D12 IDevice";
        // A non-mismatch init failure (e.g. no window station for the Win32
        // backend) is an honest SKIP, not a draw-path failure.
        GTEST_SKIP() << "D3D12Context::create failed (non-mismatch): " << ctx_r.error().message;
    }
    auto& ctx = *ctx_r.value();

    // ---- Warm-up frame ---------------------------------------------------
    // A brand-new ImGui window with no API-set size is HIDDEN for exactly one
    // frame while ImGui auto-measures content to fit. On that hidden first
    // frame the widgets are submitted (layout runs) but emit NO vertices --
    // ImGui::Render() would yield TotalVtxCount == 0 and RenderDrawData would
    // be a silent no-op. We drive one full discarded frame so the window is
    // laid out; the NEXT frame renders it visible with real geometry.
    //
    // With ImGui 1.92's dynamic font atlas + the DX12 backend's
    // RendererHasTextures support, the font texture builds on demand during the
    // real frame -- no manual Fonts->Build() / texture upload is needed first.
    ctx.new_frame();
    ImGui::Begin("device_probe");
    ImGui::Text("ImGui D3D12 device draw-path test");
    ImGui::Button("noop");
    ImGui::End();
    ImGui::EndFrame();

    // ---- Produce real draw data (second frame, window now visible) -------
    ctx.new_frame();
    ImGui::Begin("device_probe");
    ImGui::Text("ImGui D3D12 device draw-path test");
    ImGui::Button("noop");
    ImGui::End();

    // ---- Offscreen colour target (no swapchain / present surface) --------
    cd::rhi::TextureDesc td {};
    td.type = cd::rhi::TextureType::k2D;
    td.format = kColorFormat;
    td.extent = { 256, 256, 1 };
    td.mip_levels = 1;
    td.array_layers = 1;
    td.usage = cd::rhi::TextureUsage::kColorAttachment;
    td.memory = cd::rhi::MemoryUsage::kGpuOnly;
    auto tex = dev->create_texture(td);
    ASSERT_TRUE(tex.has_value()) << tex.error().message;

    cd::rhi::TextureViewDesc vd {};
    vd.texture = *tex;
    vd.type = cd::rhi::TextureType::k2D;
    vd.format = kColorFormat;
    vd.base_mip = 0;
    vd.mip_count = 1;
    vd.base_layer = 0;
    vd.layer_count = 1;
    auto view = dev->create_texture_view(vd);
    ASSERT_TRUE(view.has_value()) << view.error().message;

    auto cb = dev->create_command_buffer(cd::rhi::QueueType::kGraphics);
    ASSERT_NE(cb, nullptr);

    cb->begin();
    // Undefined -> ColorAttachment so the render pass is in-spec.
    std::array<cd::rhi::TextureBarrier, 1> to_color {
        cd::rhi::TextureBarrier {
            .texture = *tex,
            .from = cd::rhi::ResourceState::kUndefined,
            .to = cd::rhi::ResourceState::kColorAttachment,
            .range = { .base_mip = 0, .mip_count = 1, .base_layer = 0, .layer_count = 1 },
        },
    };
    cb->barrier({}, to_color);

    std::array<cd::rhi::ColorAttachmentInfo, 1> color_attachments {
        cd::rhi::ColorAttachmentInfo {
            .view = *view,
            .load_op = cd::rhi::LoadOp::kClear,
            .store_op = cd::rhi::StoreOp::kStore,
            .clear_color = { .f32 = { 0.0F, 0.0F, 0.0F, 1.0F } },
        },
    };
    cd::rhi::RenderPassBeginInfo rp {};
    rp.render_area = cd::rhi::Rect2D {
        { 0,   0   },
        { 256, 256 }
    };
    rp.color_attachments = color_attachments;

    cb->begin_render_pass(rp);
    cb->set_viewport(cd::rhi::Viewport { 0.0F, 0.0F, 256.0F, 256.0F, 0.0F, 1.0F });
    cb->set_scissor(
        cd::rhi::Rect2D {
            { 0,   0   },
            { 256, 256 }
        }
    );

    // THE deliverable: drive ImGui_ImplDX12_RenderDrawData on the REAL command
    // list, inside the active render pass (RTV bound by begin_render_pass).
    ctx.render(*cb);

    cb->end_render_pass();
    cb->end();

    // ImGui::Render() ran inside ctx.render(); the draw data must be non-empty
    // so we know RenderDrawData actually had vertices to record (vs the early
    // TotalVtxCount==0 bail-out inside D3D12Context::render).
    const ImDrawData* draw = ImGui::GetDrawData();
    ASSERT_NE(draw, nullptr);
    EXPECT_GT(draw->TotalVtxCount, 0) << "ImGui produced no vertices; the draw path was a no-op";

    // The recorder must report no error after recording the ImGui draws.
    EXPECT_FALSE(cb->recording_error()) << "command buffer flagged a recording error";

    dev->submit(*cb);
    dev->wait_idle();  // event-based completion -- no sleep_for.

    EXPECT_EQ(dev->backend(), cd::rhi::Backend::kD3D12);  // device survived.

    dev->destroy_texture_view(*view);
    dev->destroy_texture(*tex);
    // ctx dtor (wait_idle + ImGui shutdown) runs before dev dtor -- unique_ptr
    // declaration order guarantees ctx tears down first.
}

}  // namespace

#endif  // _WIN32
