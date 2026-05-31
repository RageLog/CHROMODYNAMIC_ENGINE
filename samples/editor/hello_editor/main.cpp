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
#include <sstream>
#include <string>
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
    constexpr float kPi = 3.14159265358979F;
    m.verts.reserve(static_cast<std::size_t>((stacks + 1) * (slices + 1)));
    for (int i = 0; i <= stacks; ++i)
    {
        const float phi   = static_cast<float>(i) / static_cast<float>(stacks) * kPi;
        const float sin_p = std::sin(phi);
        const float cos_p = std::cos(phi);
        for (int j = 0; j <= slices; ++j)
        {
            const float theta = static_cast<float>(j) / static_cast<float>(slices) * 2.0F * kPi;
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
    constexpr float kPi = 3.14159265358979F;
    m.verts.reserve(static_cast<std::size_t>(2 + 2 * slices));
    m.verts.push_back({ {  0.0F,  0.5F, 0.0F }, { 1.00F, 1.00F, 1.00F } });
    m.verts.push_back({ {  0.0F, -0.5F, 0.0F }, { 0.30F, 0.30F, 0.30F } });
    for (int i = 0; i < slices; ++i)
    {
        const float t = static_cast<float>(i) / static_cast<float>(slices) * 2.0F * kPi;
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
        const float t = static_cast<float>(i) / static_cast<float>(slices) * 2.0F * kPi;
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

    // ---- Selected entity (invalid = no selection) ------------------------
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
                entity_metas.push_back(m);
            }
        }
        // Auto-select the first root so the inspector is non-empty on boot.
        if (!gltf_ingests.empty() && gltf_ingests.front().result.root_entity.id != 0)
        {
            selected = gltf_ingests.front().result.root_entity;
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
            entity_metas.push_back(m);
        }
        if (!entity_metas.empty())
        {
            selected = entity_metas.front().handle;
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
    palette.register_command(10, "Select: First Entity",
        [&]() {
            if (!entity_metas.empty()) {
                selected = entity_metas.front().handle;
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
                    const bool is_selected = (selected.id == ent.id);
                    ImGui::PushID(static_cast<int>(ent.id));
                    if (ImGui::Selectable(label.c_str(), is_selected,
                                         ImGuiSelectableFlags_None,
                                         ImVec2 { 0.0F, 0.0F }))
                    {
                        if (!is_selected)
                        {
                            selected = ent;
                            inspector_panel.set_target(selected);
                            rot_slider_deg    = 0.0F;
                            rot_slider_entity = {};
                            log_push("selected: " + label);
                        }
                    }
                    ImGui::PopID();

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

    std::printf("hello_editor T3.2: clean exit (%u frames).\n", frame_idx);
    return 0;
}
