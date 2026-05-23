// =============================================================================
// CHROMODYNAMIC — samples/hello_editor
//
// v0.33.0 / Phase 13.D — first end-to-end sample wiring the editor
// primitives (EditHistory + TransformCommands) into a real ImGui UI.
//
// Per the Phase 13.D ADR: the existing cd::editor + cd::editor_ui
// stack is cd::ui-driven and stays as headless test scaffolding. The
// "interactive editor" use case sits on top of ImGui directly because
// every render sample already speaks cd::imgui_backend; the bridge
// problem is solved by re-using the World + Scene + EditHistory
// primitives without going through cd::ui at all.
//
// What this sample demonstrates:
//   * Boots a cd::ecs::World + cd::scene::Scene + cd::editor::EditHistory.
//   * Spawns three named entities ("Cube", "Sphere", "Cone") with
//     identity LocalTransforms.
//   * Two panels:
//       - Scene tree (selectable rows; selection drives the inspector)
//       - Inspector (position / scale / rotation fields; edits push
//         TranslateCommand / ScaleCommand / RotateCommand through
//         EditHistory).
//   * Undo / Redo toolbar with live "Undo X" / "Redo X" labels.
//   * Console-style log of the last 32 history events.
//
// The 3D viewport is intentionally NOT here — adding it would
// require a real Renderer-driven scene draw which is the Phase 14
// editor-viewport candidate. The marathon-shippable cut is the UX
// shape that proves "the primitives compose"; the viewport closes
// the visual loop later.
// =============================================================================
#include "SampleRuntime.hpp"

#include <cd/ecs/World.hpp>
#include <cd/editor/EditHistory.hpp>
#include <cd/editor/TransformCommands.hpp>
#include <cd/imgui/Context.hpp>
#include <cd/math/Quaternion.hpp>
#include <cd/math/Vector.hpp>
#include <cd/platform/Window.hpp>
#include <cd/render/Renderer.hpp>
#include <cd/rhi/ICommandBuffer.hpp>
#include <cd/rhi/IDevice.hpp>
#include <cd/rhi_vulkan/VulkanDevice.hpp>
#include <cd/scene/Scene.hpp>
#include <imgui.h>

#include <array>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <string>
#include <vector>

namespace
{

struct SceneEntity
{
    cd::ecs::Entity handle {};
    std::string name;
};

}  // namespace

int main(int argc, char** argv)
{
    const cd::sample::Runtime runtime = cd::sample::parse_runtime(argc, argv);

    cd::platform::WindowDesc wd {};
    wd.title = "CHROMODYNAMIC — hello_editor";
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
        return 4;
    auto& ctx = **ctx_r;

    // ---- World / Scene / EditHistory ---------------------------------------
    cd::ecs::World world;
    cd::scene::Scene scene { world };
    cd::editor::EditHistory history;
    std::deque<std::string> log;
    auto log_push = [&log](std::string s) {
        log.emplace_back(std::move(s));
        while (log.size() > 32) log.pop_front();
    };

    std::vector<SceneEntity> entities;
    for (const char* name : { "Cube", "Sphere", "Cone" })
    {
        SceneEntity e;
        e.handle = scene.create_node();
        e.name = name;
        entities.push_back(std::move(e));
    }
    log_push("Spawned 3 entities (Cube, Sphere, Cone)");

    int selected = 0;

    std::printf("hello_editor: ready. ESC to exit.\n");
    std::fflush(stdout);

    std::vector<cd::platform::OSEvent> events;
    events.reserve(64);
    bool needs_rebuild = false;
    std::uint32_t frame_idx = 0;
    while (true)
    {
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
            return 5;
        }
        auto& frame = *frame_r;
        auto& cmd = *frame.command_buffer;

        std::array<cd::rhi::ColorAttachmentInfo, 1> color_attach {
            cd::rhi::ColorAttachmentInfo {
                .view = frame.swapchain_image_view,
                .load_op = cd::rhi::LoadOp::kClear,
                .store_op = cd::rhi::StoreOp::kStore,
                .clear_color = { .f32 = { 0.12F, 0.13F, 0.16F, 1.0F } }
            }
        };
        cd::rhi::RenderPassBeginInfo rp {};
        rp.render_area = cd::rhi::Rect2D { { 0, 0 }, frame.extent };
        rp.color_attachments = color_attach;
        cmd.begin_render_pass(rp);
        cmd.set_viewport(cd::rhi::Viewport {
            0.0F, 0.0F,
            static_cast<float>(frame.extent.width),
            static_cast<float>(frame.extent.height),
            0.0F, 1.0F });
        cmd.set_scissor(cd::rhi::Rect2D { { 0, 0 }, frame.extent });

        ctx.new_frame();

        // ---- Toolbar (undo / redo / counts) --------------------------------
        ImGui::Begin("Toolbar");
        {
            const bool can_undo = history.can_undo();
            const bool can_redo = history.can_redo();
            if (!can_undo) ImGui::BeginDisabled();
            if (ImGui::Button("Undo"))
            {
                if (history.undo())
                {
                    std::string lbl { history.next_redo_label() };
                    log_push("undo: " + (lbl.empty() ? std::string {} : lbl));
                }
            }
            if (!can_undo) ImGui::EndDisabled();
            ImGui::SameLine();
            if (!can_redo) ImGui::BeginDisabled();
            if (ImGui::Button("Redo"))
            {
                std::string lbl { history.next_redo_label() };
                if (history.redo())
                    log_push("redo: " + (lbl.empty() ? std::string {} : lbl));
            }
            if (!can_redo) ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::Text("depth: undo=%zu redo=%zu  (bytes=%zu)",
                        history.undo_depth(), history.redo_depth(),
                        history.bytes_in_use());
            if (can_undo)
            {
                ImGui::SameLine();
                std::string lbl { history.next_undo_label() };
                ImGui::TextDisabled("next undo: %s", lbl.c_str());
            }
        }
        ImGui::End();

        // ---- Scene tree ---------------------------------------------------
        ImGui::Begin("Scene");
        for (std::size_t i = 0; i < entities.size(); ++i)
        {
            const bool is_selected = (selected == static_cast<int>(i));
            if (ImGui::Selectable(entities[i].name.c_str(), is_selected))
                selected = static_cast<int>(i);
        }
        ImGui::End();

        // ---- Inspector ----------------------------------------------------
        ImGui::Begin("Inspector");
        if (selected >= 0 && selected < static_cast<int>(entities.size()))
        {
            auto& ent = entities[static_cast<std::size_t>(selected)];
            if (auto* lt = scene.local(ent.handle); lt != nullptr)
            {
                ImGui::Text("Entity: %s", ent.name.c_str());
                ImGui::Separator();

                // Position — Apply Translate Delta
                static float dx = 0.0F, dy = 0.0F, dz = 0.0F;
                ImGui::Text("Position: (%.2f, %.2f, %.2f)",
                            static_cast<double>(lt->value.position.x),
                            static_cast<double>(lt->value.position.y),
                            static_cast<double>(lt->value.position.z));
                ImGui::InputFloat3("translate delta", &dx);
                if (ImGui::Button("Apply Translate"))
                {
                    history.push(std::make_unique<cd::editor::TranslateCommand>(
                        scene, ent.handle, cd::math::Vec3f { dx, dy, dz }));
                    log_push("push: TranslateCommand");
                    dx = dy = dz = 0.0F;
                }

                ImGui::Separator();
                // Scale — Apply Scale Factor
                static float sx = 1.0F, sy = 1.0F, sz = 1.0F;
                ImGui::Text("Scale: (%.2f, %.2f, %.2f)",
                            static_cast<double>(lt->value.scale.x),
                            static_cast<double>(lt->value.scale.y),
                            static_cast<double>(lt->value.scale.z));
                ImGui::InputFloat3("scale factor", &sx);
                if (ImGui::Button("Apply Scale"))
                {
                    history.push(std::make_unique<cd::editor::ScaleCommand>(
                        scene, ent.handle, cd::math::Vec3f { sx, sy, sz }));
                    log_push("push: ScaleCommand");
                    sx = sy = sz = 1.0F;
                }

                ImGui::Separator();
                // Rotation — quaternion replacement
                static float qx = 0.0F, qy = 0.0F, qz = 0.0F, qw = 1.0F;
                ImGui::Text("Rotation: (%.2f, %.2f, %.2f, %.2f)",
                            static_cast<double>(lt->value.rotation.x),
                            static_cast<double>(lt->value.rotation.y),
                            static_cast<double>(lt->value.rotation.z),
                            static_cast<double>(lt->value.rotation.w));
                ImGui::InputFloat4("new quat (xyzw)", &qx);
                if (ImGui::Button("Apply Rotate"))
                {
                    history.push(std::make_unique<cd::editor::RotateCommand>(
                        scene, ent.handle, cd::math::Quatf { qx, qy, qz, qw }));
                    log_push("push: RotateCommand");
                    qx = qy = qz = 0.0F;
                    qw = 1.0F;
                }
            }
            else
            {
                ImGui::TextDisabled("entity has no LocalTransform");
            }
        }
        else
        {
            ImGui::TextDisabled("no selection");
        }
        ImGui::End();

        // ---- History log --------------------------------------------------
        ImGui::Begin("History log");
        for (auto it = log.rbegin(); it != log.rend(); ++it)
            ImGui::TextUnformatted(it->c_str());
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
            return 6;
        }
        ++frame_idx;
    }

    renderer.wait_idle();
    std::printf("hello_editor: clean exit (%u frames). undo depth=%zu redo depth=%zu\n",
                frame_idx, history.undo_depth(), history.redo_depth());
    return 0;
}
