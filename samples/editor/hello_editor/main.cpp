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

#include <cd/asset/json/Json.hpp>
#include <cd/ecs/World.hpp>
#include <cd/editor/CommandPalette.hpp>
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
#include <cd/rhi/vulkan/VulkanDevice.hpp>
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
    enum class MeshKind : std::uint8_t { kCube, kSphere, kCone } mesh { MeshKind::kCube };
};

// Shared vertex format for all three viewport meshes.
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

// Build a unit-radius UV-sphere (radius 0.5 to match cube ø). Vertex
// color is a smooth normal-to-pastel mapping so the sphere reads as a
// 3D object even before per-entity tint is applied.
struct MeshCpu
{
    std::vector<CubeVertex>   verts;
    std::vector<std::uint16_t> idx;
};

[[nodiscard]] inline MeshCpu make_sphere(int stacks = 18, int slices = 24)
{
    MeshCpu m;
    constexpr float kPi = 3.14159265358979F;
    m.verts.reserve(static_cast<std::size_t>((stacks + 1) * (slices + 1)));
    for (int i = 0; i <= stacks; ++i)
    {
        const float phi    = static_cast<float>(i) / static_cast<float>(stacks) * kPi;
        const float sin_p  = std::sin(phi);
        const float cos_p  = std::cos(phi);
        for (int j = 0; j <= slices; ++j)
        {
            const float theta = static_cast<float>(j) / static_cast<float>(slices) * 2.0F * kPi;
            const float sin_t = std::sin(theta);
            const float cos_t = std::cos(theta);
            CubeVertex v {};
            v.pos[0] = 0.5F * sin_p * cos_t;
            v.pos[1] = 0.5F * cos_p;
            v.pos[2] = 0.5F * sin_p * sin_t;
            // Pastel normal mapping: x*0.5+0.5 → 0..1 per axis.
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

// Build a cone — apex at +Y 0.5, circular base at -Y 0.5, radius 0.5.
// Sides + base disk both rendered. Vertex color goes from white at the
// apex to mid-grey at the base so the cone shape reads from any angle.
[[nodiscard]] inline MeshCpu make_cone(int slices = 32)
{
    MeshCpu m;
    constexpr float kPi = 3.14159265358979F;
    // Indexing layout:
    //   0                     = apex
    //   1                     = base center
    //   2 .. 2+slices-1       = base ring vertices (for the side fan)
    //   2+slices .. 2+2*slices-1 = base ring vertices again (for the disk fan)
    // Side + base use separate rings so each can have its own
    // (color / normal-like) attribute without sharing a vertex.
    m.verts.reserve(static_cast<std::size_t>(2 + 2 * slices));
    m.verts.push_back({ {  0.0F,  0.5F, 0.0F }, { 1.00F, 1.00F, 1.00F } });  // apex
    m.verts.push_back({ {  0.0F, -0.5F, 0.0F }, { 0.30F, 0.30F, 0.30F } });  // base center
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
    // Sides — apex (0) → ring[i] → ring[i+1]
    for (int i = 0; i < slices; ++i)
    {
        const auto a = static_cast<std::uint16_t>(2 + i);
        const auto b = static_cast<std::uint16_t>(2 + (i + 1) % slices);
        m.idx.push_back(0);
        m.idx.push_back(a);
        m.idx.push_back(b);
    }
    // Base disk — center (1) → ring[i+1] → ring[i] (CCW from below)
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

[[nodiscard]] inline SceneEntity::MeshKind mesh_kind_from_name(const std::string& name) noexcept
{
    if (name == "Sphere") return SceneEntity::MeshKind::kSphere;
    if (name == "Cone")   return SceneEntity::MeshKind::kCone;
    return SceneEntity::MeshKind::kCube;
}

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

    cd::rhi::vulkan::VulkanCreateInfo vci {};
    auto device_r = cd::rhi::vulkan::create_vulkan_device(vci);
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

    // Procedural sphere + cone meshes so entities named "Sphere" / "Cone"
    // draw their actual primitive shape instead of always rendering as a
    // tinted cube. Index buffers store the per-mesh triangle count for
    // the draw_indexed dispatch below.
    const auto sphere_cpu = make_sphere(20, 28);
    const auto cone_cpu   = make_cone(40);
    const std::span<const std::byte> sphere_vb_bytes {
        reinterpret_cast<const std::byte*>(sphere_cpu.verts.data()),
        sphere_cpu.verts.size() * sizeof(CubeVertex) };
    const std::span<const std::byte> sphere_ib_bytes {
        reinterpret_cast<const std::byte*>(sphere_cpu.idx.data()),
        sphere_cpu.idx.size() * sizeof(std::uint16_t) };
    const std::span<const std::byte> cone_vb_bytes {
        reinterpret_cast<const std::byte*>(cone_cpu.verts.data()),
        cone_cpu.verts.size() * sizeof(CubeVertex) };
    const std::span<const std::byte> cone_ib_bytes {
        reinterpret_cast<const std::byte*>(cone_cpu.idx.data()),
        cone_cpu.idx.size() * sizeof(std::uint16_t) };
    auto sphere_vb = make_upload_buf(sphere_vb_bytes, cd::rhi::BufferUsage::kVertex);
    auto sphere_ib = make_upload_buf(sphere_ib_bytes, cd::rhi::BufferUsage::kIndex);
    auto cone_vb   = make_upload_buf(cone_vb_bytes,   cd::rhi::BufferUsage::kVertex);
    auto cone_ib   = make_upload_buf(cone_ib_bytes,   cd::rhi::BufferUsage::kIndex);
    const auto sphere_index_count = static_cast<std::uint32_t>(sphere_cpu.idx.size());
    const auto cone_index_count   = static_cast<std::uint32_t>(cone_cpu.idx.size());

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

    // Phase 111: register the fuzzy CommandPalette + a Ctrl+Shift+P
    // hotkey. Palette is visible as a popup (ImGui InputText + filter
    // list) only when `palette_visible` is true. Caller-side state:
    cd::editor::CommandPalette palette;
    bool        palette_visible = false;
    std::string palette_query;
    palette_query.reserve(64);

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
            e.mesh = mesh_kind_from_name(e.name);
            scene.local(e.handle)->value.position = s.pos;
            entities.push_back(std::move(e));
        }
    }
    log_push("Spawned 3 entities (Cube, Sphere, Cone) — each draws its own primitive mesh");

    int selected = 0;  // moved before palette registration (Phase 111
                       // commands capture &selected via lambda).

    // Phase 111: command registry. Each command captures the bits of
    // editor state it touches (history, log, entities) via &-reference
    // capture. The palette is rendering-agnostic — we just consult
    // its filter() output and render a popup ourselves below.
    palette.register_command(1, "Edit: Undo",
        [&]() { if (history.undo()) log_push("palette: Undo"); });
    palette.register_command(2, "Edit: Redo",
        [&]() { if (history.redo()) log_push("palette: Redo"); });
    palette.register_command(3, "Edit: Clear History",
        [&]() { history.clear(); log_push("palette: history cleared"); });
    palette.register_command(10, "Select: Cube",
        [&]() { for (std::size_t i = 0; i < entities.size(); ++i)
                  if (entities[i].name == "Cube") { selected = static_cast<int>(i); log_push("palette: select Cube"); break; } });
    palette.register_command(11, "Select: Sphere",
        [&]() { for (std::size_t i = 0; i < entities.size(); ++i)
                  if (entities[i].name == "Sphere") { selected = static_cast<int>(i); log_push("palette: select Sphere"); break; } });
    palette.register_command(12, "Select: Cone",
        [&]() { for (std::size_t i = 0; i < entities.size(); ++i)
                  if (entities[i].name == "Cone") { selected = static_cast<int>(i); log_push("palette: select Cone"); break; } });
    palette.register_command(20, "Transform: Reset Selected",
        [&]() {
            if (selected >= 0 && selected < static_cast<int>(entities.size()))
            {
                auto& ent = entities[static_cast<std::size_t>(selected)];
                if (auto* lt = scene.local(ent.handle); lt != nullptr)
                {
                    lt->value.position = {};
                    lt->value.scale = { 1.0F, 1.0F, 1.0F };
                    lt->value.rotation = { 0.0F, 0.0F, 0.0F, 1.0F };
                    log_push("palette: reset selected transform");
                }
            }
        });
    palette.register_command(30, "View: Toggle Auto-Spin Camera",
        [&]() { /* hook for SceneCameraController when integrated */
                log_push("palette: TODO toggle auto-spin (Phase 110 hook)"); });
    palette.register_command(40, "Help: Print Shortcuts",
        [&]() {
            log_push("Ctrl+Shift+P : open command palette");
            log_push("Ctrl+Z / Ctrl+Y : undo / redo");
            log_push("Esc : close palette / exit");
        });

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
            {
                // Phase 111: Esc closes the palette first; only on the
                // second press does it quit the app.
                if (palette_visible) { palette_visible = false; palette_query.clear(); }
                else                  window.request_close();
            }
            else if (e.kind == cd::platform::OSEventKind::kResize)
                needs_rebuild = true;
            else if (e.kind == cd::platform::OSEventKind::kKeyDown &&
                     e.key == cd::platform::KeyCode::kP)
            {
                // Ctrl+Shift+P toggles the command palette. We read the
                // current keyboard state from ImGui's IO since the
                // platform event doesn't carry modifier flags directly
                // — ImGui already mirrors them via Context::handle_event.
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
                // (FPS / production / UE convention: drag direction equals
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

            // Group by mesh kind so we bind each pair of VB/IB once,
            // not three times per kind. (For 3 entities this is mostly
            // pedagogical; with hundreds the saved bind calls matter.)
            auto draw_kind = [&](SceneEntity::MeshKind kind,
                                 cd::rhi::BufferHandle vb,
                                 cd::rhi::BufferHandle ib,
                                 std::uint32_t index_count)
            {
                bool bound = false;
                for (const auto& ent : entities)
                {
                    if (ent.mesh != kind) continue;
                    auto* lt = scene.local(ent.handle);
                    if (lt == nullptr) continue;
                    if (!bound)
                    {
                        cmd.bind_vertex_buffer(0, vb, 0);
                        cmd.bind_index_buffer(ib, 0, cd::rhi::IndexType::kUInt16);
                        bound = true;
                    }
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
                    cmd.draw_indexed(index_count, 1, 0, 0, 0);
                }
            };
            draw_kind(SceneEntity::MeshKind::kCube,
                      cube_vb, cube_ib,
                      static_cast<std::uint32_t>(kCubeIndices.size()));
            draw_kind(SceneEntity::MeshKind::kSphere,
                      sphere_vb, sphere_ib, sphere_index_count);
            draw_kind(SceneEntity::MeshKind::kCone,
                      cone_vb, cone_ib, cone_index_count);
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
        // Inspector widened from 380 → 480 so the 3-field InputFloat3
        // widgets keep their right-side label visible. At 380 the X / Y /
        // Z mini-boxes ran into the trailing "translate delta" label and
        // the Apply buttons wrapped onto the next line — user-reported
        // as "şekille isimler karışıyor".
        const float inspector_w = 480.0F;
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
                // Phase 121: serialize entity name + tint as extra
                // per-node JSON fields so the load path can restore
                // the actual entity identity (not just transforms).
                const auto json = cd::scene::serialize_scene_with(scene,
                    [&](cd::ecs::Entity e, cd::asset::json::Object& obj) {
                        for (const auto& se : entities)
                        {
                            if (se.handle.id != e.id) continue;
                            obj["name"] = cd::asset::json::Value { se.name };
                            cd::asset::json::Array tint;
                            tint.push_back(cd::asset::json::Value { static_cast<double>(se.tint.x) });
                            tint.push_back(cd::asset::json::Value { static_cast<double>(se.tint.y) });
                            tint.push_back(cd::asset::json::Value { static_cast<double>(se.tint.z) });
                            obj["tint"] = cd::asset::json::Value { std::move(tint) };
                            obj["mesh"] = cd::asset::json::Value {
                                (se.mesh == SceneEntity::MeshKind::kCube)   ? std::string{"Cube"} :
                                (se.mesh == SceneEntity::MeshKind::kSphere) ? std::string{"Sphere"} :
                                                                              std::string{"Cone"} };
                            break;
                        }
                    });
                const auto text = cd::asset::json::serialize(json, true);
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
                    auto json_r = cd::asset::json::parse(text);
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
                            // Phase 121: real name + tint round-trip via
                            // `deserialize_scene_with` callback. The
                            // older fallback (cycling Cube/Sphere/Cone
                            // by position) stays in place for legacy
                            // saves that lack name/tint/mesh keys.
                            static const std::array<const char*, 3> kSeedNames {
                                "Cube", "Sphere", "Cone"
                            };
                            static const std::array<cd::math::Vec3f, 3> kSeedTints {{
                                { 1.0F, 0.4F, 0.4F },
                                { 0.4F, 1.0F, 0.4F },
                                { 0.4F, 0.4F, 1.0F },
                            }};
                            std::size_t loaded_count = 0;
                            // First clear any partial state left by the
                            // earlier deserialize_scene call.
                            entities.clear();
                            world = cd::ecs::World {};
                            scene = cd::scene::Scene { world };
                            auto map_again = cd::scene::deserialize_scene_with(
                                scene, *json_r,
                                [&](cd::ecs::Entity e, const cd::asset::json::Object& node)
                                {
                                    SceneEntity se;
                                    se.handle = e;
                                    if (auto it = node.find("name");
                                        it != node.end() && it->second.is_string())
                                    {
                                        se.name = it->second.as_string();
                                    }
                                    else if (loaded_count < kSeedNames.size())
                                    {
                                        se.name = kSeedNames[loaded_count];
                                    }
                                    else
                                    {
                                        se.name = "n_" + std::to_string(loaded_count);
                                    }
                                    if (auto it = node.find("tint");
                                        it != node.end() && it->second.is_array() &&
                                        it->second.as_array().size() >= 3)
                                    {
                                        const auto& a = it->second.as_array();
                                        se.tint = {
                                            static_cast<float>(a[0].as_number()),
                                            static_cast<float>(a[1].as_number()),
                                            static_cast<float>(a[2].as_number())
                                        };
                                    }
                                    else if (loaded_count < kSeedTints.size())
                                    {
                                        se.tint = kSeedTints[loaded_count];
                                    }
                                    else
                                    {
                                        se.tint = { 0.7F, 0.7F, 0.7F };
                                    }
                                    if (auto it = node.find("mesh");
                                        it != node.end() && it->second.is_string())
                                    {
                                        se.mesh = mesh_kind_from_name(it->second.as_string());
                                    }
                                    else
                                    {
                                        se.mesh = mesh_kind_from_name(se.name);
                                    }
                                    entities.push_back(std::move(se));
                                    ++loaded_count;
                                });
                            (void)map_again;  // already consumed in callback
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
                // Reserve generous right-hand padding so DragFloat3 X/Y/Z
                // mini-fields never bleed into the label column.
                ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x * 0.62F);

                ImGui::Text("Entity: %s", ent.name.c_str());

                // Drag widgets edit the scene transform LIVE. Pre-drag
                // values are snapshotted on `IsItemActivated()` so we can
                // synthesize a single delta-command for EditHistory when
                // the user releases the mouse (`IsItemDeactivatedAfterEdit`).
                // This way undo/redo still works without spamming the
                // history with per-pixel sub-commands.

                // ---- Position ---------------------------------------------
                ImGui::SeparatorText("Position");
                {
                    static cd::math::Vec3f pre_drag {};
                    float xyz[3] {
                        lt->value.position.x,
                        lt->value.position.y,
                        lt->value.position.z };
                    ImGui::Text("X Y Z");
                    ImGui::SameLine();
                    const bool changed = ImGui::DragFloat3(
                        "##pos", xyz, 0.05F, -10.0F, 10.0F, "%.3f");
                    if (ImGui::IsItemActivated())
                        pre_drag = lt->value.position;
                    if (changed)
                        lt->value.position = { xyz[0], xyz[1], xyz[2] };
                    if (ImGui::IsItemDeactivatedAfterEdit())
                    {
                        const cd::math::Vec3f delta {
                            lt->value.position.x - pre_drag.x,
                            lt->value.position.y - pre_drag.y,
                            lt->value.position.z - pre_drag.z };
                        if (delta.x != 0.0F || delta.y != 0.0F || delta.z != 0.0F)
                        {
                            // Restore pre-drag value, then let the command
                            // re-apply the delta through the history path
                            // so undo lands cleanly.
                            lt->value.position = pre_drag;
                            history.push(std::make_unique<cd::editor::TranslateCommand>(
                                scene, ent.handle, delta));
                            log_push("drag-end: TranslateCommand");
                        }
                    }
                }

                // ---- Scale ------------------------------------------------
                ImGui::SeparatorText("Scale");
                {
                    static cd::math::Vec3f pre_drag { 1.0F, 1.0F, 1.0F };
                    float xyz[3] {
                        lt->value.scale.x,
                        lt->value.scale.y,
                        lt->value.scale.z };
                    ImGui::Text("X Y Z");
                    ImGui::SameLine();
                    const bool changed = ImGui::DragFloat3(
                        "##scale", xyz, 0.02F, 0.05F, 5.0F, "%.3f");
                    if (ImGui::IsItemActivated())
                        pre_drag = lt->value.scale;
                    if (changed)
                        lt->value.scale = { xyz[0], xyz[1], xyz[2] };
                    if (ImGui::IsItemDeactivatedAfterEdit())
                    {
                        // ScaleCommand multiplies the current scale by the
                        // factor — derive the per-axis ratio so the
                        // command lands the user's drag exactly.
                        const cd::math::Vec3f factor {
                            (pre_drag.x != 0.0F) ? (lt->value.scale.x / pre_drag.x) : 1.0F,
                            (pre_drag.y != 0.0F) ? (lt->value.scale.y / pre_drag.y) : 1.0F,
                            (pre_drag.z != 0.0F) ? (lt->value.scale.z / pre_drag.z) : 1.0F };
                        if (factor.x != 1.0F || factor.y != 1.0F || factor.z != 1.0F)
                        {
                            lt->value.scale = pre_drag;
                            history.push(std::make_unique<cd::editor::ScaleCommand>(
                                scene, ent.handle, factor));
                            log_push("drag-end: ScaleCommand");
                        }
                    }
                }

                // ---- Rotation ---------------------------------------------
                // Edit absolute euler angles (degrees). Re-derived from
                // the quaternion every frame the widget is NOT active so
                // external changes (Undo, scene reload) sync into the
                // display; while the widget IS active we hold the
                // editor's own euler state to dodge gimbal-induced
                // round-trip jitter.
                ImGui::SeparatorText("Rotation (Euler, deg)");
                {
                    constexpr float kRad2Deg = 180.0F / 3.14159265358979F;
                    constexpr float kDeg2Rad = 3.14159265358979F / 180.0F;
                    static cd::math::Quatf pre_drag { 0.0F, 0.0F, 0.0F, 1.0F };
                    static float          editor_euler_deg[3] { 0.0F, 0.0F, 0.0F };
                    static bool           editing = false;

                    auto quat_to_euler_zyx = [](const cd::math::Quatf& q) {
                        // Convention matches the old Apply Euler block
                        // (q = qz * qy * qx); inverse derivation below.
                        const float sx = 2.0F * (q.w * q.x + q.y * q.z);
                        const float cx = 1.0F - 2.0F * (q.x * q.x + q.y * q.y);
                        const float roll = std::atan2(sx, cx);
                        float sy = 2.0F * (q.w * q.y - q.z * q.x);
                        if (sy >  1.0F) sy =  1.0F;
                        if (sy < -1.0F) sy = -1.0F;
                        const float pitch = std::asin(sy);
                        const float sz = 2.0F * (q.w * q.z + q.x * q.y);
                        const float cz = 1.0F - 2.0F * (q.y * q.y + q.z * q.z);
                        const float yaw = std::atan2(sz, cz);
                        return std::array<float, 3> { roll, pitch, yaw };
                    };
                    auto euler_to_quat_zyx = [](float ex, float ey, float ez) {
                        const float hx = ex * 0.5F;
                        const float hy = ey * 0.5F;
                        const float hz = ez * 0.5F;
                        const float cx = std::cos(hx), sx = std::sin(hx);
                        const float cy = std::cos(hy), sy = std::sin(hy);
                        const float cz = std::cos(hz), sz = std::sin(hz);
                        cd::math::Quatf q;
                        q.w = cz * cy * cx + sz * sy * sx;
                        q.x = cz * cy * sx - sz * sy * cx;
                        q.y = cz * sy * cx + sz * cy * sx;
                        q.z = sz * cy * cx - cz * sy * sx;
                        return q;
                    };

                    if (!editing)
                    {
                        const auto e = quat_to_euler_zyx(lt->value.rotation);
                        editor_euler_deg[0] = e[0] * kRad2Deg;
                        editor_euler_deg[1] = e[1] * kRad2Deg;
                        editor_euler_deg[2] = e[2] * kRad2Deg;
                    }

                    ImGui::Text("X Y Z");
                    ImGui::SameLine();
                    const bool changed = ImGui::DragFloat3(
                        "##rot", editor_euler_deg, 1.0F, -180.0F, 180.0F, "%.1f");

                    if (ImGui::IsItemActivated())
                    {
                        pre_drag = lt->value.rotation;
                        editing = true;
                    }
                    if (changed && editing)
                    {
                        lt->value.rotation = euler_to_quat_zyx(
                            editor_euler_deg[0] * kDeg2Rad,
                            editor_euler_deg[1] * kDeg2Rad,
                            editor_euler_deg[2] * kDeg2Rad);
                    }
                    if (ImGui::IsItemDeactivatedAfterEdit())
                    {
                        // Restore pre-drag rotation, push absolute new
                        // quaternion via RotateCommand for clean undo.
                        const cd::math::Quatf final_rot = lt->value.rotation;
                        lt->value.rotation = pre_drag;
                        history.push(std::make_unique<cd::editor::RotateCommand>(
                            scene, ent.handle, final_rot));
                        log_push("drag-end: RotateCommand");
                        editing = false;
                    }
                }

                ImGui::PopItemWidth();
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

        // ---- Command palette (Phase 111) ----------------------------------
        // VS Code-style fuzzy-search popup pinned to the top centre of
        // the viewport. Opens with Ctrl+Shift+P; closes with Esc, click-
        // away, or running a command. Up/Down arrow keys move the
        // active hit (currently just the first match); Enter invokes.
        if (palette_visible)
        {
            const float palette_w = 520.0F;
            const float palette_h = 320.0F;
            ImGui::SetNextWindowPos(
                ImVec2 { (vw - palette_w) * 0.5F, 80.0F },
                ImGuiCond_Always);
            ImGui::SetNextWindowSize(
                ImVec2 { palette_w, palette_h },
                ImGuiCond_Always);
            const ImGuiWindowFlags pf =
                ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
                ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking;
            if (ImGui::Begin("Command Palette", &palette_visible, pf))
            {
                // Auto-focus the InputText every frame the palette opens.
                if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
                char buf[128] {};
                std::snprintf(buf, sizeof(buf), "%s", palette_query.c_str());
                if (ImGui::InputText("##palette_query", buf, sizeof(buf)))
                    palette_query = buf;
                ImGui::Separator();
                const auto hits = palette.filter(palette_query);
                if (hits.empty())
                {
                    ImGui::TextDisabled("no match (%zu commands registered)",
                                        palette.size());
                }
                else
                {
                    for (std::size_t i = 0; i < hits.size() && i < 16; ++i)
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
                // Enter on first match — fast-path keyboarders.
                if (ImGui::IsKeyPressed(ImGuiKey_Enter, false) && !hits.empty())
                {
                    (void)palette.invoke(hits.front());
                    palette_visible = false;
                    palette_query.clear();
                }
            }
            ImGui::End();
        }

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
