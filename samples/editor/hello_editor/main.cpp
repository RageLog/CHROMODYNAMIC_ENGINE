// =============================================================================
// CHROMODYNAMIC — samples/hello_editor
//
// T3.2 / phase529-hello-editor-sample
// Scene-tree + inspector + viewport demo using cd::editor binary panels.
//
// What this sample demonstrates:
//   * Loads a glTF from assets/samples/Generic/ via the phase 508 generic
//     path (cd::asset::gltf::load_scene + cd::render::scene::ingest_gltf_scene).
//     Falls back to three synthetic primitives (Cube / Sphere / Cone) when
//     no file is found in the candidate directories — so the sample always
//     boots without requiring assets.
//
//   * Scene tree (left panel) built with cd::editor::HierarchyView:
//       - One row per live ECS entity carrying a LocalTransform.
//       - Collapsible tree — click the expand arrow to reveal children.
//       - Click a leaf row to select that entity; selection drives the
//         inspector.
//
//   * Inspector (right panel) binds to the selected entity's components:
//       - Position / Scale / Rotation (Euler, degrees) drag-fields.
//       - Rotate slider at the bottom of the inspector that mutates
//         Rotation.Y live — user drags and sees the viewport mesh spin.
//       - All drags push a RotateCommand / TranslateCommand / ScaleCommand
//         through EditHistory so Ctrl+Z / Ctrl+Y undo/redo works.
//
//   * Console panel (bottom strip) shows the last 32 editor log messages.
//       - Logs glTF load results, entity selection, and history events.
//
//   * 3D viewport renders the scene via a minimal Vulkan pass:
//       - Free-fly camera (W/A/S/D + right-mouse-drag).
//       - Per-entity push-constant MVP + tint.
//       - Rotation changes in the inspector immediately update the rendered
//         mesh on the same frame — no separate "apply" step.
//
//   * Undo / Redo toolbar with live depth labels.
//   * Ctrl+Shift+P command palette (fuzzy search over registered commands).
// =============================================================================
#include <SampleRuntime.hpp>

// Phase 508 generic glTF path lives in hello_engine's header set. We include
// it from there rather than copying to avoid drift. The CMakeLists exposes
// the hello_engine source directory via target_include_directories so this
// angle-bracket include resolves without copying any headers.
#include <HelloGenericGltf.hpp>

#include <cd/asset/json/Json.hpp>
#include <cd/ecs/World.hpp>
#include <cd/editor/CommandPalette.hpp>
#include <cd/editor/EditHistory.hpp>
#include <cd/editor/HierarchyView.hpp>
#include <cd/editor/TransformCommands.hpp>
#include <cd/editor/panel_inspector/Inspector.hpp>
#include <cd/editor/panel_console/Console.hpp>
#include <cd/imgui/Context.hpp>
#include <cd/debug_draw/DebugDraw.hpp>
#include <cd/editor/AxisGizmo.hpp>
#include <cd/editor/SelectionSet.hpp>
#include <numbers>
#include <optional>
#include <cd/debug_line/DebugLine.hpp>
#include <cd/material/Material.hpp>
#include <cd/math/Matrix.hpp>
#include <cd/math/Quaternion.hpp>
#include <cd/math/Transform.hpp>
#include <cd/math/Vector.hpp>
#include <cd/platform/Window.hpp>
#include <cd/render/Renderer.hpp>
#include <cd/rhi/ICommandBuffer.hpp>
#include <cd/rhi/IDevice.hpp>
#include <cd/rhi/vulkan/VulkanDevice.hpp>
#include <cd/scene/Scene.hpp>
#include <cd/light/Light.hpp>
#include <cd/world_container/LayerMember.hpp>
#include <cd/world_container/ProjectIo.hpp>
#include <cd/scene/Serializer.hpp>
#include <cd/shader/Compiler.hpp>
#include <imgui.h>
#include <imgui_internal.h>  // DockBuilder API

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <numbers>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace
{

// ---------------------------------------------------------------------------
// Minimal CPU-side mesh geometry (used for synthetic fallback primitives)
// ---------------------------------------------------------------------------

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

struct MeshCpu
{
    std::vector<CubeVertex>    verts;
    std::vector<std::uint16_t> idx;
};

[[nodiscard]] inline MeshCpu make_sphere(int stacks = 18, int slices = 24)
{
    MeshCpu m;
    m.verts.reserve(static_cast<std::size_t>((stacks + 1) * (slices + 1)));
    for (int i = 0; i <= stacks; ++i)
    {
        const float phi   = static_cast<float>(i) / static_cast<float>(stacks) * std::numbers::pi_v<float>;
        const float sin_p = std::sin(phi);
        const float cos_p = std::cos(phi);
        for (int j = 0; j <= slices; ++j)
        {
            const float theta = static_cast<float>(j) / static_cast<float>(slices) * 2.0F * std::numbers::pi_v<float>;
            CubeVertex v {};
            v.pos[0] = 0.5F * sin_p * std::cos(theta);
            v.pos[1] = 0.5F * cos_p;
            v.pos[2] = 0.5F * sin_p * std::sin(theta);
            v.color[0] = v.pos[0] + 0.5F;
            v.color[1] = v.pos[1] + 0.5F;
            v.color[2] = v.pos[2] + 0.5F;
            m.verts.push_back(v);
        }
    }
    m.idx.reserve(static_cast<std::size_t>(stacks * slices * 6));
    for (int i = 0; i < stacks; ++i)
    {
        for (int j = 0; j < slices; ++j)
        {
            const auto a = static_cast<std::uint16_t>(i * (slices + 1) + j);
            const auto b = static_cast<std::uint16_t>(a + slices + 1);
            m.idx.push_back(a);
            m.idx.push_back(b);
            m.idx.push_back(static_cast<std::uint16_t>(a + 1));
            m.idx.push_back(b);
            m.idx.push_back(static_cast<std::uint16_t>(b + 1));
            m.idx.push_back(static_cast<std::uint16_t>(a + 1));
        }
    }
    return m;
}

[[nodiscard]] inline MeshCpu make_cone(int slices = 32)
{
    MeshCpu m;
    m.verts.reserve(static_cast<std::size_t>(2 + 2 * slices));
    m.verts.push_back({ {  0.0F,  0.5F, 0.0F }, { 1.00F, 1.00F, 1.00F } });
    m.verts.push_back({ {  0.0F, -0.5F, 0.0F }, { 0.30F, 0.30F, 0.30F } });
    for (int i = 0; i < slices; ++i)
    {
        const float t = static_cast<float>(i) / static_cast<float>(slices) * 2.0F * std::numbers::pi_v<float>;
        const float x = 0.5F * std::cos(t);
        const float z = 0.5F * std::sin(t);
        CubeVertex side {};
        side.pos[0] = x; side.pos[1] = -0.5F; side.pos[2] = z;
        side.color[0] = 0.5F + 0.5F * std::cos(t);
        side.color[1] = 0.7F;
        side.color[2] = 0.5F + 0.5F * std::sin(t);
        m.verts.push_back(side);
    }
    for (int i = 0; i < slices; ++i)
    {
        const float t = static_cast<float>(i) / static_cast<float>(slices) * 2.0F * std::numbers::pi_v<float>;
        const float x = 0.5F * std::cos(t);
        const float z = 0.5F * std::sin(t);
        CubeVertex disk {};
        disk.pos[0] = x; disk.pos[1] = -0.5F; disk.pos[2] = z;
        disk.color[0] = 0.20F; disk.color[1] = 0.20F; disk.color[2] = 0.25F;
        m.verts.push_back(disk);
    }
    m.idx.reserve(static_cast<std::size_t>(slices * 6));
    for (int i = 0; i < slices; ++i)
    {
        const auto a = static_cast<std::uint16_t>(2 + i);
        const auto b = static_cast<std::uint16_t>(2 + (i + 1) % slices);
        m.idx.push_back(0);
        m.idx.push_back(a);
        m.idx.push_back(b);
    }
    for (int i = 0; i < slices; ++i)
    {
        const auto a = static_cast<std::uint16_t>(2 + slices + i);
        const auto b = static_cast<std::uint16_t>(2 + slices + (i + 1) % slices);
        m.idx.push_back(1);
        m.idx.push_back(b);
        m.idx.push_back(a);
    }
    return m;
}

// ---------------------------------------------------------------------------
// Per-entity viewport state (tint for synthetic primitives; neutral for glTF)
// ---------------------------------------------------------------------------

struct EntityMeta
{
    cd::ecs::Entity   handle {};
    std::string       display_name;
    cd::math::Vec3f   tint { 1.0F, 1.0F, 1.0F };
    // Mesh kind only used for synthetic fallback drawcalls.
    enum class Kind : std::uint8_t { kCube, kSphere, kCone, kGltf } kind { Kind::kCube };
    // phase1098: raw bridge kind string as LOADED (e.g. "Torus") — the
    // editor renders unknowns as Cube but must re-save the original
    // string so an editor round-trip never downgrades engine data.
    std::string bridge_kind {};
};

// phase1097: editor light row — same shape as hello_engine's LightRow
// so the bridge file's "lights" array round-trips field-for-field.
struct EditorLightRow
{
    std::string     name { "Light" };
    cd::light::Light light;
    bool            enabled { true };
    float           kelvin  { 6500.0F };
};

// ---------------------------------------------------------------------------
// Viewport shaders (colour + push-constant MVP/tint)
// ---------------------------------------------------------------------------

constexpr const char* kViewportVS = R"glsl(
#version 450
layout(push_constant) uniform PC {
    mat4 mvp;
    vec4 tint;
} pc;
layout(location = 0) in vec3 in_pos;
layout(location = 1) in vec3 in_color;
layout(location = 0) out vec3 v_color;
void main() {
    vec4 clip = pc.mvp * vec4(in_pos, 1.0);
    clip.y = -clip.y;
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

struct ViewportPC {
    cd::math::Mat4f mvp {};
    float tint[4] { 1.0F, 1.0F, 1.0F, 1.0F };
};

// ---------------------------------------------------------------------------
// Label helpers for entity display names
// ---------------------------------------------------------------------------

[[nodiscard]] inline std::string entity_label(cd::ecs::Entity e)
{
    return "Entity#" + std::to_string(e.id);
}

}  // namespace

// ===========================================================================
// main
// ===========================================================================
int main(int argc, char** argv)
{
    const cd::sample::Runtime runtime = cd::sample::parse_runtime(argc, argv);

    // ---- Window -----------------------------------------------------------
    cd::platform::WindowDesc wd {};
    wd.title  = "CHROMODYNAMIC — hello_editor (T3.2)";
    wd.width  = 1280;
    wd.height = 720;
    auto window_r = cd::platform::create_window(wd);
    if (!window_r.has_value()) return 1;
    auto& window = **window_r;

    // ---- RHI device -------------------------------------------------------
    cd::rhi::vulkan::VulkanCreateInfo vci {};
    auto device_r = cd::rhi::vulkan::create_vulkan_device(vci);
    if (!device_r.has_value()) return 2;
    auto& device = **device_r;

    // ---- Renderer ---------------------------------------------------------
    cd::render::RendererDesc rd {};
    rd.device                   = &device;
    rd.swapchain.window_handle  = window.native_window_handle();
    rd.swapchain.display_handle = window.native_display_handle();
    rd.swapchain.extent         = { window.width(), window.height() };
    rd.swapchain.format         = cd::rhi::Format::kBGRA8Unorm;
    rd.frames_in_flight         = 2;
    auto renderer_r = cd::render::Renderer::create(rd);
    if (!renderer_r.has_value()) return 3;
    auto& renderer = *renderer_r;

    // ---- ImGui backend ----------------------------------------------------
    cd::imgui::InitDesc id {};
    id.window           = &window;
    id.device           = &device;
    id.color_format     = cd::rhi::Format::kBGRA8Unorm;
    id.frames_in_flight = 2;
    auto ctx_r = cd::imgui::Context::create(id);
    if (!ctx_r.has_value()) return 4;
    auto& ctx = **ctx_r;

    ImGuiIO& imgui_io = ImGui::GetIO();
    imgui_io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    bool dock_initialised = false;

    // ---- GPU buffer upload helper -----------------------------------------
    auto make_upload_buf = [&](std::span<const std::byte> bytes,
                               cd::rhi::BufferUsage usage) -> cd::rhi::BufferHandle
    {
        cd::rhi::BufferDesc bd {};
        bd.size   = bytes.size();
        bd.usage  = usage;
        bd.memory = cd::rhi::MemoryUsage::kCpuToGpu;
        auto r = device.create_buffer(bd);
        if (!r.has_value()) return {};
        (void)device.upload_buffer(*r, 0, bytes);
        return *r;
    };

    // ---- Synthetic fallback geometry (cube / sphere / cone) ---------------
    const auto sphere_cpu = make_sphere(20, 28);
    const auto cone_cpu   = make_cone(40);

    auto cube_vb = make_upload_buf(
        { reinterpret_cast<const std::byte*>(kCubeVerts.data()),
          kCubeVerts.size() * sizeof(CubeVertex) },
        cd::rhi::BufferUsage::kVertex);
    auto cube_ib = make_upload_buf(
        { reinterpret_cast<const std::byte*>(kCubeIndices.data()),
          kCubeIndices.size() * sizeof(std::uint16_t) },
        cd::rhi::BufferUsage::kIndex);
    auto sphere_vb = make_upload_buf(
        { reinterpret_cast<const std::byte*>(sphere_cpu.verts.data()),
          sphere_cpu.verts.size() * sizeof(CubeVertex) },
        cd::rhi::BufferUsage::kVertex);
    auto sphere_ib = make_upload_buf(
        { reinterpret_cast<const std::byte*>(sphere_cpu.idx.data()),
          sphere_cpu.idx.size() * sizeof(std::uint16_t) },
        cd::rhi::BufferUsage::kIndex);
    auto cone_vb = make_upload_buf(
        { reinterpret_cast<const std::byte*>(cone_cpu.verts.data()),
          cone_cpu.verts.size() * sizeof(CubeVertex) },
        cd::rhi::BufferUsage::kVertex);
    auto cone_ib = make_upload_buf(
        { reinterpret_cast<const std::byte*>(cone_cpu.idx.data()),
          cone_cpu.idx.size() * sizeof(std::uint16_t) },
        cd::rhi::BufferUsage::kIndex);
    const auto sphere_idx_count = static_cast<std::uint32_t>(sphere_cpu.idx.size());
    const auto cone_idx_count   = static_cast<std::uint32_t>(cone_cpu.idx.size());

    // ---- Depth target (rebuilt on resize) ---------------------------------
    constexpr auto kDepthFormat = cd::rhi::Format::kD32Float;
    cd::rhi::TextureHandle     depth_image {};
    cd::rhi::TextureViewHandle depth_view {};
    auto make_depth = [&](cd::rhi::Extent2D size) {
        if (depth_view.is_valid())  device.destroy_texture_view(depth_view);
        if (depth_image.is_valid()) device.destroy_texture(depth_image);
        cd::rhi::TextureDesc td {};
        td.type   = cd::rhi::TextureType::k2D;
        td.format = kDepthFormat;
        td.extent = { size.width, size.height, 1 };
        td.usage  = cd::rhi::TextureUsage::kDepthStencilAttachment;
        td.memory = cd::rhi::MemoryUsage::kGpuOnly;
        auto t = device.create_texture(td);
        if (!t.has_value()) return false;
        cd::rhi::TextureViewDesc vd {};
        vd.texture     = *t;
        vd.format      = kDepthFormat;
        vd.mip_count   = 1;
        vd.layer_count = 1;
        auto v = device.create_texture_view(vd);
        if (!v.has_value()) { device.destroy_texture(*t); return false; }
        depth_image = *t;
        depth_view  = *v;
        return true;
    };
    make_depth({ window.width(), window.height() });

    // ---- Viewport material (push-constant colour pass) --------------------
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
            .size   = static_cast<std::uint32_t>(sizeof(ViewportPC)) }
    };
    cd::material::MaterialDesc mat_desc {};
    mat_desc.vertex_glsl              = kViewportVS;
    mat_desc.fragment_glsl            = kViewportFS;
    mat_desc.vertex_bindings          = kVtxBindings;
    mat_desc.vertex_attributes        = kVtxAttrs;
    mat_desc.color_attachment_formats = kColorFormats;
    mat_desc.depth_attachment_format  = kDepthFormat;
    mat_desc.push_constants           = kPush;
    mat_desc.topology                 = cd::rhi::PrimitiveTopology::kTriangleList;
    mat_desc.raster.cull              = cd::rhi::CullMode::kBack;
    mat_desc.depth_stencil.depth_test    = true;
    mat_desc.depth_stencil.depth_write   = true;
    mat_desc.depth_stencil.depth_compare = cd::rhi::CompareOp::kLess;
    mat_desc.name = "hello_editor_viewport";
    auto mat_r = cd::material::Material::create(device, compiler.get(), mat_desc);

    // phase1062: editor debug-line renderer (cd::debug_draw, 2nd
    // consumer). Single BGRA8 attachment -> the library's embedded
    // default shaders fit as-is. Failure degrades gracefully — the
    // editor runs without the grid/selection overlays.
    cd::debug_draw::Renderer dbg_renderer {};
    {
        cd::debug_draw::RendererDesc dd {};
        dd.color_attachment_formats = kColorFormats;
        dd.depth_attachment_format  = kDepthFormat;
        dd.name = "hello_editor/debug_line";
        if (auto dd_r = cd::debug_draw::Renderer::create(
                device, compiler.get(), dd); dd_r.has_value())
        {
            dbg_renderer = std::move(*dd_r);
        }
        else
        {
            std::fprintf(stderr,
                "hello_editor: debug_draw create failed: %.*s "
                "(grid/selection overlays disabled)\n",
                static_cast<int>(dd_r.error().message.size()),
                dd_r.error().message.data());
        }
    }
    cd::debug_line::LineBatch dbg_batch;

    // phase1063/1073: draggable translate gizmo for the selected
    // entity. cd::editor::AxisGizmo owns the hover/drag state machine;
    // v2 (phase1073) replaced the v1 screen-delta metric with the
    // library's ray-plane kit: each frame the mouse ray
    // (pick_ray_from_ndc) is intersected with a drag plane chosen at
    // begin_drag — axis drags use the plane CONTAINING the axis that
    // max-faces the camera (axis_drag_plane_normal), XY/XZ/YZ pad
    // drags use the pad plane itself. The world hit feeds
    // AxisGizmo::update_drag, which filters components per axis/pad.
    // Arrows + pad squares render through cd::debug_draw.
    cd::editor::AxisGizmo viewport_gizmo;
    struct GizmoDragState
    {
        cd::math::Vec3f plane_point {};    // drag-plane anchor (target at grab)
        cd::math::Vec3f plane_normal {};   // chosen at begin_drag
        // phase1064: position snapshot at drag start so drag-end can
        // rewind + push a TranslateCommand (EditHistory::push applies
        // immediately — rewinding first makes apply land exactly on
        // the live final position and undo on the exact start).
        cd::math::Vec3f drag_start_pos {};
        // phase1092: group-translate session — per-entity start
        // positions captured at grab (entity, position) so the live
        // drag and the undo command apply the SAME centroid delta to
        // every member.
        std::vector<std::pair<cd::ecs::Entity, cd::math::Vec3f>> drag_start_group {};
        // phase1074: rotate/scale sessions.
        cd::math::Quatf drag_start_rot {};
        cd::math::Vec3f drag_start_scale { 1.0F, 1.0F, 1.0F };
        cd::math::Vec3f ring_u {};        // rotate: in-plane basis
        cd::math::Vec3f ring_v {};
        float           angle_prev { 0.0F };  // rotate: unwrap accumulator
        cd::math::Vec3f grab_hit {};      // scale: initial plane hit
    };
    GizmoDragState gizmo_drag {};

    // phase1093: freeze-frustum debug staple — checking the box snapshots
    // inverse(view_proj) of THAT frame; the frozen wireframe frustum then
    // stays in the world so the user can fly outside it and inspect what
    // the camera saw (the classic culling-debug workflow).
    bool            freeze_frustum { false };
    bool            frozen_frustum_valid { false };
    cd::math::Mat4f frozen_inv_vp {};

    // ---- ECS / scene / editor primitives ----------------------------------
    cd::ecs::World        world;
    cd::scene::Scene      scene { world };
    cd::editor::EditHistory   history;
    cd::editor::HierarchyView hierarchy;   // T3.2: drive the scene-tree panel

    cd::editor::panel::console::Console console_panel;
    auto log_push = [&console_panel](std::string_view s) {
        console_panel.push_log(s);
    };

    // ---- Command palette --------------------------------------------------
    cd::editor::CommandPalette palette;
    bool        palette_visible = false;
    std::string palette_query;
    palette_query.reserve(64);

    // ---- Entity metadata table (display names + tints) --------------------
    // Maps ECS entity IDs to their visual metadata. Updated on glTF load
    // and on synthetic fallback spawn.
    std::vector<EntityMeta> entity_metas;

    // phase1111: bridge file path + project boot moved ABOVE the spawn
    // section so freshly spawned entities can be assigned to the ACTIVE
    // level's active layer from minute one.
    constexpr const char* kBridgePath = "hello_engine.cdscene.json";
    // phase1109: project container — the editor now opens/saves a
    // .cdproject (cd::world_container::ProjectIo) and drives scene
    // loading through the ACTIVE level's scene_path. First boot (no
    // file) creates a default project whose single level points at
    // the hello_engine bridge scene, tying the two systems together.
    constexpr const char* kProjectPath = "hello_editor.cdproject";
    std::unique_ptr<cd::world_container::Project> project;
    std::size_t active_level_idx = 0;
    if (auto loaded_proj = cd::world_container::load_project_file(kProjectPath);
        loaded_proj.has_value())
    {
        project = std::move(*loaded_proj);
        log_push("[project] loaded " + std::string { kProjectPath } + " (" +
                 std::to_string(project->level_count()) + " levels)");
    }
    else
    {
        project = std::make_unique<cd::world_container::Project>("Hello Project");
        auto* lvl = project->add_level("Main");
        lvl->set_scene_path(kBridgePath);
        log_push("[project] new default project (Main -> bridge scene)");
    }

    // Name of the active level's active (spawn-destination) layer.
    auto active_layer_name = [&]() -> std::string
    {
        if (const auto* lvl = project->level(active_level_idx); lvl != nullptr)
            if (const auto* lay = lvl->layer(lvl->active_layer()); lay != nullptr)
                return std::string { lay->name() };
        return std::string { cd::world_container::kDefaultLayerName };
    };


    // ---- Selection (phase1092: multi-select) ------------------------------
    // `selection` is the source of truth (cd::editor::SelectionSet);
    // `selected` mirrors selection.primary() so the inspector, rotate/
    // scale gizmo modes and the existing single-target sites keep their
    // shape. Ctrl+click in the hierarchy toggles membership.
    cd::editor::SelectionSet selection;
    cd::ecs::Entity selected {};

    // ---- Inspector panel (cd::editor_panel_inspector) --------------------
    cd::editor::panel::inspector::Inspector inspector_panel;
    inspector_panel.set_world_ptr(&world);

    auto find_meta = [&](cd::ecs::Entity e) -> EntityMeta* {
        for (auto& m : entity_metas)
            if (m.handle.id == e.id) return &m;
        return nullptr;
    };

    // ---- Phase 508 generic glTF load -------------------------------------
    // Use HelloGenericGltf's directory scanner. On success every glTF node
    // becomes an ECS entity with a LocalTransform; the hierarchy is wired
    // through Scene::attach(). We keep IngestResult alive for shutdown.
    std::vector<cd_sample::GenericIngest> gltf_ingests =
        cd_sample::load_generic_gltf_scenes(device, world, scene, log_push);

    const bool loaded_gltf = !gltf_ingests.empty();

    if (loaded_gltf)
    {
        // Walk every ingested node and register it in the metadata table.
        // glTF entities get a neutral white tint; kind = kGltf marks them
        // as "no synthetic draw" — the T3.2 viewport only renders the
        // synthetic-fallback entities.  The scene tree + inspector work
        // for ALL entities regardless.
        for (const auto& gi : gltf_ingests)
        {
            // Root entity
            {
                EntityMeta m;
                m.handle       = gi.result.root_entity;
                m.display_name = "[glTF root] " + std::filesystem::path(gi.source_path).filename().string();
                m.tint         = { 1.0F, 1.0F, 1.0F };
                m.kind         = EntityMeta::Kind::kGltf;
                cd::world_container::assign_layer(world, m.handle,
                                                  active_layer_name());
                entity_metas.push_back(m);
            }
            // Per-node entities
            for (std::size_t ni = 0; ni < gi.result.node_entities.size(); ++ni)
            {
                EntityMeta m;
                m.handle       = gi.result.node_entities[ni];
                m.display_name = "node_" + std::to_string(ni);
                m.tint         = { 0.8F, 0.85F, 1.0F };
                m.kind         = EntityMeta::Kind::kGltf;
                cd::world_container::assign_layer(world, m.handle,
                                                  active_layer_name());
                entity_metas.push_back(m);
            }
        }
        // Auto-select the first root so the inspector is non-empty on boot.
        if (!gltf_ingests.empty() && gltf_ingests.front().result.root_entity.id != 0)
        {
            selected = gltf_ingests.front().result.root_entity;
            selection.clear();
            selection.add(selected);
            inspector_panel.set_target(selected);
        }
    }
    else
    {
        // ---- Synthetic fallback: three named primitives -------------------
        log_push("[gltf] no Generic/ assets found — spawning Cube / Sphere / Cone");
        struct Seed { const char* name; cd::math::Vec3f pos; cd::math::Vec3f tint; EntityMeta::Kind kind; };
        const std::array<Seed, 3> seeds {{
            { "Cube",   { -1.6F, 0.0F, 0.0F }, { 1.0F, 0.4F, 0.4F }, EntityMeta::Kind::kCube   },
            { "Sphere", {  0.0F, 0.0F, 0.0F }, { 0.4F, 1.0F, 0.4F }, EntityMeta::Kind::kSphere },
            { "Cone",   {  1.6F, 0.0F, 0.0F }, { 0.4F, 0.4F, 1.0F }, EntityMeta::Kind::kCone   },
        }};
        for (const auto& s : seeds)
        {
            EntityMeta m;
            m.handle = scene.create_node();
            m.display_name = s.name;
            m.tint = s.tint;
            m.kind = s.kind;
            scene.local(m.handle)->value.position = s.pos;
            cd::world_container::assign_layer(world, m.handle,
                                              active_layer_name());
            entity_metas.push_back(m);
        }
        if (!entity_metas.empty())
        {
            selected = entity_metas.front().handle;
            selection.clear();
            selection.add(selected);
            inspector_panel.set_target(selected);
        }
        log_push("Spawned Cube, Sphere, Cone");
    }

    // Expand root entities in the hierarchy view so the tree opens fully
    // by default.
    scene.for_each_root([&](cd::ecs::Entity e, cd::scene::LocalTransform&) {
        hierarchy.expand(e);
    });

    // ---- Command palette registrations ------------------------------------
    palette.register_command(1, "Edit: Undo",
        [&]() { if (history.undo()) log_push("palette: Undo"); });
    palette.register_command(2, "Edit: Redo",
        [&]() { if (history.redo()) log_push("palette: Redo"); });
    palette.register_command(3, "Edit: Clear History",
        [&]() { history.clear(); log_push("palette: history cleared"); });
    // phase1095: hello_engine scene BRIDGE. Same file path + per-entity
    // field contract ("name"/"kind"/"tint", kind strings Cube/Sphere/
    // Cone/Gltf) and the same palette ids (80/81) as hello_engine's
    // Scene: Save / Scene: Load pair — arrange a scene here, save, then
    // load it in hello_engine to see it in the full PBR pipeline (and
    // vice versa). Kinds the editor has no mesh for (Torus/Cylinder/
    // Sponza from an engine-side save) fall back to Cube so they stay
    // visible and selectable.
    // phase1097: real light rows replace the phase1096 verbatim
    // pass-through — the bridge "lights" array now parses into
    // editable rows and serialises back field-for-field.
    std::vector<EditorLightRow> light_rows;
    auto bridge_kind_name = [](EntityMeta::Kind k) -> const char*
    {
        switch (k)
        {
            case EntityMeta::Kind::kSphere: return "Sphere";
            case EntityMeta::Kind::kCone:   return "Cone";
            case EntityMeta::Kind::kGltf:   return "Gltf";
            default:                        return "Cube";
        }
    };
    auto bridge_kind_from = [](std::string_view n) -> EntityMeta::Kind
    {
        if (n == "Sphere") return EntityMeta::Kind::kSphere;
        if (n == "Cone")   return EntityMeta::Kind::kCone;
        if (n == "Gltf")   return EntityMeta::Kind::kGltf;
        return EntityMeta::Kind::kCube;
    };
    auto save_scene_to = [&](const std::string& bridge_path)
    {
            auto find_meta = [&](cd::ecs::Entity e) -> const EntityMeta*
            {
                for (const auto& m : entity_metas)
                    if (m.handle.id == e.id) return &m;
                return nullptr;
            };
            auto root = cd::scene::serialize_scene_with(scene,
                [&](cd::ecs::Entity e, cd::asset::json::Object& obj)
                {
                    const auto* m = find_meta(e);
                    if (m == nullptr) return;
                    obj["name"] = cd::asset::json::Value { m->display_name };
                    obj["kind"] = cd::asset::json::Value {
                        m->bridge_kind.empty()
                            ? std::string { bridge_kind_name(m->kind) }
                            : m->bridge_kind };
                    obj["tint"] = cd::scene::vec3_to_json(m->tint);
                    obj["layer"] = cd::asset::json::Value { std::string {
                        cd::world_container::layer_of(world, e) } };
                });
            if (!light_rows.empty())
            {
                cd::asset::json::Array la;
                for (const auto& row : light_rows)
                {
                    cd::asset::json::Object lo;
                    lo["name"]      = cd::asset::json::Value { row.name };
                    lo["enabled"]   = cd::asset::json::Value { row.enabled };
                    lo["type"]      = cd::asset::json::Value {
                        static_cast<int>(row.light.type) };
                    lo["kelvin"]    = cd::asset::json::Value {
                        static_cast<double>(row.kelvin) };
                    lo["intensity"] = cd::asset::json::Value {
                        static_cast<double>(row.light.intensity) };
                    lo["range"]     = cd::asset::json::Value {
                        static_cast<double>(row.light.range) };
                    lo["position"]  = cd::scene::vec3_to_json(row.light.position);
                    lo["color"]     = cd::scene::vec3_to_json(row.light.color);
                    lo["direction"] = cd::scene::vec3_to_json(row.light.direction);
                    la.emplace_back(std::move(lo));
                }
                root.as_object_mut()["lights"] =
                    cd::asset::json::Value { std::move(la) };
            }
            const auto txt = cd::asset::json::serialize(root, true);
            std::ofstream f { bridge_path, std::ios::binary | std::ios::trunc };
            if (f)
            {
                f.write(txt.data(), static_cast<std::streamsize>(txt.size()));
                log_push("[bridge] saved " +
                         std::to_string(entity_metas.size()) + " entities -> " +
                         bridge_path);
            }
            else
            {
                log_push("[bridge] save FAILED (ofstream)");
            }
    };
    auto load_scene_from = [&](const std::string& bridge_path)
    {
            auto r = cd::asset::json::load(bridge_path);
            if (!r.has_value())
            {
                log_push("[bridge] load failed: " +
                         std::string { r.error().message });
                return;
            }
            if (r->is_object())
            {
                const auto& ro = r->as_object();
                if (auto it = ro.find("lights");
                    it != ro.end() && it->second.is_array())
                {
                    light_rows.clear();
                    auto read3 = [](const cd::asset::json::Object& o,
                                    const char* key, cd::math::Vec3f& out)
                    {
                        auto f = o.find(key);
                        if (f == o.end() || !f->second.is_array() ||
                            f->second.as_array().size() != 3)
                            return;
                        const auto& a = f->second.as_array();
                        if (a[0].is_number() && a[1].is_number() &&
                            a[2].is_number())
                            out = { static_cast<float>(a[0].as_number()),
                                    static_cast<float>(a[1].as_number()),
                                    static_cast<float>(a[2].as_number()) };
                    };
                    for (const auto& lv : it->second.as_array())
                    {
                        if (!lv.is_object()) continue;
                        const auto& lo = lv.as_object();
                        EditorLightRow row {};
                        if (auto n = lo.find("name");
                            n != lo.end() && n->second.is_string())
                            row.name = n->second.as_string();
                        if (auto e2 = lo.find("enabled");
                            e2 != lo.end() && e2->second.is_bool())
                            row.enabled = e2->second.as_bool();
                        if (auto t = lo.find("type");
                            t != lo.end() && t->second.is_number())
                            row.light.type = static_cast<cd::light::LightType>(
                                static_cast<int>(t->second.as_number()));
                        if (auto k = lo.find("kelvin");
                            k != lo.end() && k->second.is_number())
                            row.kelvin = static_cast<float>(k->second.as_number());
                        if (auto in = lo.find("intensity");
                            in != lo.end() && in->second.is_number())
                            row.light.intensity =
                                static_cast<float>(in->second.as_number());
                        if (auto rg = lo.find("range");
                            rg != lo.end() && rg->second.is_number())
                            row.light.range =
                                static_cast<float>(rg->second.as_number());
                        read3(lo, "position",  row.light.position);
                        read3(lo, "color",     row.light.color);
                        read3(lo, "direction", row.light.direction);
                        light_rows.push_back(std::move(row));
                    }
                    log_push("[bridge] loaded " +
                             std::to_string(light_rows.size()) + " lights");
                }
            }
            // Replace semantics: drop every current root (children
            // cascade), then rebuild metas from the file's extras.
            std::vector<cd::ecs::Entity> roots;
            scene.for_each_root(
                [&](cd::ecs::Entity e, cd::scene::LocalTransform&)
                {
                    roots.push_back(e);
                });
            for (const auto e : roots)
                scene.destroy_node(e);
            entity_metas.clear();
            selection.clear();
            selected = {};
            inspector_panel.set_target(selected);
            history.clear();
            const auto loaded = cd::scene::deserialize_scene_with(
                scene, *r,
                [&](cd::ecs::Entity e, const cd::asset::json::Object& obj)
                {
                    EntityMeta m;
                    m.handle = e;
                    if (auto it = obj.find("name");
                        it != obj.end() && it->second.is_string())
                        m.display_name = it->second.as_string();
                    if (auto it = obj.find("kind");
                        it != obj.end() && it->second.is_string())
                    {
                        m.bridge_kind = it->second.as_string();
                        m.kind = bridge_kind_from(m.bridge_kind);
                    }
                    if (auto it = obj.find("tint");
                        it != obj.end() && it->second.is_array() &&
                        it->second.as_array().size() == 3)
                    {
                        const auto& a = it->second.as_array();
                        if (a[0].is_number() && a[1].is_number() &&
                            a[2].is_number())
                            m.tint = { static_cast<float>(a[0].as_number()),
                                       static_cast<float>(a[1].as_number()),
                                       static_cast<float>(a[2].as_number()) };
                    }
                    if (auto it = obj.find("layer");
                        it != obj.end() && it->second.is_string() &&
                        it->second.as_string() !=
                            cd::world_container::kDefaultLayerName)
                    {
                        cd::world_container::assign_layer(
                            world, e, it->second.as_string());
                    }
                    entity_metas.push_back(std::move(m));
                });
            if (!loaded.has_value())
            {
                log_push("[bridge] deserialize failed: " +
                         std::string { loaded.error().message });
                return;
            }
            scene.for_each_root(
                [&](cd::ecs::Entity e, cd::scene::LocalTransform&)
                {
                    hierarchy.expand(e);
                });
            if (!entity_metas.empty())
            {
                selected = entity_metas.front().handle;
                selection.add(selected);
                inspector_panel.set_target(selected);
            }
            log_push("[bridge] loaded " +
                     std::to_string(entity_metas.size()) + " entities <- " +
                     bridge_path);
    };
    palette.register_command(80, "Scene: Save (hello_engine bridge)",
        [&]() { save_scene_to(kBridgePath); });
    palette.register_command(81, "Scene: Load (hello_engine bridge)",
        [&]() { load_scene_from(kBridgePath); });

    palette.register_command(10, "Select: First Entity",
        [&]() {
            if (!entity_metas.empty()) {
                selected = entity_metas.front().handle;
                selection.clear();
                selection.add(selected);
                inspector_panel.set_target(selected);
                log_push("palette: select first entity");
            }
        });
    palette.register_command(20, "Transform: Reset Selected",
        [&]() {
            if (selected.id != 0)
            {
                if (auto* lt = scene.local(selected); lt != nullptr)
                {
                    lt->value.position = {};
                    lt->value.scale    = { 1.0F, 1.0F, 1.0F };
                    lt->value.rotation = { 0.0F, 0.0F, 0.0F, 1.0F };
                    log_push("palette: reset transform");
                }
            }
        });
    palette.register_command(40, "Help: Print Shortcuts",
        [&]() {
            log_push("Ctrl+Shift+P : command palette");
            log_push("Ctrl+Z / Ctrl+Y : undo / redo");
            log_push("Esc : close palette / exit");
        });

    // ---- Free-fly camera --------------------------------------------------
    struct FlyCamera {
        cd::math::Vec3f position { 0.0F, 2.0F, 6.0F };
        float yaw   { -1.5707963F };   // looking toward -Z
        float pitch { -0.25F };
        float speed { 3.0F };
        float mouse_sensitivity { 0.005F };
    } camera;
    auto last_time = std::chrono::steady_clock::now();

    // ---- Rotation slider state (inspector bottom row) --------------------
    // `rot_slider_deg` is a persistent Y-rotation accumulator that the
    // inspector's "Y rotate" slider controls. It resets when selection
    // changes. When the slider value changes, we recompute the entity's
    // quaternion and update the viewport on the same frame.
    float rot_slider_deg = 0.0F;
    cd::ecs::Entity rot_slider_entity {};  // which entity the slider is tracking

    std::printf("hello_editor T3.2: ready. ESC to exit.\n");
    std::fflush(stdout);

    std::vector<cd::platform::OSEvent> events;
    events.reserve(64);
    bool          needs_rebuild = false;
    std::uint32_t frame_idx    = 0;

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
            if (e.kind == cd::platform::OSEventKind::kKeyDown &&
                e.key  == cd::platform::KeyCode::kEscape)
            {
                if (palette_visible) { palette_visible = false; palette_query.clear(); }
                else                  window.request_close();
            }
            else if (e.kind == cd::platform::OSEventKind::kResize)
                needs_rebuild = true;
            else if (e.kind == cd::platform::OSEventKind::kKeyDown &&
                     e.key  == cd::platform::KeyCode::kP)
            {
                const ImGuiIO& io = ImGui::GetIO();
                if (io.KeyCtrl && io.KeyShift)
                {
                    palette_visible = !palette_visible;
                    if (palette_visible) palette_query.clear();
                }
            }
        }

        if (needs_rebuild)
        {
            if (window.width() == 0 || window.height() == 0) continue;
            if (!renderer.recreate_swapchain({ window.width(), window.height() }).has_value()) continue;
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
        auto& cmd   = *frame.command_buffer;

        // ---- Begin render pass -------------------------------------------
        std::array<cd::rhi::ColorAttachmentInfo, 1> color_attach {
            cd::rhi::ColorAttachmentInfo {
                .view       = frame.swapchain_image_view,
                .load_op    = cd::rhi::LoadOp::kClear,
                .store_op   = cd::rhi::StoreOp::kStore,
                .clear_color = { .f32 = { 0.12F, 0.13F, 0.16F, 1.0F } }
            }
        };
        cd::rhi::DepthStencilAttachmentInfo depth_attach {};
        depth_attach.view        = depth_view;
        depth_attach.depth_load  = cd::rhi::LoadOp::kClear;
        depth_attach.depth_store = cd::rhi::StoreOp::kStore;
        depth_attach.clear.depth = 1.0F;
        cd::rhi::RenderPassBeginInfo rp {};
        rp.render_area      = cd::rhi::Rect2D { { 0, 0 }, frame.extent };
        rp.color_attachments = color_attach;
        if (mat_r.has_value()) rp.depth_stencil = &depth_attach;
        cmd.begin_render_pass(rp);
        cmd.set_viewport(cd::rhi::Viewport {
            0.0F, 0.0F,
            static_cast<float>(frame.extent.width),
            static_cast<float>(frame.extent.height),
            0.0F, 1.0F });
        cmd.set_scissor(cd::rhi::Rect2D { { 0, 0 }, frame.extent });

        // ---- 3D viewport: synthetic fallback primitives ------------------
        if (mat_r.has_value() && cube_vb.is_valid() && cube_ib.is_valid())
        {
            auto& mat = *mat_r;

            // Camera update.
            const auto t_now = std::chrono::steady_clock::now();
            const float dt = std::chrono::duration<float>(t_now - last_time).count();
            last_time = t_now;

            const bool ui_mouse = imgui_io.WantCaptureMouse;
            const bool ui_kbd   = imgui_io.WantTextInput;

            if (!ui_mouse && ImGui::IsMouseDown(ImGuiMouseButton_Right))
            {
                const ImVec2 drag = ImGui::GetMouseDragDelta(ImGuiMouseButton_Right);
                camera.yaw   += drag.x * camera.mouse_sensitivity;
                camera.pitch -= drag.y * camera.mouse_sensitivity;
                camera.pitch  = std::clamp(camera.pitch, -1.55F, 1.55F);
                ImGui::ResetMouseDragDelta(ImGuiMouseButton_Right);
            }
            const float cp = std::cos(camera.pitch);
            const cd::math::Vec3f forward {
                std::cos(camera.yaw) * cp,
                std::sin(camera.pitch),
                std::sin(camera.yaw) * cp
            };
            const cd::math::Vec3f right { -forward.z, 0.0F, forward.x };
            if (!ui_kbd)
            {
                const float step = camera.speed * dt;
                if (ImGui::IsKeyDown(ImGuiKey_W)) {
                    camera.position.x += forward.x * step;
                    camera.position.y += forward.y * step;
                    camera.position.z += forward.z * step;
                }
                if (ImGui::IsKeyDown(ImGuiKey_S)) {
                    camera.position.x -= forward.x * step;
                    camera.position.y -= forward.y * step;
                    camera.position.z -= forward.z * step;
                }
                if (ImGui::IsKeyDown(ImGuiKey_D)) {
                    camera.position.x += right.x * step;
                    camera.position.z += right.z * step;
                }
                if (ImGui::IsKeyDown(ImGuiKey_A)) {
                    camera.position.x -= right.x * step;
                    camera.position.z -= right.z * step;
                }
                if (ImGui::IsKeyDown(ImGuiKey_E)) camera.position.y += step;
                if (ImGui::IsKeyDown(ImGuiKey_Q)) camera.position.y -= step;
            }

            const cd::math::Vec3f eye    = camera.position;
            const cd::math::Vec3f target = {
                eye.x + forward.x, eye.y + forward.y, eye.z + forward.z };
            const auto view   = cd::math::look_at(eye, target, { 0.0F, 1.0F, 0.0F });
            const float aspect = static_cast<float>(frame.extent.width) /
                                 static_cast<float>(std::max(1u, frame.extent.height));
            const auto proj = cd::math::perspective<float>(0.9F, aspect, 0.1F, 100.0F);
            const auto vp   = proj * view;

            cmd.bind_graphics_pipeline(mat.pipeline());

            // Draw each synthetic fallback entity.
            auto draw_kind = [&](EntityMeta::Kind kind,
                                 cd::rhi::BufferHandle vb,
                                 cd::rhi::BufferHandle ib,
                                 std::uint32_t idx_count)
            {
                bool bound = false;
                for (const auto& meta : entity_metas)
                {
                    if (meta.kind != kind) continue;
                    auto* lt = scene.local(meta.handle);
                    if (lt == nullptr) continue;
                    if (!bound)
                    {
                        cmd.bind_vertex_buffer(0, vb, 0);
                        cmd.bind_index_buffer(ib, 0, cd::rhi::IndexType::kUInt16);
                        bound = true;
                    }
                    ViewportPC pc {};
                    pc.mvp    = vp * cd::math::to_mat4(lt->value);
                    pc.tint[0] = meta.tint.x;
                    pc.tint[1] = meta.tint.y;
                    pc.tint[2] = meta.tint.z;
                    pc.tint[3] = 1.0F;
                    cmd.push_constants(mat.pipeline_layout(),
                                       cd::rhi::ShaderStage::kVertex,
                                       0, sizeof(pc), &pc);
                    cmd.draw_indexed(idx_count, 1, 0, 0, 0);
                }
            };
            draw_kind(EntityMeta::Kind::kCube,   cube_vb,   cube_ib,   static_cast<std::uint32_t>(kCubeIndices.size()));
            draw_kind(EntityMeta::Kind::kSphere, sphere_vb, sphere_ib, sphere_idx_count);
            draw_kind(EntityMeta::Kind::kCone,   cone_vb,   cone_ib,   cone_idx_count);

            // phase1062: editor staples via cd::debug_draw — the
            // classic floor grid (10 m, 1 m cells, XZ plane) and an
            // orange selection box around the selected entity's
            // transform. Flushed inside this pass; depth test keeps
            // the grid behind geometry, depth-write-off keeps it out
            // of later passes.
            // phase1063: gizmo pick + drag (before the overlay batch
            // so this frame's arrows reflect this frame's state).
            if (selected.id != 0)
            {
                if (auto* gz_lt = scene.local(selected); gz_lt != nullptr)
                {
                    constexpr float kArmLen = 1.2F;
                    constexpr float kPadMin = 0.30F;  // pad inner edge (× arm)
                    constexpr float kPadMax = 0.62F;  // pad outer edge (× arm)
                    constexpr float kRingRad = 1.05F; // rotate ring radius
                    constexpr float kRingTol = 0.14F; // ring pick band (world)
                    // phase1092: gizmo anchors at the selection
                    // CENTROID (single selection: the entity itself).
                    cd::math::Vec3f sel_centroid = gz_lt->value.position;
                    if (selection.size() > 1)
                    {
                        cd::math::Vec3f acc {};
                        float n_sel = 0.0F;
                        for (const auto sel_e : selection.entries())
                        {
                            if (auto* lt = scene.local(sel_e); lt != nullptr)
                            {
                                acc.x += lt->value.position.x;
                                acc.y += lt->value.position.y;
                                acc.z += lt->value.position.z;
                                n_sel += 1.0F;
                            }
                        }
                        if (n_sel > 0.0F)
                            sel_centroid = { acc.x / n_sel,
                                             acc.y / n_sel,
                                             acc.z / n_sel };
                    }
                    viewport_gizmo.set_target(sel_centroid);
                    // phase1074: mode hotkeys — 1/2/3 (W/E/R belongs to
                    // the WASD camera). Ignored while typing in UI and
                    // mid-drag (set_mode is drag-gated in the library).
                    if (!ui_kbd)
                    {
                        if (ImGui::IsKeyPressed(ImGuiKey_1))
                            viewport_gizmo.set_mode(cd::editor::GizmoMode::kTranslate);
                        if (ImGui::IsKeyPressed(ImGuiKey_2))
                            viewport_gizmo.set_mode(cd::editor::GizmoMode::kRotate);
                        if (ImGui::IsKeyPressed(ImGuiKey_3))
                            viewport_gizmo.set_mode(cd::editor::GizmoMode::kScale);
                    }
                    const auto to_screen =
                        [&](const cd::math::Vec3f& w) -> std::optional<ImVec2>
                    {
                        const cd::math::Vec4f clip =
                            vp * cd::math::Vec4f { w.x, w.y, w.z, 1.0F };
                        if (clip.w <= 1e-5F) return std::nullopt;
                        const float sx = (clip.x / clip.w * 0.5F + 0.5F) *
                            static_cast<float>(frame.extent.width);
                        const float sy = (1.0F - (clip.y / clip.w * 0.5F + 0.5F)) *
                            static_cast<float>(frame.extent.height);
                        return ImVec2 { sx, sy };
                    };
                    const auto dist_to_seg = [](ImVec2 a, ImVec2 b, ImVec2 q)
                    {
                        const float abx = b.x - a.x;
                        const float aby = b.y - a.y;
                        const float len2 = abx * abx + aby * aby;
                        float t = 0.0F;
                        if (len2 > 1e-6F)
                            t = std::clamp(((q.x - a.x) * abx + (q.y - a.y) * aby) / len2,
                                           0.0F, 1.0F);
                        const float px = a.x + abx * t - q.x;
                        const float py = a.y + aby * t - q.y;
                        return std::sqrt(px * px + py * py);
                    };
                    const ImVec2 mouse = ImGui::GetMousePos();
                    // phase1073: world-space mouse ray via the library
                    // pick kit (NDC y is the flip of to_screen's y).
                    const auto inv_vp = cd::math::inverse(vp);
                    const float ndc_mx =
                        mouse.x / static_cast<float>(frame.extent.width) * 2.0F - 1.0F;
                    const float ndc_my =
                        1.0F - 2.0F * mouse.y / static_cast<float>(frame.extent.height);
                    const auto mouse_ray =
                        cd::editor::pick_ray_from_ndc(inv_vp, ndc_mx, ndc_my);
                    const auto org_px = to_screen(viewport_gizmo.target());
                    if (org_px.has_value() && !ui_mouse && mouse_ray.has_value())
                    {
                        const auto gmode = viewport_gizmo.mode();
                        if (!viewport_gizmo.is_dragging())
                        {
                            cd::editor::GizmoAxis best = cd::editor::GizmoAxis::kNone;
                            float best_d = viewport_gizmo.hover_tolerance_pixels;
                            const auto tgt = viewport_gizmo.target();
                            if (gmode != cd::editor::GizmoMode::kRotate)
                            for (const auto ax : { cd::editor::GizmoAxis::kX,
                                                   cd::editor::GizmoAxis::kY,
                                                   cd::editor::GizmoAxis::kZ })
                            {
                                const auto dir = cd::editor::axis_dir(ax);
                                const auto tip_px = to_screen(
                                    { tgt.x + dir.x * kArmLen,
                                      tgt.y + dir.y * kArmLen,
                                      tgt.z + dir.z * kArmLen });
                                if (!tip_px.has_value()) continue;
                                const float ax_px_x = tip_px->x - org_px->x;
                                const float ax_px_y = tip_px->y - org_px->y;
                                const float ax_px_len = std::sqrt(
                                    ax_px_x * ax_px_x + ax_px_y * ax_px_y);
                                if (ax_px_len < 4.0F) continue;  // axis toward camera
                                const float d = dist_to_seg(*org_px, *tip_px, mouse);
                                if (d < best_d)
                                {
                                    best_d = d;
                                    best = ax;
                                }
                            }
                            // phase1073: XY/XZ/YZ pad pick — world-space
                            // ray-vs-pad-square; an inside hit beats any
                            // arrow proximity (pads sit between arrows).
                            if (gmode == cd::editor::GizmoMode::kTranslate)
                            for (const auto pad : { cd::editor::GizmoAxis::kXY,
                                                    cd::editor::GizmoAxis::kXZ,
                                                    cd::editor::GizmoAxis::kYZ })
                            {
                                const auto n = cd::editor::plane_normal(pad);
                                const auto hit = cd::editor::intersect_ray_plane(
                                    *mouse_ray, tgt, n);
                                if (!hit.has_value()) continue;
                                const cd::math::Vec3f local {
                                    hit->x - tgt.x, hit->y - tgt.y, hit->z - tgt.z };
                                float u = 0.0F;
                                float v = 0.0F;
                                switch (pad)
                                {
                                    case cd::editor::GizmoAxis::kXY:
                                        u = local.x; v = local.y; break;
                                    case cd::editor::GizmoAxis::kXZ:
                                        u = local.x; v = local.z; break;
                                    default:
                                        u = local.y; v = local.z; break;
                                }
                                const float lo = kPadMin * kArmLen;
                                const float hi = kPadMax * kArmLen;
                                if (u >= lo && u <= hi && v >= lo && v <= hi)
                                {
                                    best = pad;
                                    break;
                                }
                            }
                            // phase1074: rotate-ring hover — ray vs ring
                            // plane, then radial band test around kRingRad.
                            if (gmode == cd::editor::GizmoMode::kRotate)
                            {
                                float best_band = kRingTol;
                                for (const auto ax : { cd::editor::GizmoAxis::kX,
                                                       cd::editor::GizmoAxis::kY,
                                                       cd::editor::GizmoAxis::kZ })
                                {
                                    const auto n = cd::editor::axis_dir(ax);
                                    const auto hit = cd::editor::intersect_ray_plane(
                                        *mouse_ray, tgt, n);
                                    if (!hit.has_value()) continue;
                                    const cd::math::Vec3f local {
                                        hit->x - tgt.x, hit->y - tgt.y, hit->z - tgt.z };
                                    const float dist = std::sqrt(
                                        cd::math::dot(local, local));
                                    const float band = std::fabs(dist - kRingRad);
                                    if (band < best_band)
                                    {
                                        best_band = band;
                                        best = ax;
                                    }
                                }
                            }
                            viewport_gizmo.set_hover(best);
                            if (best != cd::editor::GizmoAxis::kNone &&
                                ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                            {
                                cd::math::Vec3f n {};
                                if (gmode == cd::editor::GizmoMode::kRotate)
                                    n = cd::editor::axis_dir(best);
                                else if (cd::editor::is_plane(best))
                                    n = cd::editor::plane_normal(best);
                                else
                                    n = cd::editor::axis_drag_plane_normal(
                                        best, forward);
                                const auto hit0 = cd::editor::intersect_ray_plane(
                                    *mouse_ray, tgt, n);
                                if (hit0.has_value())
                                {
                                    gizmo_drag = {};
                                    gizmo_drag.plane_point  = tgt;
                                    gizmo_drag.plane_normal = n;
                                    switch (gmode)
                                    {
                                        case cd::editor::GizmoMode::kTranslate:
                                            viewport_gizmo.begin_drag(best, *hit0);
                                            // phase1092: the drag origin is
                                            // the CENTROID (gizmo target),
                                            // and every member's start
                                            // position is captured.
                                            gizmo_drag.drag_start_pos = tgt;
                                            gizmo_drag.drag_start_group.clear();
                                            for (const auto sel_e : selection.entries())
                                            {
                                                if (auto* lt = scene.local(sel_e); lt != nullptr)
                                                    gizmo_drag.drag_start_group.emplace_back(
                                                        sel_e, lt->value.position);
                                            }
                                            if (gizmo_drag.drag_start_group.empty())
                                                gizmo_drag.drag_start_group.emplace_back(
                                                    selected, gz_lt->value.position);
                                            break;
                                        case cd::editor::GizmoMode::kRotate:
                                        {
                                            // In-plane basis = the other two
                                            // principal axes (right-handed
                                            // around the ring normal).
                                            switch (best)
                                            {
                                                case cd::editor::GizmoAxis::kX:
                                                    gizmo_drag.ring_u = { 0.0F, 1.0F, 0.0F };
                                                    gizmo_drag.ring_v = { 0.0F, 0.0F, 1.0F };
                                                    break;
                                                case cd::editor::GizmoAxis::kY:
                                                    gizmo_drag.ring_u = { 0.0F, 0.0F, 1.0F };
                                                    gizmo_drag.ring_v = { 1.0F, 0.0F, 0.0F };
                                                    break;
                                                default:
                                                    gizmo_drag.ring_u = { 1.0F, 0.0F, 0.0F };
                                                    gizmo_drag.ring_v = { 0.0F, 1.0F, 0.0F };
                                                    break;
                                            }
                                            const cd::math::Vec3f local {
                                                hit0->x - tgt.x,
                                                hit0->y - tgt.y,
                                                hit0->z - tgt.z };
                                            gizmo_drag.angle_prev = std::atan2(
                                                cd::math::dot(local, gizmo_drag.ring_v),
                                                cd::math::dot(local, gizmo_drag.ring_u));
                                            gizmo_drag.drag_start_rot =
                                                gz_lt->value.rotation;
                                            viewport_gizmo.begin_value_drag(best);
                                            break;
                                        }
                                        case cd::editor::GizmoMode::kScale:
                                            gizmo_drag.grab_hit = *hit0;
                                            gizmo_drag.drag_start_scale =
                                                gz_lt->value.scale;
                                            viewport_gizmo.begin_value_drag(best);
                                            break;
                                    }
                                }
                            }
                        }
                        else if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
                        {
                            const auto hit = cd::editor::intersect_ray_plane(
                                *mouse_ray,
                                gizmo_drag.plane_point,
                                gizmo_drag.plane_normal);
                            if (hit.has_value())
                            {
                                switch (gmode)
                                {
                                    case cd::editor::GizmoMode::kTranslate:
                                    {
                                        viewport_gizmo.update_drag(*hit);
                                        const auto& c0 = gizmo_drag.drag_start_pos;
                                        const auto& c1 = viewport_gizmo.target();
                                        const cd::math::Vec3f d {
                                            c1.x - c0.x, c1.y - c0.y, c1.z - c0.z };
                                        for (const auto& [sel_e, p0] :
                                             gizmo_drag.drag_start_group)
                                        {
                                            if (auto* lt = scene.local(sel_e); lt != nullptr)
                                                lt->value.position = {
                                                    p0.x + d.x, p0.y + d.y, p0.z + d.z };
                                        }
                                        break;
                                    }
                                    case cd::editor::GizmoMode::kRotate:
                                    {
                                        const auto& tgt0 = gizmo_drag.plane_point;
                                        const cd::math::Vec3f local {
                                            hit->x - tgt0.x,
                                            hit->y - tgt0.y,
                                            hit->z - tgt0.z };
                                        const float ang = std::atan2(
                                            cd::math::dot(local, gizmo_drag.ring_v),
                                            cd::math::dot(local, gizmo_drag.ring_u));
                                        float delta = ang - gizmo_drag.angle_prev;
                                        // Shortest-arc unwrap so crossing
                                        // ±pi keeps accumulating smoothly.
                                        constexpr float kPi = std::numbers::pi_v<float>;
                                        if (delta >  kPi) delta -= 2.0F * kPi;
                                        if (delta < -kPi) delta += 2.0F * kPi;
                                        const float total =
                                            viewport_gizmo.drag_value() + delta;
                                        viewport_gizmo.update_value_drag(total);
                                        gizmo_drag.angle_prev = ang;
                                        const auto axis = cd::editor::axis_dir(
                                            viewport_gizmo.active_axis());
                                        gz_lt->value.rotation = cd::math::normalize(
                                            cd::math::Quatf::from_axis_angle(
                                                axis, total) *
                                            gizmo_drag.drag_start_rot);
                                        break;
                                    }
                                    case cd::editor::GizmoMode::kScale:
                                    {
                                        const auto axis = cd::editor::axis_dir(
                                            viewport_gizmo.active_axis());
                                        const cd::math::Vec3f span {
                                            hit->x - gizmo_drag.grab_hit.x,
                                            hit->y - gizmo_drag.grab_hit.y,
                                            hit->z - gizmo_drag.grab_hit.z };
                                        const float along =
                                            cd::math::dot(span, axis);
                                        viewport_gizmo.update_value_drag(along);
                                        const float factor = std::max(
                                            0.01F, 1.0F + along / kArmLen);
                                        auto sc = gizmo_drag.drag_start_scale;
                                        if (axis.x != 0.0F) sc.x *= factor;
                                        if (axis.y != 0.0F) sc.y *= factor;
                                        if (axis.z != 0.0F) sc.z *= factor;
                                        gz_lt->value.scale = sc;
                                        break;
                                    }
                                }
                            }
                        }
                        else
                        {
                            // phase1064/1074: release transition — fold
                            // the whole drag into ONE undoable command
                            // (rewind-then-push: EditHistory::push
                            // applies immediately).
                            switch (gmode)
                            {
                                case cd::editor::GizmoMode::kTranslate:
                                {
                                    (void)viewport_gizmo.end_drag();
                                    // phase1092: total = centroid delta;
                                    // rewind every member then push ONE
                                    // CompositeCommand so a single Ctrl+Z
                                    // unwinds the whole group move.
                                    const auto& start_c = gizmo_drag.drag_start_pos;
                                    const auto end_c = viewport_gizmo.target();
                                    const cd::math::Vec3f total {
                                        end_c.x - start_c.x,
                                        end_c.y - start_c.y,
                                        end_c.z - start_c.z };
                                    const float len2 = total.x * total.x +
                                        total.y * total.y + total.z * total.z;
                                    if (len2 > 1e-10F)
                                    {
                                        auto comp = std::make_unique<
                                            cd::editor::CompositeCommand>(
                                            "Translate selection");
                                        for (const auto& [sel_e, p0] :
                                             gizmo_drag.drag_start_group)
                                        {
                                            if (auto* lt = scene.local(sel_e); lt != nullptr)
                                            {
                                                lt->value.position = p0;  // rewind
                                                comp->add(std::make_unique<
                                                    cd::editor::TranslateCommand>(
                                                    scene, sel_e, total));
                                            }
                                        }
                                        history.push(std::move(comp));
                                    }
                                    break;
                                }
                                case cd::editor::GizmoMode::kRotate:
                                {
                                    const float total =
                                        viewport_gizmo.end_value_drag();
                                    if (std::fabs(total) > 1e-6F)
                                    {
                                        const auto final_rot =
                                            gz_lt->value.rotation;
                                        gz_lt->value.rotation =
                                            gizmo_drag.drag_start_rot;  // rewind
                                        history.push(
                                            std::make_unique<cd::editor::RotateCommand>(
                                                scene, selected, final_rot));
                                    }
                                    break;
                                }
                                case cd::editor::GizmoMode::kScale:
                                {
                                    const float along =
                                        viewport_gizmo.end_value_drag();
                                    const float factor = std::max(
                                        0.01F, 1.0F + along / kArmLen);
                                    if (std::fabs(factor - 1.0F) > 1e-6F)
                                    {
                                        // Reconstruct the per-axis factor
                                        // from start vs live scale.
                                        const auto& s0 = gizmo_drag.drag_start_scale;
                                        const auto& s1 = gz_lt->value.scale;
                                        const cd::math::Vec3f f {
                                            s0.x != 0.0F ? s1.x / s0.x : 1.0F,
                                            s0.y != 0.0F ? s1.y / s0.y : 1.0F,
                                            s0.z != 0.0F ? s1.z / s0.z : 1.0F };
                                        gz_lt->value.scale = s0;  // rewind
                                        history.push(
                                            std::make_unique<cd::editor::ScaleCommand>(
                                                scene, selected, f));
                                    }
                                    break;
                                }
                            }
                        }
                    }
                }
            }

            if (dbg_renderer.is_valid())
            {
                dbg_batch.add_grid({ 0.0F, 0.0F, 0.0F },
                                   { 1.0F, 0.0F, 0.0F },
                                   { 0.0F, 0.0F, 1.0F },
                                   10, 1.0F,
                                   { 0.32F, 0.33F, 0.38F, 1.0F });
                // phase1093: freeze-frustum — snapshot once on toggle,
                // then draw the frozen frustum every frame (teal).
                if (freeze_frustum && !frozen_frustum_valid)
                {
                    frozen_inv_vp = cd::math::inverse(vp);
                    frozen_frustum_valid = true;
                }
                if (frozen_frustum_valid)
                {
                    dbg_batch.add_frustum(frozen_inv_vp,
                                          { 0.25F, 0.85F, 0.80F, 1.0F });
                }
                // phase1097: light gizmos — cross at the position in
                // the light's own colour; point/spot add the range
                // ring (XZ), directionals add a direction arrow.
                for (const auto& row : light_rows)
                {
                    if (!row.enabled) continue;
                    const cd::math::Vec4f lc { row.light.color.x,
                                               row.light.color.y,
                                               row.light.color.z, 1.0F };
                    dbg_batch.add_cross(row.light.position, 0.25F, lc);
                    if (row.light.type == cd::light::LightType::kDirectional)
                    {
                        const auto& d = row.light.direction;
                        dbg_batch.add_arrow(
                            row.light.position,
                            { row.light.position.x + d.x * 1.5F,
                              row.light.position.y + d.y * 1.5F,
                              row.light.position.z + d.z * 1.5F },
                            lc);
                    }
                    else if (row.light.range > 0.0F)
                    {
                        dbg_batch.add_circle(row.light.position,
                                             { 0.0F, 1.0F, 0.0F },
                                             row.light.range, 48, lc);
                    }
                }
                if (selected.id != 0)
                {
                    // phase1092: one box per selection member — primary
                    // keeps the bright orange, secondaries draw dimmer.
                    for (const auto sel_e : selection.entries())
                    {
                        auto* box_lt = scene.local(sel_e);
                        if (box_lt == nullptr)
                            continue;
                        const auto& btr = box_lt->value;
                        const cd::math::Vec3f bhalf {
                            0.6F * btr.scale.x,
                            0.6F * btr.scale.y,
                            0.6F * btr.scale.z };
                        const bool is_primary = (sel_e == selected);
                        dbg_batch.add_aabb(
                            { btr.position.x - bhalf.x,
                              btr.position.y - bhalf.y,
                              btr.position.z - bhalf.z },
                            { btr.position.x + bhalf.x,
                              btr.position.y + bhalf.y,
                              btr.position.z + bhalf.z },
                            is_primary
                                ? cd::math::Vec4f { 0.95F, 0.60F, 0.15F, 1.0F }
                                : cd::math::Vec4f { 0.75F, 0.50F, 0.20F, 1.0F });
                    }
                    if (auto* sel_lt = scene.local(selected);
                        sel_lt != nullptr)
                    {

                        // phase1063: translate gizmo arrows — base
                        // R/G/B, hover brightened, active near-white.
                        // phase1074: rotate mode draws rings instead;
                        // scale mode draws arms with cross end-caps.
                        constexpr float kArmLen = 1.2F;
                        const auto gmode_r = viewport_gizmo.mode();
                        const auto tgt = viewport_gizmo.target();
                        if (gmode_r != cd::editor::GizmoMode::kRotate)
                        for (const auto ax : { cd::editor::GizmoAxis::kX,
                                               cd::editor::GizmoAxis::kY,
                                               cd::editor::GizmoAxis::kZ })
                        {
                            const auto dir = cd::editor::axis_dir(ax);
                            cd::math::Vec4f tint {
                                ax == cd::editor::GizmoAxis::kX ? 0.85F : 0.20F,
                                ax == cd::editor::GizmoAxis::kY ? 0.85F : 0.25F,
                                ax == cd::editor::GizmoAxis::kZ ? 0.85F : 0.25F,
                                1.0F };
                            if (viewport_gizmo.active_axis() == ax)
                                tint = { 1.0F, 1.0F, 0.85F, 1.0F };
                            else if (viewport_gizmo.hover() == ax)
                            {
                                tint.x = std::min(tint.x + 0.35F, 1.0F);
                                tint.y = std::min(tint.y + 0.35F, 1.0F);
                                tint.z = std::min(tint.z + 0.35F, 1.0F);
                            }
                            const cd::math::Vec3f tip {
                                tgt.x + dir.x * kArmLen,
                                tgt.y + dir.y * kArmLen,
                                tgt.z + dir.z * kArmLen };
                            if (gmode_r == cd::editor::GizmoMode::kScale)
                            {
                                dbg_batch.add_line(tgt, tip, tint);
                                dbg_batch.add_cross(tip, 0.08F, tint);
                            }
                            else
                            {
                                dbg_batch.add_arrow(tgt, tip, tint);
                            }
                        }
                        if (gmode_r == cd::editor::GizmoMode::kRotate)
                        {
                            constexpr float kRingRad = 1.05F;
                            for (const auto ax : { cd::editor::GizmoAxis::kX,
                                                   cd::editor::GizmoAxis::kY,
                                                   cd::editor::GizmoAxis::kZ })
                            {
                                cd::math::Vec4f tint {
                                    ax == cd::editor::GizmoAxis::kX ? 0.85F : 0.20F,
                                    ax == cd::editor::GizmoAxis::kY ? 0.85F : 0.25F,
                                    ax == cd::editor::GizmoAxis::kZ ? 0.85F : 0.25F,
                                    1.0F };
                                if (viewport_gizmo.active_axis() == ax)
                                    tint = { 1.0F, 1.0F, 0.85F, 1.0F };
                                else if (viewport_gizmo.hover() == ax)
                                {
                                    tint.x = std::min(tint.x + 0.35F, 1.0F);
                                    tint.y = std::min(tint.y + 0.35F, 1.0F);
                                    tint.z = std::min(tint.z + 0.35F, 1.0F);
                                }
                                dbg_batch.add_circle(
                                    tgt, cd::editor::axis_dir(ax),
                                    kRingRad, 48, tint);
                            }
                        }
                        // phase1073: XY/XZ/YZ pad squares between the
                        // arrows — outline only (4 lines per pad),
                        // tinted by the two member axes, hover/active
                        // brightened like the arrows. Translate only.
                        constexpr float kPadMin = 0.30F;
                        constexpr float kPadMax = 0.62F;
                        if (gmode_r == cd::editor::GizmoMode::kTranslate)
                        for (const auto pad : { cd::editor::GizmoAxis::kXY,
                                                cd::editor::GizmoAxis::kXZ,
                                                cd::editor::GizmoAxis::kYZ })
                        {
                            cd::math::Vec3f a1 {};
                            cd::math::Vec3f a2 {};
                            switch (pad)
                            {
                                case cd::editor::GizmoAxis::kXY:
                                    a1 = { 1.0F, 0.0F, 0.0F };
                                    a2 = { 0.0F, 1.0F, 0.0F }; break;
                                case cd::editor::GizmoAxis::kXZ:
                                    a1 = { 1.0F, 0.0F, 0.0F };
                                    a2 = { 0.0F, 0.0F, 1.0F }; break;
                                default:
                                    a1 = { 0.0F, 1.0F, 0.0F };
                                    a2 = { 0.0F, 0.0F, 1.0F }; break;
                            }
                            cd::math::Vec4f tint {
                                0.5F * (a1.x + a2.x) + 0.15F,
                                0.5F * (a1.y + a2.y) + 0.15F,
                                0.5F * (a1.z + a2.z) + 0.15F,
                                1.0F };
                            if (viewport_gizmo.active_axis() == pad)
                                tint = { 1.0F, 1.0F, 0.85F, 1.0F };
                            else if (viewport_gizmo.hover() == pad)
                            {
                                tint.x = std::min(tint.x + 0.35F, 1.0F);
                                tint.y = std::min(tint.y + 0.35F, 1.0F);
                                tint.z = std::min(tint.z + 0.35F, 1.0F);
                            }
                            const float lo = kPadMin * kArmLen;
                            const float hi = kPadMax * kArmLen;
                            const auto corner =
                                [&](float u, float v) -> cd::math::Vec3f
                            {
                                return { tgt.x + a1.x * u + a2.x * v,
                                         tgt.y + a1.y * u + a2.y * v,
                                         tgt.z + a1.z * u + a2.z * v };
                            };
                            const auto c00 = corner(lo, lo);
                            const auto c10 = corner(hi, lo);
                            const auto c11 = corner(hi, hi);
                            const auto c01 = corner(lo, hi);
                            dbg_batch.add_line(c00, c10, tint);
                            dbg_batch.add_line(c10, c11, tint);
                            dbg_batch.add_line(c11, c01, tint);
                            dbg_batch.add_line(c01, c00, tint);
                        }
                    }
                }
                dbg_renderer.flush(device, cmd, dbg_batch, vp, frame_idx);
                dbg_batch.clear();
            }
        }

        // ==================================================================
        // ImGui UI
        // ==================================================================
        ctx.new_frame();

        // ---- DockSpace (full-window host) --------------------------------
        {
            const ImGuiViewport* main_vp = ImGui::GetMainViewport();
            ImGui::SetNextWindowPos(main_vp->WorkPos);
            ImGui::SetNextWindowSize(main_vp->WorkSize);
            ImGui::SetNextWindowViewport(main_vp->ID);
            const ImGuiWindowFlags host_flags =
                ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar |
                ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize  |
                ImGuiWindowFlags_NoMove     | ImGuiWindowFlags_NoBringToFrontOnFocus |
                ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_NoBackground;
            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0F);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0F);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2 { 0, 0 });
            ImGui::Begin("##DockSpaceHost", nullptr, host_flags);
            ImGui::PopStyleVar(3);

            const ImGuiID dock_id = ImGui::GetID("CDDockSpace");
            if (!dock_initialised && ImGui::DockBuilderGetNode(dock_id) == nullptr)
            {
                ImGui::DockBuilderRemoveNode(dock_id);
                const int dock_flags =
                    static_cast<int>(ImGuiDockNodeFlags_DockSpace) |
                    static_cast<int>(ImGuiDockNodeFlags_PassthruCentralNode);
                ImGui::DockBuilderAddNode(dock_id, static_cast<ImGuiDockNodeFlags>(dock_flags));
                ImGui::DockBuilderSetNodeSize(dock_id, main_vp->WorkSize);

                ImGuiID dock_main  = dock_id;
                ImGuiID dock_top   = ImGui::DockBuilderSplitNode(dock_main, ImGuiDir_Up,    0.13F, nullptr, &dock_main);
                ImGuiID dock_bot   = ImGui::DockBuilderSplitNode(dock_main, ImGuiDir_Down,  0.22F, nullptr, &dock_main);
                ImGuiID dock_left  = ImGui::DockBuilderSplitNode(dock_main, ImGuiDir_Left,  0.20F, nullptr, &dock_main);
                ImGuiID dock_right = ImGui::DockBuilderSplitNode(dock_main, ImGuiDir_Right, 0.28F, nullptr, &dock_main);

                ImGui::DockBuilderDockWindow("Toolbar",    dock_top);
                ImGui::DockBuilderDockWindow("Scene Tree", dock_left);
                ImGui::DockBuilderDockWindow("Inspector",  dock_right);
                ImGui::DockBuilderDockWindow("Console",    dock_bot);
                ImGui::DockBuilderFinish(dock_id);
                dock_initialised = true;
            }
            ImGui::DockSpace(dock_id, ImVec2 { 0, 0 }, ImGuiDockNodeFlags_PassthruCentralNode);
            ImGui::End();
        }

        // ---- Fallback window sizes for the first frame -------------------
        const float vw      = static_cast<float>(frame.extent.width);
        const float vh      = static_cast<float>(frame.extent.height);
        const float gutter  = 8.0F;
        const float tool_h  = 110.0F;
        const float scene_w = 260.0F;
        const float insp_w  = 460.0F;
        const float cons_h  = 180.0F;

        // ---- Toolbar (undo / redo) ----------------------------------------
        ImGui::SetNextWindowPos(ImVec2 { gutter, gutter }, ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2 { vw - 2.0F * gutter, tool_h }, ImGuiCond_FirstUseEver);
        ImGui::Begin("Project");
        {
            ImGui::Text("%.*s", static_cast<int>(project->name().size()),
                        project->name().data());
            if (ImGui::SmallButton("Save Project"))
            {
                if (save_project_file(kProjectPath, *project).has_value())
                    log_push("[project] saved " + std::string { kProjectPath });
                else
                    log_push("[project] save FAILED");
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Save Scene -> Level"))
            {
                if (const auto* lvl = project->level(active_level_idx);
                    lvl != nullptr && !lvl->scene_path().empty())
                    save_scene_to(std::string { lvl->scene_path() });
            }
            ImGui::Separator();
            for (std::size_t li = 0; li < project->level_count(); ++li)
            {
                const auto* lvl = project->level(li);
                if (lvl == nullptr) continue;
                ImGui::PushID(static_cast<int>(li) + 7000);
                const bool is_active = (li == active_level_idx);
                std::string row { lvl->name() };
                row += "  (";
                row += lvl->scene_path().empty()
                           ? std::string { "no scene" }
                           : std::string { lvl->scene_path() };
                row += ")";
                if (ImGui::Selectable(row.c_str(), is_active) && !is_active)
                {
                    active_level_idx = li;
                    if (!lvl->scene_path().empty())
                        load_scene_from(std::string { lvl->scene_path() });
                    log_push("[project] active level: " +
                             std::string { lvl->name() });
                }
                ImGui::PopID();
            }
            if (ImGui::SmallButton("+ Level"))
            {
                const auto n = project->level_count() + 1;
                auto* lvl = project->add_level("Level " + std::to_string(n));
                lvl->set_scene_path("level_" + std::to_string(n) +
                                    ".cdscene.json");
                log_push("[project] added level " + std::to_string(n));
            }
        }
        ImGui::End();

        ImGui::Begin("Toolbar");
        {
            // Undo / Redo
            const bool can_undo = history.can_undo();
            const bool can_redo = history.can_redo();
            if (!can_undo) ImGui::BeginDisabled();
            if (ImGui::Button("Undo"))
            {
                if (history.undo())
                    log_push("undo: " + std::string { history.next_redo_label() });
            }
            if (!can_undo) ImGui::EndDisabled();
            ImGui::SameLine();
            if (!can_redo) ImGui::BeginDisabled();
            if (ImGui::Button("Redo"))
            {
                std::string lbl { history.next_redo_label() };
                if (history.redo()) log_push("redo: " + lbl);
            }
            if (!can_redo) ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::Text("depth: undo=%zu redo=%zu",
                        history.undo_depth(), history.redo_depth());

            // Undo label
            if (can_undo)
            {
                std::string lbl { history.next_undo_label() };
                ImGui::TextDisabled("next undo: %s", lbl.c_str());
            }

            ImGui::Separator();
            // phase1093: freeze-frustum toggle. Snapshot happens in the
            // render block below where `vp` is in scope (frame ordering:
            // the snapshot uses the SAME frame's matrix the user saw).
            ImGui::Checkbox("Freeze frustum", &freeze_frustum);
            if (!freeze_frustum)
                frozen_frustum_valid = false;
            ImGui::SameLine();
            ImGui::TextDisabled(frozen_frustum_valid
                                    ? "(frozen — fly outside to inspect)"
                                    : "(snapshots this frame's camera)");

            ImGui::Separator();
            // phase1097: light rows — bridge-editable light setup.
            if (ImGui::CollapsingHeader("Lights"))
            {
                int remove_at = -1;
                for (int li = 0; std::cmp_less(li,light_rows.size()); ++li)
                {
                    auto& row = light_rows[static_cast<std::size_t>(li)];
                    ImGui::PushID(li + 9000);
                    ImGui::Checkbox("##on", &row.enabled);
                    ImGui::SameLine();
                    ImGui::Text("%s", row.name.c_str());
                    ImGui::SameLine();
                    if (ImGui::SmallButton("x")) remove_at = li;
                    ImGui::DragFloat3("pos", &row.light.position.x, 0.05F);
                    ImGui::DragFloat("intensity", &row.light.intensity,
                                     0.5F, 0.0F, 10000.0F);
                    ImGui::ColorEdit3("color", &row.light.color.x,
                                      ImGuiColorEditFlags_NoInputs);
                    ImGui::PopID();
                }
                if (remove_at >= 0)
                    light_rows.erase(light_rows.begin() + remove_at);
                if (ImGui::Button("+ Point Light"))
                {
                    EditorLightRow row {};
                    row.name = "Point " + std::to_string(light_rows.size() + 1);
                    row.light.type = cd::light::LightType::kPoint;
                    row.light.position = { 0.0F, 2.0F, 0.0F };
                    row.light.range = 6.0F;
                    row.light.intensity = 60.0F;
                    light_rows.push_back(std::move(row));
                }
            }

            ImGui::Separator();
            ImGui::TextDisabled("Ctrl+Shift+P: command palette   |   WASD + RMB: free-fly camera");
        }
        ImGui::End();

        // ---- Scene tree (HierarchyView) ----------------------------------
        ImGui::SetNextWindowPos(
            ImVec2 { gutter, gutter + tool_h + gutter }, ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(
            ImVec2 { scene_w, vh - tool_h - cons_h - 4.0F * gutter }, ImGuiCond_FirstUseEver);
        ImGui::Begin("Scene Tree");
        {
            // The HierarchyView produces (entity, depth) pairs in DFS
            // order for all currently-visible rows. We render them as
            // indented selectables. Clicking a row sets the selection;
            // clicking the expand triangle toggles expand/collapse.
            const auto rows = hierarchy.visible_order(scene);

            if (rows.empty())
            {
                ImGui::TextDisabled("(empty scene)");
            }
            else
            {
                for (const auto& [ent, depth] : rows)
                {
                    const float indent = static_cast<float>(depth) * 14.0F;
                    if (indent > 0.0F) ImGui::Indent(indent);

                    // Has children? Show expand/collapse toggle.
                    const auto* kids = scene.children_of(ent);
                    const bool has_children = (kids != nullptr && !kids->entities.empty());

                    if (has_children)
                    {
                        // Small triangle button beside the label.
                        const char* arrow = hierarchy.is_expanded(ent) ? "v " : "> ";
                        ImGui::PushID(static_cast<int>(ent.id) * 2);
                        if (ImGui::SmallButton(arrow))
                            hierarchy.toggle(ent);
                        ImGui::PopID();
                        ImGui::SameLine();
                    }

                    // Determine display label.
                    const EntityMeta* meta = find_meta(ent);
                    const std::string label = meta
                        ? meta->display_name
                        : entity_label(ent);

                    // Selectable row.
                    const bool is_selected = selection.contains(ent);
                    ImGui::PushID(static_cast<int>(ent.id));
                    if (ImGui::Selectable(label.c_str(), is_selected,
                                         ImGuiSelectableFlags_None,
                                         ImVec2 { 0.0F, 0.0F }))
                    {
                        // phase1092: Ctrl+click toggles membership;
                        // plain click single-selects.
                        if (imgui_io.KeyCtrl)
                        {
                            if (is_selected)
                                selection.remove(ent);
                            else
                                selection.add(ent);
                            selected = selection.primary();
                            inspector_panel.set_target(selected);
                            rot_slider_deg    = 0.0F;
                            rot_slider_entity = {};
                            log_push("selection: " +
                                     std::to_string(selection.size()) +
                                     " entities");
                        }
                        else if (!is_selected || selection.size() > 1)
                        {
                            selection.clear();
                            selection.add(ent);
                            selected = ent;
                            inspector_panel.set_target(selected);
                            rot_slider_deg    = 0.0F;
                            rot_slider_entity = {};
                            log_push("selected: " + label);
                        }
                    }
                    ImGui::PopID();
                    // phase1111: dim layer suffix for organised rows.
                    {
                        const auto lay_name =
                            cd::world_container::layer_of(world, ent);
                        if (lay_name != cd::world_container::kDefaultLayerName)
                        {
                            ImGui::SameLine();
                            ImGui::TextDisabled("[%.*s]",
                                static_cast<int>(lay_name.size()),
                                lay_name.data());
                        }
                    }

                    if (indent > 0.0F) ImGui::Unindent(indent);
                }
            }
        }
        ImGui::End();

        // ---- Inspector ---------------------------------------------------
        ImGui::SetNextWindowPos(
            ImVec2 { vw - insp_w - gutter, gutter + tool_h + gutter },
            ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(
            ImVec2 { insp_w, vh - tool_h - cons_h - 4.0F * gutter },
            ImGuiCond_FirstUseEver);
        ImGui::Begin("Inspector");
        {
            // Display name above the panel (the library reports entity id only;
            // we add the display name from our metadata table here).
            if (selected.is_valid())
            {
                const EntityMeta* meta = find_meta(selected);
                const std::string label = meta ? meta->display_name : entity_label(selected);
                ImGui::Text("Entity: %s  (id=%u)", label.c_str(), selected.id);
                ImGui::Separator();
            }
            // Delegate all drag-field / slider rendering to the Inspector panel.
            (void)inspector_panel.draw_imgui(scene, history,
                                             rot_slider_deg, rot_slider_entity);
        }
        ImGui::End();

        // ---- Command palette popup ---------------------------------------
        if (palette_visible)
        {
            const float pw = 520.0F, ph = 300.0F;
            ImGui::SetNextWindowPos(ImVec2 { (vw - pw) * 0.5F, 80.0F }, ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImVec2 { pw, ph }, ImGuiCond_Always);
            const ImGuiWindowFlags pf =
                ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
                ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking;
            if (ImGui::Begin("Command Palette", &palette_visible, pf))
            {
                if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
                char buf[128] {};
                std::snprintf(buf, sizeof(buf), "%s", palette_query.c_str());
                if (ImGui::InputText("##pq", buf, sizeof(buf)))
                    palette_query = buf;
                ImGui::Separator();
                const auto hits = palette.filter(palette_query);
                if (hits.empty())
                {
                    ImGui::TextDisabled("no match (%zu commands)", palette.size());
                }
                else
                {
                    for (std::size_t i = 0; i < hits.size() && i < 12; ++i)
                    {
                        const auto& entry = palette.at(hits[i]);
                        char row[160] {};
                        std::snprintf(row, sizeof(row), "  %s", entry.label.c_str());
                        if (ImGui::Selectable(row))
                        {
                            (void)palette.invoke(hits[i]);
                            palette_visible = false;
                            palette_query.clear();
                            break;
                        }
                    }
                }
                if (ImGui::IsKeyPressed(ImGuiKey_Enter, false) && !hits.empty())
                {
                    (void)palette.invoke(hits.front());
                    palette_visible = false;
                    palette_query.clear();
                }
            }
            ImGui::End();
        }

        // ---- Console (log output) ----------------------------------------
        ImGui::SetNextWindowPos(
            ImVec2 { gutter, vh - cons_h - gutter }, ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(
            ImVec2 { vw - 2.0F * gutter, cons_h }, ImGuiCond_FirstUseEver);
        ImGui::Begin("Console");
        {
            const auto& log_entries = console_panel.entries();
            for (auto it = log_entries.rbegin(); it != log_entries.rend(); ++it)
                ImGui::TextUnformatted(it->c_str());
        }
        ImGui::End();

        // ==================================================================
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

    // Destroy glTF GPU resources.
    cd_sample::destroy_generic_ingests(device, gltf_ingests);

    dbg_renderer.destroy(device);  // phase1062 — device idle here
    std::printf("hello_editor T3.2: clean exit (%u frames).\n", frame_idx);
    return 0;
}
