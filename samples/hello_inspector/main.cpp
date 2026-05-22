// =============================================================================
// CHROMODYNAMIC — samples/hello_inspector
//
// Visual editor stub: ImGui scene-tree window + inspector window for the
// selected node. Demonstrates the cd::scene Scene::for_each_root and
// for_each_descendant enumeration API plus cd::imgui_backend on a real
// Vulkan swapchain.
//
// Scene topology built at startup:
//   root_a
//   ├── child_a1
//   │   └── grandchild
//   └── child_a2
//   root_b
//
// Inspector lets the user mutate the selected node's translation /
// scale via ImGui::DragFloat3. Headless mode (--headless N) skips the
// interactive loop after N frames — covers the smoke harness.
// =============================================================================
#include "SampleRuntime.hpp"

#include <cd/ecs/World.hpp>
#include <cd/imgui/Context.hpp>
#include <cd/math/Transform.hpp>
#include <cd/platform/Window.hpp>
#include <cd/render/Renderer.hpp>
#include <cd/rhi/Barriers.hpp>
#include <cd/rhi/ICommandBuffer.hpp>
#include <cd/rhi/IDevice.hpp>
#include <cd/rhi_vulkan/VulkanDevice.hpp>
#include <cd/scene/Scene.hpp>

#include <imgui.h>

#include <array>
#include <cstdint>
#include <cstdio>
#include <string>
#include <unordered_map>
#include <vector>

namespace
{

struct NodeLabel
{
    std::string name;
};

// Single-entity selection. cd::ecs::Entity is a 64-bit handle; we keep
// it value-typed and treat a default-constructed Entity as "no selection".
cd::ecs::Entity g_selected {};

void draw_tree_node(cd::scene::Scene& scene, cd::ecs::Entity ent,
                    const std::unordered_map<std::uint64_t, std::string>& labels)
{
    const auto it = labels.find(static_cast<std::uint64_t>(ent.id));
    const char* label = it != labels.end() ? it->second.c_str() : "<unnamed>";

    const auto* children = scene.children_of(ent);
    const bool has_kids = (children != nullptr) && !children->entities.empty();
    const ImGuiTreeNodeFlags flags = (has_kids ? 0 : ImGuiTreeNodeFlags_Leaf) |
                                     (ent == g_selected ? ImGuiTreeNodeFlags_Selected : 0) |
                                     ImGuiTreeNodeFlags_OpenOnArrow |
                                     ImGuiTreeNodeFlags_DefaultOpen;
    const bool open = ImGui::TreeNodeEx(reinterpret_cast<void*>(static_cast<std::uintptr_t>(ent.id)),
                                        flags, "%s [#%u]", label, ent.id);
    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
        g_selected = ent;
    if (open && has_kids)
    {
        for (const auto& c : children->entities)
            draw_tree_node(scene, c, labels);
        ImGui::TreePop();
    }
    else if (open && !has_kids)
    {
        ImGui::TreePop();
    }
}

}  // namespace

int main(int argc, char** argv)
{
    const cd::sample::Runtime runtime = cd::sample::parse_runtime(argc, argv);

    cd::platform::WindowDesc wd {};
    wd.title = "CHROMODYNAMIC — hello_inspector";
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

    // ---- Build the demo scene ----------------------------------------------
    cd::ecs::World ecs_world;
    cd::scene::Scene scene { ecs_world };
    std::unordered_map<std::uint64_t, std::string> labels;

    auto add = [&](const std::string& name, cd::ecs::Entity parent,
                   cd::math::Vec3f pos = {}) {
        auto e = scene.create_node();
        scene.local(e)->value.position = pos;
        labels[static_cast<std::uint64_t>(e.id)] = name;
        if (parent.is_valid())
            scene.attach(e, parent);
        return e;
    };

    auto root_a    = add("root_a", {}, { 0.0F, 0.0F, 0.0F });
    auto child_a1  = add("child_a1", root_a, { 1.0F, 0.0F, 0.0F });
    auto grandchild= add("grandchild", child_a1, { 0.0F, 1.0F, 0.0F });
    (void)add("child_a2", root_a, { -1.0F, 0.0F, 0.0F });
    (void)add("root_b", {}, { 3.0F, 0.0F, 0.0F });
    (void)grandchild;

    std::printf("hello_inspector: ready. Click a node in the tree, edit transform in the inspector. ESC to exit.\n");
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

        // ---- ImGui frame ---------------------------------------------------
        ctx.new_frame();

        // ----- Scene tree window -----
        ImGui::Begin("Scene tree");
        ImGui::Text("Frame %u | %zu nodes", frame_idx,
                    [&]() { std::size_t n = 0; scene.for_each_node([&](auto, auto&) { ++n; }); return n; }());
        ImGui::Separator();
        scene.for_each_root([&](cd::ecs::Entity e, cd::scene::LocalTransform&) {
            draw_tree_node(scene, e, labels);
        });
        ImGui::End();

        // ----- Inspector window -----
        ImGui::Begin("Inspector");
        if (g_selected.is_valid())
        {
            const auto it = labels.find(static_cast<std::uint64_t>(g_selected.id));
            ImGui::Text("Selected: %s [#%u]",
                        it != labels.end() ? it->second.c_str() : "<unnamed>",
                        g_selected.id);
            ImGui::Separator();
            auto* lt = scene.local(g_selected);
            if (lt != nullptr)
            {
                ImGui::DragFloat3("translation", &lt->value.position.x, 0.05F);
                ImGui::DragFloat3("scale",       &lt->value.scale.x,    0.05F, 0.01F, 100.0F);
                // Rotation is a quaternion; expose raw components but keep
                // them clamped to a unit sphere via re-normalize on edit.
                if (ImGui::DragFloat4("rotation (xyzw)", &lt->value.rotation.x, 0.01F, -1.0F, 1.0F))
                {
                    const auto x = lt->value.rotation.x;
                    const auto y = lt->value.rotation.y;
                    const auto z = lt->value.rotation.z;
                    const auto w = lt->value.rotation.w;
                    const float len = std::sqrt(x * x + y * y + z * z + w * w);
                    if (len > 1e-6F)
                    {
                        lt->value.rotation.x /= len;
                        lt->value.rotation.y /= len;
                        lt->value.rotation.z /= len;
                        lt->value.rotation.w /= len;
                    }
                }
            }
            const auto parent = scene.parent_of(g_selected);
            if (parent.is_valid())
            {
                const auto pit = labels.find(static_cast<std::uint64_t>(parent.id));
                ImGui::Text("Parent: %s [#%u]",
                            pit != labels.end() ? pit->second.c_str() : "<unnamed>",
                            parent.id);
            }
            else
            {
                ImGui::TextUnformatted("Parent: <none> (root)");
            }
        }
        else
        {
            ImGui::TextUnformatted("(click a node in the tree)");
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
            return 6;
        }
        ++frame_idx;
    }

    renderer.wait_idle();
    std::printf("hello_inspector: clean exit (%u frames).\n", frame_idx);
    return 0;
}
