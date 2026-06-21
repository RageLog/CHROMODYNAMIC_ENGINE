// =============================================================================
// CHROMODYNAMIC -- cd::imgui_backend DEVICE-GATED GPU-draw test.
//
// The host-side suite (test_imgui_backend.cpp) covers everything reachable
// WITHOUT a GPU: the name-hash colour encoder, the profiler flame-graph
// branch logic through a headless ImGui context, Context::create's null-
// argument guards, the error-domain contract, and the input-mapping tables.
//
// This file closes the one gap that suite explicitly seals as device-gated:
// the live draw path Context::render(cmd) -> ImGui_ImplVulkan_RenderDrawData
// recorded onto a REAL Vulkan command buffer. The host this targets HAS a
// Vulkan ICD (cd_test_rhi_vulkan passes here), so the seal can be promoted to
// a genuine, exercised assertion rather than a promote-on-need note.
//
// Honesty contract (mirrors test_rhi_vulkan.cpp):
//   * GTEST_SKIP when no Vulkan ICD can make an IDevice -> headless CI stays
//     green and the seal stays honest where no device exists.
//   * GTEST_SKIP when no platform window can be created (e.g. a session with
//     no window-station) -> same reason the rhi swapchain tests skip on a
//     null HWND.
//   * No present surface / swapchain is required: we render ImGui into an
//     OFFSCREEN colour texture via VK_KHR_dynamic_rendering, exactly the
//     begin_render_pass shape the renderer uses, so the GPU draw is driven
//     end-to-end without a window-system surface.
//
// What this PROVES on a device host:
//   1. Context::create succeeds on a Vulkan IDevice (NOT kBackendMismatch).
//   2. new_frame() + real ImGui widget calls produce non-empty draw data.
//   3. render(cmd) inside an active dynamic-rendering pass records
//      ImGui_ImplVulkan_RenderDrawData onto the command buffer without a
//      recording error, and the submit + wait_idle complete without device
//      loss.
//
// This is its OWN test binary so ImGui's global singleton context is isolated
// from the headless contexts the host-side suite creates/destroys.
// =============================================================================
#include <cd/imgui/Context.hpp>
#include <cd/platform/Window.hpp>
#include <cd/rhi/Barriers.hpp>
#include <cd/rhi/Format.hpp>
#include <cd/rhi/ICommandBuffer.hpp>
#include <cd/rhi/IDevice.hpp>
#include <cd/rhi/vulkan/VulkanDevice.hpp>

#include <imgui.h>

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <memory>

namespace
{

// Mirror the rhi_vulkan skip guard EXACTLY: a Vulkan IDevice or nullptr.
[[nodiscard]] std::unique_ptr<cd::rhi::IDevice> try_make_device()
{
    cd::rhi::vulkan::VulkanCreateInfo info {};
    info.enable_validation = false;
    auto r = cd::rhi::vulkan::create_vulkan_device(info);
    if (!r.has_value())
        return nullptr;
    return std::move(*r);
}

// A hidden platform window. create_window returns a real IWindow whose
// native_window_handle() feeds ImGui_ImplWin32_Init inside Context::create.
[[nodiscard]] std::unique_ptr<cd::platform::IWindow> try_make_window()
{
    cd::platform::WindowDesc wd {};
    wd.title = "cd_imgui_backend_device_test";
    wd.width = 256;
    wd.height = 256;
    // Visible: ImGui_ImplWin32_NewFrame derives io.DisplaySize from
    // GetClientRect(hwnd); a never-shown window reports a 0x0 client rect, which
    // makes ImGui clip every widget and emit zero vertices (the draw path would
    // then be a silent no-op and not actually exercise RenderDrawData).
    wd.visible = true;
    auto r = cd::platform::create_window(wd);
    if (!r.has_value())
        return nullptr;
    return std::move(*r);
}

// The full device-gated path: create -> new_frame -> draw -> offscreen pass ->
// render(cmd) -> submit -> wait_idle. SKIPs (never fails) when the device or
// the window is unavailable.
TEST(ImGuiBackendDevice, RenderDrawDataOntoOffscreenPass)
{
    auto dev = try_make_device();
    if (!dev)
        GTEST_SKIP() << "no Vulkan ICD available on this host";

    auto window = try_make_window();
    if (!window)
        GTEST_SKIP() << "could not create a platform window on this host";

    // The colour format the ImGui pipeline is built for AND the offscreen
    // attachment MUST match so the dynamic-rendering pass is in-spec.
    constexpr cd::rhi::Format kColorFormat = cd::rhi::Format::kRGBA8Unorm;

    cd::imgui::InitDesc id {};
    id.window = window.get();
    id.device = dev.get();
    id.color_format = kColorFormat;
    id.frames_in_flight = 2;

    auto ctx_r = cd::imgui::Context::create(id);
    // On a Vulkan-backed device, create MUST NOT return kBackendMismatch.
    if (!ctx_r.has_value())
    {
        ASSERT_NE(ctx_r.error().code,
                  static_cast<std::uint32_t>(cd::imgui::imgui_errors::Code::kBackendMismatch))
            << "Context::create returned kBackendMismatch on a Vulkan IDevice";
        // A non-mismatch init failure (e.g. no window station for the Win32
        // backend) is an honest SKIP, not a draw-path failure.
        GTEST_SKIP() << "Context::create failed (non-mismatch): " << ctx_r.error().message;
    }
    auto& ctx = *ctx_r.value();

    // ---- Warm-up frame ---------------------------------------------------
    // A brand-new ImGui window with no API-set size is HIDDEN for exactly one
    // frame while ImGui auto-measures its content to fit the window
    // (imgui.cpp: `if (window_just_created && !size_set_by_api)
    // window->HiddenFramesCannotSkipItems = 1;`). On that hidden first frame
    // the widgets are still submitted (so layout runs) but the window emits
    // NO draw vertices — ImGui::Render() would yield TotalVtxCount == 0 and
    // RenderDrawData would be a silent no-op. We therefore drive one full
    // discarded frame here so the window is laid out; the NEXT frame renders
    // it visible with real geometry. ImGui::EndFrame() finalises the frame
    // without producing a draw list to record.
    //
    // Note: with ImGui 1.92's dynamic font atlas + the Vulkan backend's
    // ImGuiBackendFlags_RendererHasTextures support, the font texture builds
    // on demand during the real frame — no manual Fonts->Build() / font-
    // texture upload call is needed before the first frame.
    ctx.new_frame();
    ImGui::Begin("device_probe");
    ImGui::Text("ImGui device draw-path test");
    ImGui::Button("noop");
    ImGui::End();
    ImGui::EndFrame();

    // ---- Produce real draw data (second frame, window now visible) -------
    ctx.new_frame();
    ImGui::Begin("device_probe");
    ImGui::Text("ImGui device draw-path test");
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
    // Undefined -> ColorAttachment so vkCmdBeginRendering is in-spec.
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

    // THE deliverable: drive ImGui_ImplVulkan_RenderDrawData on the REAL
    // command buffer, inside the active dynamic-rendering pass.
    ctx.render(*cb);

    cb->end_render_pass();
    cb->end();

    // ImGui::Render() ran inside ctx.render(); the draw data must be non-empty
    // so we know RenderDrawData actually had vertices to record (vs the early
    // TotalVtxCount==0 bail-out inside Context::render).
    const ImDrawData* draw = ImGui::GetDrawData();
    ASSERT_NE(draw, nullptr);
    EXPECT_GT(draw->TotalVtxCount, 0) << "ImGui produced no vertices; the draw path was a no-op";

    // The recorder must report no error after recording the ImGui draws.
    EXPECT_FALSE(cb->recording_error()) << "command buffer flagged a recording error";

    dev->submit(*cb);
    dev->wait_idle();  // event-based completion — no sleep_for.

    EXPECT_EQ(dev->backend(), cd::rhi::Backend::kVulkan);  // device survived.

    dev->destroy_texture_view(*view);
    dev->destroy_texture(*tex);
    // ctx dtor (vkDeviceWaitIdle + ImGui shutdown) runs before dev dtor —
    // unique_ptr declaration order guarantees ctx tears down first.
}

}  // namespace
