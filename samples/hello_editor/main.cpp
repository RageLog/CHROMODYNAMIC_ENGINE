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

#include <cd/asset_json/Json.hpp>
#include <cd/ecs/World.hpp>
#include <cd/editor/EditHistory.hpp>
#include <cd/editor/TransformCommands.hpp>
#include <cd/imgui/Context.hpp>
#include <cd/material/Material.hpp>
#include <cd/math/Matrix.hpp>
#include <cd/math/Quaternion.hpp>
#include <cd/math/Transform.hpp>
#include <cd/math/Vector.hpp>
#include <cd/platform/Window.hpp>
#include <cd/render/Renderer.hpp>
#include <cd/rhi/ICommandBuffer.hpp>
#include <cd/rhi/IDevice.hpp>
#include <cd/rhi_vulkan/VulkanDevice.hpp>
#include <cd/scene/Scene.hpp>
#include <cd/scene/Serializer.hpp>
#include <cd/shader/Compiler.hpp>
#include <imgui.h>
#include <imgui_internal.h>  // DockBuilder API (Phase 16.A)

#include <algorithm>
#include <chrono>
#include <cmath>

#include <array>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace
{

struct SceneEntity
{
    cd::ecs::Entity handle {};
    std::string name;
    cd::math::Vec3f tint { 1.0F, 1.0F, 1.0F };
};

// Cube geometry shared across every entity in the viewport.
struct CubeVertex { float pos[3]; float color[3]; };

constexpr std::array<CubeVertex, 8> kCubeVerts {{
    { { -0.5F, -0.5F, -0.5F }, { 0.0F, 0.0F, 0.0F } },
    { {  0.5F, -0.5F, -0.5F }, { 1.0F, 0.0F, 0.0F } },
    { {  0.5F,  0.5F, -0.5F }, { 1.0F, 1.0F, 0.0F } },
    { { -0.5F,  0.5F, -0.5F }, { 0.0F, 1.0F, 0.0F } },
    { { -0.5F, -0.5F,  0.5F }, { 0.0F, 0.0F, 1.0F } },
    { {  0.5F, -0.5F,  0.5F }, { 1.0F, 0.0F, 1.0F } },
    { {  0.5F,  0.5F,  0.5F }, { 1.0F, 1.0F, 1.0F } },
    { { -0.5F,  0.5F,  0.5F }, { 0.0F, 1.0F, 1.0F } },
}};
constexpr std::array<std::uint16_t, 36> kCubeIndices {
    0,1,2, 0,2,3,  4,6,5, 4,7,6,
    0,3,7, 0,7,4,  1,5,6, 1,6,2,
    0,4,5, 0,5,1,  3,2,6, 3,6,7,
};

constexpr const char* kViewportVS = R"glsl(
#version 450
layout(push_constant) uniform PC {
    mat4 mvp;
    vec4 tint;  // .rgb modulates vertex color; .a unused
} pc;
layout(location = 0) in vec3 in_pos;
layout(location = 1) in vec3 in_color;
layout(location = 0) out vec3 v_color;
void main() {
    vec4 clip = pc.mvp * vec4(in_pos, 1.0);
    clip.y = -clip.y;  // Vulkan NDC Y-down
    gl_Position = clip;
    v_color = in_color * pc.tint.rgb;
}
)glsl";

constexpr const char* kViewportFS = R"glsl(
#version 450
layout(location = 0) in  vec3 v_color;
layout(location = 0) out vec4 out_color;
void main() { out_color = vec4(v_color, 1.0); }
)glsl";

struct CubePushConstants {
    cd::math::Mat4f mvp {};
    float tint[4] { 1.0F, 1.0F, 1.0F, 1.0F };
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

    // Re-enable docking (Phase 16.A). DockBuilder constructs the
    // default layout on the first frame; user-resized splits and
    // moved panels then persist via imgui.ini across runs.
    ImGuiIO& imgui_io = ImGui::GetIO();
    imgui_io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    bool dock_initialised = false;

    // ---- 3D viewport resources (Phase 14.E) --------------------------------
    // Geometry + material shared across every entity. Per-entity state
    // (tint, MVP) is pushed via push constants in the draw loop.
    auto make_upload_buf = [&](std::span<const std::byte> bytes,
                               cd::rhi::BufferUsage usage) -> cd::rhi::BufferHandle {
        cd::rhi::BufferDesc bd {};
        bd.size = bytes.size();
        bd.usage = usage;
        bd.memory = cd::rhi::MemoryUsage::kCpuToGpu;
        auto r = device.create_buffer(bd);
        if (!r.has_value()) return {};
        (void)device.upload_buffer(*r, 0, bytes);
        return *r;
    };
    const std::span<const std::byte> vb_bytes {
        reinterpret_cast<const std::byte*>(kCubeVerts.data()),
        kCubeVerts.size() * sizeof(CubeVertex) };
    const std::span<const std::byte> ib_bytes {
        reinterpret_cast<const std::byte*>(kCubeIndices.data()),
        kCubeIndices.size() * sizeof(std::uint16_t) };
    auto cube_vb = make_upload_buf(vb_bytes, cd::rhi::BufferUsage::kVertex);
    auto cube_ib = make_upload_buf(ib_bytes, cd::rhi::BufferUsage::kIndex);

    // Depth target for the viewport (rebuilt on resize).
    constexpr auto kDepthFormat = cd::rhi::Format::kD32Float;
    cd::rhi::TextureHandle depth_image {};
    cd::rhi::TextureViewHandle depth_view {};
    auto make_depth = [&](cd::rhi::Extent2D size) {
        if (depth_view.is_valid()) device.destroy_texture_view(depth_view);
        if (depth_image.is_valid()) device.destroy_texture(depth_image);
        cd::rhi::TextureDesc td {};
        td.type = cd::rhi::TextureType::k2D;
        td.format = kDepthFormat;
        td.extent = { size.width, size.height, 1 };
        td.usage = cd::rhi::TextureUsage::kDepthStencilAttachment;
        td.memory = cd::rhi::MemoryUsage::kGpuOnly;
        auto t = device.create_texture(td);
        if (!t.has_value()) return false;
        cd::rhi::TextureViewDesc vd {};
        vd.texture = *t;
        vd.format = kDepthFormat;
        vd.mip_count = 1;
        vd.layer_count = 1;
        auto v = device.create_texture_view(vd);
        if (!v.has_value()) { device.destroy_texture(*t); return false; }
        depth_image = *t;
        depth_view = *v;
        return true;
    };
    make_depth({ window.width(), window.height() });

    // Shader + material for the cube.
    auto compiler = cd::shader::make_glslang_compiler();
    constexpr std::array<cd::rhi::VertexBinding, 1> kVtxBindings {
        cd::rhi::VertexBinding { 0, sizeof(CubeVertex), false }
    };
    constexpr std::array<cd::rhi::VertexAttribute, 2> kVtxAttrs {
        cd::rhi::VertexAttribute { 0, 0, cd::rhi::Format::kRGB32Float, offsetof(CubeVertex, pos)   },
        cd::rhi::VertexAttribute { 1, 0, cd::rhi::Format::kRGB32Float, offsetof(CubeVertex, color) }
    };
    constexpr std::array<cd::rhi::Format, 1> kColorFormats { cd::rhi::Format::kBGRA8Unorm };
    constexpr std::array<cd::rhi::PushConstantRange, 1> kPush {
        cd::rhi::PushConstantRange {
            .stages = cd::rhi::ShaderStage::kVertex,
            .offset = 0,
            .size = static_cast<std::uint32_t>(sizeof(CubePushConstants)) }
    };
    cd::material::MaterialDesc cube_md {};
    cube_md.vertex_glsl = kViewportVS;
    cube_md.fragment_glsl = kViewportFS;
    cube_md.vertex_bindings = kVtxBindings;
    cube_md.vertex_attributes = kVtxAttrs;
    cube_md.color_attachment_formats = kColorFormats;
    cube_md.depth_attachment_format = kDepthFormat;
    cube_md.push_constants = kPush;
    cube_md.topology = cd::rhi::PrimitiveTopology::kTriangleList;
    cube_md.raster.cull = cd::rhi::CullMode::kBack;
    cube_md.depth_stencil.depth_test = true;
    cube_md.depth_stencil.depth_write = true;
    cube_md.depth_stencil.depth_compare = cd::rhi::CompareOp::kLess;
    cube_md.name = "viewport_cube";
    auto cube_mat_r = cd::material::Material::create(device, compiler.get(), cube_md);

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
    {
        struct Init { const char* name; cd::math::Vec3f pos; cd::math::Vec3f tint; };
        const std::array<Init, 3> seeds {{
            { "Cube",   { -1.6F, 0.0F, 0.0F }, { 1.0F, 0.4F, 0.4F } },
            { "Sphere", {  0.0F, 0.0F, 0.0F }, { 0.4F, 1.0F, 0.4F } },
            { "Cone",   {  1.6F, 0.0F, 0.0F }, { 0.4F, 0.4F, 1.0F } },
        }};
        for (const auto& s : seeds)
        {
            SceneEntity e;
            e.handle = scene.create_node();
            e.name = s.name;
            e.tint = s.tint;
            scene.local(e.handle)->value.position = s.pos;
            entities.push_back(std::move(e));
        }
    }
    log_push("Spawned 3 entities (Cube, Sphere, Cone) with tinted cube meshes");

    int selected = 0;

    // Save/Load default file path. The editor writes/reads
    // hello_editor.cdscene.json next to the binary; the
    // ImGui InputText below lets users change it at runtime.
    std::array<char, 256> path_buf {};
    {
        const char* default_path = "hello_editor.cdscene.json";
        for (std::size_t i = 0; i < std::strlen(default_path); ++i)
            path_buf[i] = default_path[i];
    }

    // ---- Free-fly camera state (Phase 15.A) --------------------------------
    // Initial pose looks down toward the cubes. W/A/S/D translate along
    // camera-relative axes; right-mouse drag rotates yaw/pitch. ImGui
    // input gates so dragging over a panel doesn't move the camera.
    struct FlyCamera {
        cd::math::Vec3f position { 0.0F, 2.0F, 6.0F };
        // yaw = -π/2 makes forward = (0, *, -1) — looking down -Z
        // toward the cubes at origin. Earlier (yaw = π → forward = -X)
        // pointed away from the cubes so the viewport was empty.
        float yaw { -1.5707963F };
        float pitch { -0.25F };    // slight downward tilt
        float speed { 3.0F };      // units / second
        float mouse_sensitivity { 0.005F };
    } camera;
    auto last_time = std::chrono::steady_clock::now();

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
            make_depth({ window.width(), window.height() });
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
        cd::rhi::DepthStencilAttachmentInfo depth_attach {};
        depth_attach.view = depth_view;
        depth_attach.depth_load = cd::rhi::LoadOp::kClear;
        depth_attach.depth_store = cd::rhi::StoreOp::kStore;
        depth_attach.clear.depth = 1.0F;
        cd::rhi::RenderPassBeginInfo rp {};
        rp.render_area = cd::rhi::Rect2D { { 0, 0 }, frame.extent };
        rp.color_attachments = color_attach;
        if (cube_mat_r.has_value())
            rp.depth_stencil = &depth_attach;
        cmd.begin_render_pass(rp);
        cmd.set_viewport(cd::rhi::Viewport {
            0.0F, 0.0F,
            static_cast<float>(frame.extent.width),
            static_cast<float>(frame.extent.height),
            0.0F, 1.0F });
        cmd.set_scissor(cd::rhi::Rect2D { { 0, 0 }, frame.extent });

        // ---- 3D viewport pass: one tinted cube per entity ------------------
        if (cube_mat_r.has_value() && cube_vb.is_valid() && cube_ib.is_valid())
        {
            auto& cube_mat = *cube_mat_r;
            // Free-fly camera (Phase 15.A) — WASD translate + right-mouse
            // drag rotate. ImGui input gates ensure dragging across a
            // panel does not move the camera.
            const auto t_now = std::chrono::steady_clock::now();
            const float dt =
                std::chrono::duration<float>(t_now - last_time).count();
            last_time = t_now;

            // Only steer the camera when the cursor isn't hovering a
            // panel (the IO flag tracks "ImGui wants the mouse").
            // For keyboard, also consult WantTextInput which is
            // specifically set when an InputFloat / InputText has
            // focus (WantCaptureKeyboard alone misses some focus
            // transitions in the ImGui docking branch).
            const bool ui_wants_mouse = imgui_io.WantCaptureMouse;
            // For keyboard: gate ONLY on WantTextInput (an active text
            // widget). WantCaptureKeyboard is more aggressive — it
            // stays true when any window has focus, which blocked
            // WASD entirely after the user clicked a panel once.
            const bool ui_wants_kbd = imgui_io.WantTextInput;

            // Right-mouse drag → yaw/pitch.
            if (!ui_wants_mouse && ImGui::IsMouseDown(ImGuiMouseButton_Right))
            {
                const ImVec2 drag = ImGui::GetMouseDragDelta(ImGuiMouseButton_Right);
                // Drag right → camera looks right. Drag up → looks up.
                // (FPS / Unity / UE convention: drag direction equals
                // gaze-direction change.)
                camera.yaw   += drag.x * camera.mouse_sensitivity;
                camera.pitch -= drag.y * camera.mouse_sensitivity;
                camera.pitch = std::clamp(camera.pitch, -1.55F, 1.55F);
                ImGui::ResetMouseDragDelta(ImGuiMouseButton_Right);
            }

            // Forward / right vectors derived from yaw + pitch.
            const float cp = std::cos(camera.pitch);
            const cd::math::Vec3f forward {
                std::cos(camera.yaw) * cp,
                std::sin(camera.pitch),
                std::sin(camera.yaw) * cp
            };
            const cd::math::Vec3f world_up { 0.0F, 1.0F, 0.0F };
            // Right = forward × up in a right-handed Y-up frame.
            // forward = (0, *, -1) (looking -Z) ⇒ right = (1, 0, 0)
            // = world +X. Previous formula had the sign inverted,
            // making A and D swap (user-reported).
            const cd::math::Vec3f right {
                -forward.z, 0.0F, forward.x
            };

            // WASD translation.
            if (!ui_wants_kbd)
            {
                const float step = camera.speed * dt;
                if (ImGui::IsKeyDown(ImGuiKey_W))
                {
                    camera.position.x += forward.x * step;
                    camera.position.y += forward.y * step;
                    camera.position.z += forward.z * step;
                }
                if (ImGui::IsKeyDown(ImGuiKey_S))
                {
                    camera.position.x -= forward.x * step;
                    camera.position.y -= forward.y * step;
                    camera.position.z -= forward.z * step;
                }
                if (ImGui::IsKeyDown(ImGuiKey_D))
                {
                    camera.position.x += right.x * step;
                    camera.position.z += right.z * step;
                }
                if (ImGui::IsKeyDown(ImGuiKey_A))
                {
                    camera.position.x -= right.x * step;
                    camera.position.z -= right.z * step;
                }
                if (ImGui::IsKeyDown(ImGuiKey_E))
                    camera.position.y += step;
                if (ImGui::IsKeyDown(ImGuiKey_Q))
                    camera.position.y -= step;
            }

            const cd::math::Vec3f eye = camera.position;
            const cd::math::Vec3f target {
                eye.x + forward.x, eye.y + forward.y, eye.z + forward.z
            };
            const cd::math::Vec3f up = world_up;
            const auto view = cd::math::look_at(eye, target, up);
            const float aspect =
                static_cast<float>(frame.extent.width) /
                static_cast<float>(std::max(1u, frame.extent.height));
            const auto proj = cd::math::perspective<float>(
                0.9F /* ~50° fov */, aspect, 0.1F, 100.0F);
            const auto vp = proj * view;

            cmd.bind_graphics_pipeline(cube_mat.pipeline());
            cmd.bind_vertex_buffer(0, cube_vb, 0);
            cmd.bind_index_buffer(cube_ib, 0, cd::rhi::IndexType::kUInt16);

            for (const auto& ent : entities)
            {
                auto* lt = scene.local(ent.handle);
                if (lt == nullptr) continue;
                const auto model = cd::math::to_mat4(lt->value);
                CubePushConstants pc {};
                pc.mvp = vp * model;
                pc.tint[0] = ent.tint.x;
                pc.tint[1] = ent.tint.y;
                pc.tint[2] = ent.tint.z;
                pc.tint[3] = 1.0F;
                cmd.push_constants(cube_mat.pipeline_layout(),
                                   cd::rhi::ShaderStage::kVertex,
                                   0, sizeof(pc), &pc);
                cmd.draw_indexed(static_cast<std::uint32_t>(kCubeIndices.size()),
                                 1, 0, 0, 0);
            }
        }

        ctx.new_frame();

        // ---- DockSpace host + DockBuilder default layout (Phase 16.A) ----
        {
            const ImGuiViewport* main_vp = ImGui::GetMainViewport();
            ImGui::SetNextWindowPos(main_vp->WorkPos);
            ImGui::SetNextWindowSize(main_vp->WorkSize);
            ImGui::SetNextWindowViewport(main_vp->ID);
            const ImGuiWindowFlags host_flags =
                ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar |
                ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
                ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus |
                ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_NoBackground;
            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0F);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0F);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2 { 0, 0 });
            ImGui::Begin("##DockSpaceHost", nullptr, host_flags);
            ImGui::PopStyleVar(3);

            const ImGuiID dock_id = ImGui::GetID("CDDockSpace");

            // First-frame layout: split the dockspace into top
            // (Toolbar) / center (3 columns: Scene | viewport |
            // Inspector) / bottom (History log), then dock each
            // panel into its node.
            if (!dock_initialised && ImGui::DockBuilderGetNode(dock_id) == nullptr)
            {
                ImGui::DockBuilderRemoveNode(dock_id);
                // Bitwise-or between ImGuiDockNodeFlagsPrivate_
                // (DockSpace) and ImGuiDockNodeFlags_ (PassthruCentralNode)
                // is a deprecated enum-enum mix under -Wdeprecated-enum-
                // enum-conversion. Compose via the underlying int.
                const int dock_flags =
                    static_cast<int>(ImGuiDockNodeFlags_DockSpace) |
                    static_cast<int>(ImGuiDockNodeFlags_PassthruCentralNode);
                ImGui::DockBuilderAddNode(dock_id,
                    static_cast<ImGuiDockNodeFlags>(dock_flags));
                ImGui::DockBuilderSetNodeSize(dock_id, main_vp->WorkSize);

                ImGuiID dock_main = dock_id;
                ImGuiID dock_top = ImGui::DockBuilderSplitNode(
                    dock_main, ImGuiDir_Up,   0.15F, nullptr, &dock_main);
                ImGuiID dock_bot = ImGui::DockBuilderSplitNode(
                    dock_main, ImGuiDir_Down, 0.22F, nullptr, &dock_main);
                ImGuiID dock_left = ImGui::DockBuilderSplitNode(
                    dock_main, ImGuiDir_Left, 0.18F, nullptr, &dock_main);
                ImGuiID dock_right = ImGui::DockBuilderSplitNode(
                    dock_main, ImGuiDir_Right, 0.25F, nullptr, &dock_main);

                ImGui::DockBuilderDockWindow("Toolbar",     dock_top);
                ImGui::DockBuilderDockWindow("Scene",       dock_left);
                ImGui::DockBuilderDockWindow("Inspector",   dock_right);
                ImGui::DockBuilderDockWindow("History log", dock_bot);
                ImGui::DockBuilderFinish(dock_id);
                dock_initialised = true;
            }

            ImGui::DockSpace(dock_id, ImVec2 { 0, 0 },
                             ImGuiDockNodeFlags_PassthruCentralNode);
            ImGui::End();
        }

        // ---- Default layout (legacy SetNextWindowPos removed now
        //      that DockBuilder handles initial placement) --------------------
        // ImGui auto-layout scatters new windows in the top-left and
        // overlaps them. Pin the four panels to deterministic positions
        // on first use so the editor opens with a sensible layout. The
        // ImGuiCond_FirstUseEver guard means user-resized / moved
        // windows survive across frames.
        const float vw = static_cast<float>(frame.extent.width);
        const float vh = static_cast<float>(frame.extent.height);
        const float gutter = 8.0F;
        const float toolbar_h = 130.0F;
        const float scene_w = 240.0F;
        const float inspector_w = 380.0F;
        const float history_h = 200.0F;
        // Toolbar — top, full width.
        ImGui::SetNextWindowPos(ImVec2 { gutter, gutter }, ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(
            ImVec2 { vw - 2 * gutter, toolbar_h }, ImGuiCond_FirstUseEver);

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
            // "next undo" on its own line so the toolbar doesn't
            // horizontally overflow on narrower windows.
            if (can_undo)
            {
                std::string lbl { history.next_undo_label() };
                ImGui::TextDisabled("next undo: %s", lbl.c_str());
            }

            // ---- Save / Load (Phase 14.A) ------------------------------
            ImGui::Separator();
            ImGui::SetNextItemWidth(420);
            ImGui::InputText("scene path", path_buf.data(), path_buf.size());
            ImGui::SameLine();
            if (ImGui::Button("Save"))
            {
                const auto json = cd::scene::serialize_scene(scene);
                const auto text = cd::asset_json::serialize(json, true);
                std::ofstream f(path_buf.data(), std::ios::binary);
                if (f)
                {
                    f.write(text.data(), static_cast<std::streamsize>(text.size()));
                    log_push(std::string { "save: " } + path_buf.data() +
                             " (" + std::to_string(text.size()) + " bytes)");
                }
                else
                {
                    log_push(std::string { "save failed: " } + path_buf.data());
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Load"))
            {
                std::ifstream f(path_buf.data(), std::ios::binary);
                if (!f)
                {
                    log_push(std::string { "load failed (no file): " } + path_buf.data());
                }
                else
                {
                    std::stringstream ss;
                    ss << f.rdbuf();
                    const auto text = ss.str();
                    auto json_r = cd::asset_json::parse(text);
                    if (!json_r.has_value())
                    {
                        log_push("load failed (parse error)");
                    }
                    else
                    {
                        // Fresh world + scene; rebuild the entity list from
                        // the deserialized id map. The EditHistory becomes
                        // invalid because the old entities are gone; clear
                        // it (per cd::editor::EditHistory contract).
                        world = cd::ecs::World {};
                        scene = cd::scene::Scene { world };
                        history.clear();
                        entities.clear();
                        auto map_r = cd::scene::deserialize_scene(scene, *json_r);
                        if (!map_r.has_value())
                        {
                            log_push("load failed (deserialize error)");
                        }
                        else
                        {
                            // Re-attach human-readable names. The
                            // serializer doesn't preserve names yet, so we
                            // tag them n_0, n_1, ... in iteration order.
                            std::size_t i = 0;
                            scene.for_each_node(
                                [&](cd::ecs::Entity e, cd::scene::LocalTransform&)
                                {
                                    SceneEntity se;
                                    se.handle = e;
                                    se.name = "n_" + std::to_string(i++);
                                    entities.push_back(std::move(se));
                                });
                            selected = entities.empty() ? -1 : 0;
                            log_push(std::string { "load: " } + path_buf.data() +
                                     " (" + std::to_string(entities.size()) + " entities)");
                        }
                    }
                }
            }
        }
        ImGui::End();

        // ---- Scene tree ---------------------------------------------------
        // Left column under the toolbar.
        ImGui::SetNextWindowPos(
            ImVec2 { gutter, gutter + toolbar_h + gutter }, ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(
            ImVec2 { scene_w, vh - toolbar_h - history_h - 4 * gutter },
            ImGuiCond_FirstUseEver);
        ImGui::Begin("Scene");
        for (std::size_t i = 0; i < entities.size(); ++i)
        {
            const bool is_selected = (selected == static_cast<int>(i));
            if (ImGui::Selectable(entities[i].name.c_str(), is_selected))
                selected = static_cast<int>(i);
        }
        ImGui::End();

        // ---- Inspector ----------------------------------------------------
        // Right column under the toolbar.
        ImGui::SetNextWindowPos(
            ImVec2 { vw - inspector_w - gutter, gutter + toolbar_h + gutter },
            ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(
            ImVec2 { inspector_w, vh - toolbar_h - history_h - 4 * gutter },
            ImGuiCond_FirstUseEver);
        ImGui::Begin("Inspector");
        if (selected >= 0 && selected < static_cast<int>(entities.size()))
        {
            auto& ent = entities[static_cast<std::size_t>(selected)];
            if (auto* lt = scene.local(ent.handle); lt != nullptr)
            {
                ImGui::Text("Entity: %s", ent.name.c_str());
                ImGui::Separator();

                // Position — Apply Translate Delta. Use array to
                // guarantee contiguous storage for InputFloat3
                // (separate static floats are NOT guaranteed
                // contiguous by the C++ standard).
                static float trans[3] { 0.0F, 0.0F, 0.0F };
                ImGui::Text("Position: (%.2f, %.2f, %.2f)",
                            static_cast<double>(lt->value.position.x),
                            static_cast<double>(lt->value.position.y),
                            static_cast<double>(lt->value.position.z));
                ImGui::InputFloat3("translate delta", trans);
                if (ImGui::Button("Apply Translate"))
                {
                    history.push(std::make_unique<cd::editor::TranslateCommand>(
                        scene, ent.handle,
                        cd::math::Vec3f { trans[0], trans[1], trans[2] }));
                    log_push("push: TranslateCommand");
                    trans[0] = trans[1] = trans[2] = 0.0F;
                }

                ImGui::Separator();
                // Scale — Apply Scale Factor
                static float scale[3] { 1.0F, 1.0F, 1.0F };
                ImGui::Text("Scale: (%.2f, %.2f, %.2f)",
                            static_cast<double>(lt->value.scale.x),
                            static_cast<double>(lt->value.scale.y),
                            static_cast<double>(lt->value.scale.z));
                ImGui::InputFloat3("scale factor", scale);
                if (ImGui::Button("Apply Scale"))
                {
                    history.push(std::make_unique<cd::editor::ScaleCommand>(
                        scene, ent.handle,
                        cd::math::Vec3f { scale[0], scale[1], scale[2] }));
                    log_push("push: ScaleCommand");
                    scale[0] = scale[1] = scale[2] = 1.0F;
                }

                ImGui::Separator();
                // Rotation — Euler XYZ (degrees) → quaternion. User-
                // friendly: editing raw quat xyzw is unintuitive; the
                // backend EditHistory still stores the quaternion form.
                static float euler_deg[3] { 0.0F, 0.0F, 0.0F };
                ImGui::Text("Rotation: (%.2f, %.2f, %.2f, %.2f) quat",
                            static_cast<double>(lt->value.rotation.x),
                            static_cast<double>(lt->value.rotation.y),
                            static_cast<double>(lt->value.rotation.z),
                            static_cast<double>(lt->value.rotation.w));
                ImGui::InputFloat3("euler xyz (deg)", euler_deg);
                if (ImGui::Button("Apply Rotate"))
                {
                    // Euler ZYX intrinsic → quaternion. Each axis
                    // rotation builds its own quat, then composes
                    // in Z * Y * X order (common engine convention).
                    constexpr float kDeg2Rad = 3.14159265358979F / 180.0F;
                    const float hx = euler_deg[0] * kDeg2Rad * 0.5F;
                    const float hy = euler_deg[1] * kDeg2Rad * 0.5F;
                    const float hz = euler_deg[2] * kDeg2Rad * 0.5F;
                    const float cx = std::cos(hx), sx = std::sin(hx);
                    const float cy = std::cos(hy), sy = std::sin(hy);
                    const float cz = std::cos(hz), sz = std::sin(hz);
                    // ZYX composition:  q = qz * qy * qx
                    cd::math::Quatf q;
                    q.w = cz * cy * cx + sz * sy * sx;
                    q.x = cz * cy * sx - sz * sy * cx;
                    q.y = cz * sy * cx + sz * cy * sx;
                    q.z = sz * cy * cx - cz * sy * sx;
                    history.push(std::make_unique<cd::editor::RotateCommand>(
                        scene, ent.handle, q));
                    log_push("push: RotateCommand (from Euler)");
                    euler_deg[0] = euler_deg[1] = euler_deg[2] = 0.0F;
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
        // Bottom strip, full width.
        ImGui::SetNextWindowPos(
            ImVec2 { gutter, vh - history_h - gutter }, ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(
            ImVec2 { vw - 2 * gutter, history_h }, ImGuiCond_FirstUseEver);
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
