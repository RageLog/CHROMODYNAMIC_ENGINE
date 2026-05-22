// =============================================================================
// CHROMODYNAMIC — samples/hello_imgui/main.cpp
//
// First end-to-end ImGui demo. Boots cd::imgui::Context against the
// existing Renderer + swapchain, opens the canonical ImGui demo window,
// and adds a small custom panel reading cd::profile::StatsAggregator.
//
// Render order per frame:
//   1. begin_frame (renderer transitions swapchain image to COLOR)
//   2. cmd.begin_render_pass(clear)
//   3. ctx.new_frame(); user ImGui calls
//   4. ctx.render(cmd)   ← inside the render pass
//   5. cmd.end_render_pass(); end_frame
//
// Headless mode: --headless N keeps the window invisible-but-rendered for
// N frames, proving the ImGui pipeline cold-starts in CI.
// =============================================================================
#include "SampleRuntime.hpp"

#include <cd/imgui/Context.hpp>
#include <cd/platform/Window.hpp>
#include <cd/profile/BufferSink.hpp>
#include <cd/profile/Scope.hpp>
#include <cd/profile/StatsAggregator.hpp>
#include <cd/render/Renderer.hpp>
#include <cd/rhi/Barriers.hpp>
#include <cd/rhi/ICommandBuffer.hpp>
#include <cd/rhi/IDevice.hpp>
#include <cd/rhi_vulkan/VulkanDevice.hpp>

#include <imgui.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <vector>

int main(int argc, char** argv)
{
    const cd::sample::Runtime runtime = cd::sample::parse_runtime(argc, argv);

    cd::platform::WindowDesc wd {};
    wd.title = "CHROMODYNAMIC — hello_imgui";
    wd.width = 1280;
    wd.height = 720;
    auto window_r = cd::platform::create_window(wd);
    if (!window_r.has_value())
        return 1;
    auto& window = **window_r;

    cd::rhi_vulkan::VulkanCreateInfo vci {};
    auto device_r = cd::rhi_vulkan::create_vulkan_device(vci);
    if (!device_r.has_value())
        return 2;
    auto& device = **device_r;

    cd::render::RendererDesc rd {};
    rd.device = &device;
    rd.swapchain.window_handle = window.native_window_handle();
    rd.swapchain.display_handle = window.native_display_handle();
    rd.swapchain.extent = { window.width(), window.height() };
    rd.swapchain.format = cd::rhi::Format::kBGRA8Unorm;
    rd.frames_in_flight = 2;
    auto renderer_r = cd::render::Renderer::create(rd);
    if (!renderer_r.has_value())
        return 3;
    auto& renderer = *renderer_r;

    cd::imgui::InitDesc id {};
    id.window = &window;
    id.device = &device;
    id.color_format = cd::rhi::Format::kBGRA8Unorm;
    id.frames_in_flight = 2;
    auto ctx_r = cd::imgui::Context::create(id);
    if (!ctx_r.has_value())
    {
        std::fprintf(
            stderr,
            "imgui init: %.*s\n",
            static_cast<int>(ctx_r.error().message.size()),
            ctx_r.error().message.data()
        );
        return 4;
    }
    auto& ctx = **ctx_r;

    // Plug a BufferSink so the custom HUD panel has data to show.
    cd::profile::BufferSink sink { 4096 };
    auto* prev_sink = cd::profile::set_sink(&sink);
    cd::profile::StatsAggregator agg;

    std::printf("hello_imgui: ready. ESC to exit.\n");
    std::fflush(stdout);

    std::vector<cd::platform::OSEvent> events;
    events.reserve(64);
    bool needs_rebuild = false;
    std::uint32_t frame_idx = 0;
    bool show_demo = true;

    while (true)
    {
        {
            CD_PROFILE_SCOPE("frame");

            if (!runtime.should_continue(frame_idx))
                window.request_close();
            events.clear();
            if (!window.pump_events(events))
                break;
            for (const auto& e : events)
            {
                ctx.handle_event(e);
                if (e.kind == cd::platform::OSEventKind::kKeyDown && e.key == cd::platform::KeyCode::kEscape)
                    window.request_close();
                else if (e.kind == cd::platform::OSEventKind::kResize)
                    needs_rebuild = true;
            }
            if (needs_rebuild)
            {
                if (window.width() == 0 || window.height() == 0)
                    continue;
                if (!renderer.recreate_swapchain({ window.width(), window.height() }).has_value())
                    continue;
                needs_rebuild = false;
            }

            auto frame_r = renderer.begin_frame();
            if (!frame_r.has_value())
            {
                if (frame_r.error().code ==
                    static_cast<std::uint32_t>(cd::render::render_errors::Code::kSwapchainOutOfDate))
                {
                    needs_rebuild = true;
                    continue;
                }
                cd::profile::set_sink(prev_sink);
                return 5;
            }
            auto& frame = *frame_r;
            auto& cmd = *frame.command_buffer;

            std::array<cd::rhi::ColorAttachmentInfo, 1> color_attach {
                cd::rhi::ColorAttachmentInfo {
                                              .view = frame.swapchain_image_view,
                                              .load_op = cd::rhi::LoadOp::kClear,
                                              .store_op = cd::rhi::StoreOp::kStore,
                                              .clear_color = { .f32 = { 0.10F, 0.10F, 0.12F, 1.0F } } }
            };
            cd::rhi::RenderPassBeginInfo rp {};
            rp.render_area = cd::rhi::Rect2D {
                { 0, 0 },
                frame.extent
            };
            rp.color_attachments = color_attach;
            cmd.begin_render_pass(rp);
            cmd.set_viewport(
                cd::rhi::Viewport { 0.0F,
                                    0.0F,
                                    static_cast<float>(frame.extent.width),
                                    static_cast<float>(frame.extent.height),
                                    0.0F,
                                    1.0F }
            );
            cmd.set_scissor(
                cd::rhi::Rect2D {
                    { 0, 0 },
                    frame.extent
            }
            );

            // ---- ImGui frame -----------------------------------------------
            ctx.new_frame();
            if (show_demo)
                ImGui::ShowDemoWindow(&show_demo);

            // Custom CHROMODYNAMIC HUD reading the StatsAggregator. Rebuild
            // the rollup every frame from the live sink so the table updates.
            agg.reset();
            agg.apply(sink.snapshot());
            ImGui::Begin("CHROMODYNAMIC — profile HUD");
            ImGui::Text("Frame %u", frame_idx);
            ImGui::Separator();
            const auto rows = agg.snapshot();
            if (ImGui::BeginTable("scopes", 4))
            {
                ImGui::TableSetupColumn("scope");
                ImGui::TableSetupColumn("count");
                ImGui::TableSetupColumn("avg us");
                ImGui::TableSetupColumn("total ms");
                ImGui::TableHeadersRow();
                for (const auto& r : rows)
                {
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(r.name.c_str());
                    ImGui::TableNextColumn();
                    ImGui::Text("%llu", static_cast<unsigned long long>(r.count));
                    ImGui::TableNextColumn();
                    ImGui::Text("%.3f", r.avg_ns() / 1000.0);
                    ImGui::TableNextColumn();
                    ImGui::Text("%.3f", static_cast<double>(r.total_ns) / 1e6);
                }
                ImGui::EndTable();
            }
            ImGui::End();

            ctx.render(cmd);
            cmd.end_render_pass();

            auto end_r = renderer.end_frame();
            if (!end_r.has_value())
            {
                if (end_r.error().code ==
                    static_cast<std::uint32_t>(cd::render::render_errors::Code::kSwapchainOutOfDate))
                {
                    needs_rebuild = true;
                    continue;
                }
                cd::profile::set_sink(prev_sink);
                return 6;
            }
        }
        ++frame_idx;
    }

    renderer.wait_idle();
    cd::profile::set_sink(prev_sink);
    std::printf("hello_imgui: clean exit (%u frames).\n", frame_idx);
    return 0;
}
