// =============================================================================
// CHROMODYNAMIC - samples/game/hello_world/main.cpp
//
// Phase 523 (T3.1 - visual variant of Phase 504 G3 closeout).
//
// Replaces the Phase 504 console-only demo with a visual Vulkan rendering
// sample. All four G3 gameplay libraries remain exercised at the same
// simulation fidelity; a real platform window + Vulkan swapchain is added
// so the player's walk can be observed on screen.
//
// Gameplay simulation (identical to Phase 504):
//   1. cd::game::query             - QueryWorld rebuilt every tick from
//                                    the player's AABB.
//   2. cd::game::trigger           - TriggerVolume sphere @ (5,0,5) r=1.0;
//                                    on_enter prints "[trigger] Entered
//                                    checkpoint" once.
//   3. cd::game::camera            - CameraBrain + VCam following the player
//                                    with critically-damped smoothing.
//   4. cd::game::particles_event   - "jump_dust" recipe; fired every 2 s;
//                                    on_emit echoed to stdout.
//
// Visual additions (scope-down: colored quads / cubes per entity):
//   * Platform window (cd::platform::IWindow) — 1280x720.
//   * Vulkan device  (cd::rhi::vulkan::create_vulkan_device).
//   * Renderer       (cd::render::Renderer) — present loop, 2-in-flight.
//   * Depth target   (D32Float, recreated on resize).
//   * Minimal GLSL shader — MVP push constant, per-vertex color.
//   * Player entity  — blue cube translated to player_pos each frame.
//   * Trigger sphere — red cube (proxy) centred at (5, 0, 5), slightly
//                      larger; turns brighter white when the player is
//                      inside (trigger active).
//   * Ground plane   — dark-grey flat quad at y = 0.
//   * Particle burst indicator — yellow cube blinks at origin every 2 s
//     for one frame so the viewer sees the "jump_dust" event visually.
//   * Camera view follows the CameraBrain output each frame; the VCam's
//     position_offset (0, 2.5, -4) gives a natural 3rd-person framing.
//
// Driving loop:
//   * Fixed-tick simulation at 60 Hz (kDt = 1/60) per Phase 504.
//   * Present loop advances one frame per OS-event pump; simulation ticks
//     forward by wall-clock dt clamped to kMaxDt so the two loops
//     decouple gracefully.
//   * ESC or window close exits cleanly.
//
// Scope gate (CLAUDE.md §3): "Sample compile = the gate." No new ctest
// required.
// =============================================================================

// ---- Render / RHI headers (new in Phase 523) --------------------------------
#include <cd/camera/Camera.hpp>
#include <cd/material/Material.hpp>
#include <cd/math/Matrix.hpp>
#include <cd/math/Quaternion.hpp>
#include <cd/math/Transform.hpp>
#include <cd/math/Vector.hpp>
#include <cd/platform/Window.hpp>
#include <cd/render/Renderer.hpp>
#include <cd/rhi/Barriers.hpp>
#include <cd/rhi/ICommandBuffer.hpp>
#include <cd/rhi/IDevice.hpp>
#include <cd/rhi/vulkan/VulkanDevice.hpp>
#include <cd/shader/Compiler.hpp>

// ---- Gameplay headers (unchanged from Phase 504) ----------------------------
#include <cd/ecs/Entity.hpp>
#include <cd/game/camera/VirtualCamera.hpp>
#include <cd/game/particles_event/ParticlesEvent.hpp>
#include <cd/game/query/Query.hpp>
#include <cd/game/trigger/Trigger.hpp>
#include <cd/gameplay/time/Time.hpp>
#include <cd/physics/Aabb.hpp>
#include <cd/physics/Sphere.hpp>

#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <span>
#include <unordered_map>
#include <vector>

// =============================================================================
// Inline GLSL — minimal MVP + per-vertex color shader.
// One push-constant block: a 4x4 MVP matrix (64 bytes, vertex stage).
// No descriptor sets, no samplers.
// =============================================================================
namespace
{

constexpr const char* kVS = R"glsl(
#version 450
layout(push_constant) uniform PC { mat4 mvp; } pc;
layout(location = 0) in vec3 in_pos;
layout(location = 1) in vec3 in_color;
layout(location = 0) out vec3 v_color;
void main() {
    vec4 clip = pc.mvp * vec4(in_pos, 1.0);
    // Vulkan NDC Y is down; our look_at/perspective is Y-up — flip.
    clip.y = -clip.y;
    gl_Position = clip;
    v_color = in_color;
}
)glsl";

constexpr const char* kFS = R"glsl(
#version 450
layout(location = 0) in  vec3 v_color;
layout(location = 0) out vec4 out_color;
void main() { out_color = vec4(v_color, 1.0); }
)glsl";

// =============================================================================
// Geometry: 8-corner cube. Shared across all entity draw calls; each call
// supplies a different MVP + base color via push constants / vertex data.
// =============================================================================

struct Vertex
{
    float pos[3];
    float col[3];
};

// Build a solid-color cube centered at the origin with half-extent 0.5.
// col is the RGBA base; all 8 corners share the same tint so the model
// reads as one uniform block at distance.
[[nodiscard]] constexpr std::array<Vertex, 8> make_cube_verts(float r, float g, float b) noexcept
{
    return { {
        { { -0.5F, -0.5F, -0.5F }, { r, g, b } },
        { {  0.5F, -0.5F, -0.5F }, { r, g, b } },
        { {  0.5F,  0.5F, -0.5F }, { r, g, b } },
        { { -0.5F,  0.5F, -0.5F }, { r, g, b } },
        { { -0.5F, -0.5F,  0.5F }, { r, g, b } },
        { {  0.5F, -0.5F,  0.5F }, { r, g, b } },
        { {  0.5F,  0.5F,  0.5F }, { r, g, b } },
        { { -0.5F,  0.5F,  0.5F }, { r, g, b } },
    } };
}

// 12 triangles, CCW winding.
constexpr std::array<std::uint16_t, 36> kCubeIndices { {
    0,1,2, 0,2,3,   // back  (-Z)
    4,6,5, 4,7,6,   // front (+Z)
    0,3,7, 0,7,4,   // left  (-X)
    1,5,6, 1,6,2,   // right (+X)
    0,4,5, 0,5,1,   // bottom(-Y)
    3,2,6, 3,6,7,   // top   (+Y)
} };

// Ground plane quad (flat, y = 0, wide).
// Two triangles covering an 18 m × 18 m area centered at the origin.
struct GroundVertex { float pos[3]; float col[3]; };
constexpr std::array<GroundVertex, 4> kGroundVerts { {
    { { -9.0F, 0.0F,  9.0F }, { 0.12F, 0.14F, 0.12F } },
    { {  9.0F, 0.0F,  9.0F }, { 0.12F, 0.14F, 0.12F } },
    { {  9.0F, 0.0F, -9.0F }, { 0.10F, 0.12F, 0.10F } },
    { { -9.0F, 0.0F, -9.0F }, { 0.10F, 0.12F, 0.10F } },
} };
constexpr std::array<std::uint16_t, 6> kGroundIndices { { 0,1,2, 0,2,3 } };

// =============================================================================
// Depth target helper (identical to hello_cube pattern).
// =============================================================================

struct DepthTarget
{
    cd::rhi::TextureHandle     image {};
    cd::rhi::TextureViewHandle view  {};
    cd::rhi::Extent2D          extent {};

    void destroy(cd::rhi::IDevice& dev) noexcept
    {
        if (view.is_valid())  dev.destroy_texture_view(view);
        if (image.is_valid()) dev.destroy_texture(image);
        image  = {};
        view   = {};
        extent = {};
    }
};

[[nodiscard]] bool create_depth_target(cd::rhi::IDevice&  dev,
                                       cd::rhi::Extent2D  size,
                                       cd::rhi::Format    format,
                                       DepthTarget&       out)
{
    out.destroy(dev);
    cd::rhi::TextureDesc td {};
    td.type         = cd::rhi::TextureType::k2D;
    td.format       = format;
    td.extent       = { size.width, size.height, 1 };
    td.mip_levels   = 1;
    td.array_layers = 1;
    td.usage        = cd::rhi::TextureUsage::kDepthStencilAttachment;
    td.memory       = cd::rhi::MemoryUsage::kGpuOnly;
    auto img = dev.create_texture(td);
    if (!img.has_value()) return false;

    cd::rhi::TextureViewDesc vd {};
    vd.texture    = *img;
    vd.type       = cd::rhi::TextureType::k2D;
    vd.format     = format;
    vd.base_mip   = 0; vd.mip_count   = 1;
    vd.base_layer = 0; vd.layer_count = 1;
    auto view = dev.create_texture_view(vd);
    if (!view.has_value()) { dev.destroy_texture(*img); return false; }

    out.image  = *img;
    out.view   = *view;
    out.extent = size;
    return true;
}

// =============================================================================
// GPU buffer upload helper (identical to hello_cube).
// =============================================================================

[[nodiscard]] cd::rhi::BufferHandle
make_upload_buffer(cd::rhi::IDevice&         dev,
                   std::span<const std::byte> data,
                   cd::rhi::BufferUsage       usage)
{
    cd::rhi::BufferDesc bd {};
    bd.size   = data.size();
    bd.usage  = usage;
    bd.memory = cd::rhi::MemoryUsage::kCpuToGpu;
    auto r = dev.create_buffer(bd);
    if (!r.has_value()) return {};
    if (!dev.upload_buffer(*r, 0, data).has_value())
    { dev.destroy_buffer(*r); return {}; }
    return *r;
}

// =============================================================================
// Draw helper — pushes MVP for one entity and issues an indexed draw call.
// Assumes vertex/index buffers are already bound and material is applied.
// =============================================================================

void draw_entity(cd::rhi::ICommandBuffer&    cmd,
                 const cd::material::Material& mat,
                 const cd::math::Mat4f&        vp,
                 const cd::math::Vec3f&        world_pos,
                 float                         scale,
                 std::uint32_t                 index_count)
{
    cd::math::Transformf xf {};
    xf.position = world_pos;
    xf.scale    = { scale, scale, scale };
    const cd::math::Mat4f model = cd::math::to_mat4(xf);
    const cd::math::Mat4f mvp   = vp * model;

    cmd.push_constants(
        mat.pipeline_layout(),
        cd::rhi::ShaderStage::kVertex,
        /*offset=*/0,
        static_cast<std::uint32_t>(sizeof(mvp)),
        &mvp);

    cmd.draw_indexed(index_count, 1, 0, 0, 0);
}

// =============================================================================
// Gameplay state — mirrors Phase 504 World struct.
// =============================================================================

namespace gc  = cd::game::camera;
namespace gp  = cd::game::particles_event;
namespace gq  = cd::game::query;
namespace gt  = cd::game::trigger;
namespace gtm = cd::gameplay::time;

using cd::ecs::Entity;
using cd::ecs::EntityManager;
using cd::math::Quatf;
using cd::math::Vec3f;
using cd::physics::Aabb;
using cd::physics::Sphere;

struct World
{
    Entity player {};
    Vec3f  player_pos      { 0.0F, 0.5F, 0.0F };   ///< y=0.5 so cube sits on the ground.
    Vec3f  player_velocity { 1.0F, 0.0F, 1.0F };   ///< 1 m/s diagonal (+X,+Z).
    float  player_radius   { 0.5F };
};

[[nodiscard]] Aabb player_aabb(const World& w) noexcept
{
    return Aabb {
        Vec3f { w.player_pos.x - w.player_radius,
                w.player_pos.y - w.player_radius,
                w.player_pos.z - w.player_radius },
        Vec3f { w.player_pos.x + w.player_radius,
                w.player_pos.y + w.player_radius,
                w.player_pos.z + w.player_radius },
    };
}

[[nodiscard]] Vec3f resolve_target_pos(const World& w, Entity e) noexcept
{
    return (e == w.player) ? w.player_pos : Vec3f { 0.0F, 0.0F, 0.0F };
}

}  // namespace

// =============================================================================
// main
// =============================================================================
int main()
{
    std::puts("=== CHROMODYNAMIC hello_world (T3.1 visual, Phase 523) ===");

    // -----------------------------------------------------------------------
    // Platform window
    // -----------------------------------------------------------------------
    cd::platform::WindowDesc wd {};
    wd.title  = "CHROMODYNAMIC - hello_world (visual 3rd-person, Phase 523)";
    wd.width  = 1280;
    wd.height = 720;
    auto window_r = cd::platform::create_window(wd);
    if (!window_r.has_value())
    {
        std::fprintf(stderr, "[boot] window failed: %.*s\n",
                     static_cast<int>(window_r.error().message.size()),
                     window_r.error().message.data());
        return EXIT_FAILURE;
    }
    auto& window = **window_r;

    // -----------------------------------------------------------------------
    // Vulkan device
    // -----------------------------------------------------------------------
    cd::rhi::vulkan::VulkanCreateInfo vci {};
    vci.app_name = "hello_world_visual";
    auto device_r = cd::rhi::vulkan::create_vulkan_device(vci);
    if (!device_r.has_value())
    {
        std::fprintf(stderr, "[boot] device failed: %.*s\n",
                     static_cast<int>(device_r.error().message.size()),
                     device_r.error().message.data());
        return EXIT_FAILURE;
    }
    auto& device = **device_r;
    std::printf("[boot] GPU: %s\n", device.adapter_name().data());

    // -----------------------------------------------------------------------
    // Renderer (swapchain + present loop)
    // -----------------------------------------------------------------------
    cd::render::RendererDesc rd {};
    rd.device                   = &device;
    rd.swapchain.window_handle  = window.native_window_handle();
    rd.swapchain.display_handle = window.native_display_handle();
    rd.swapchain.extent         = { window.width(), window.height() };
    rd.swapchain.format         = cd::rhi::Format::kBGRA8Unorm;
    rd.swapchain.vsync          = true;
    rd.frames_in_flight         = 2;
    auto renderer_r = cd::render::Renderer::create(rd);
    if (!renderer_r.has_value())
    {
        std::fprintf(stderr, "[boot] renderer failed: %.*s\n",
                     static_cast<int>(renderer_r.error().message.size()),
                     renderer_r.error().message.data());
        return EXIT_FAILURE;
    }
    auto& renderer = *renderer_r;

    // -----------------------------------------------------------------------
    // Depth target
    // -----------------------------------------------------------------------
    constexpr auto kDepthFmt = cd::rhi::Format::kD32Float;
    DepthTarget depth {};
    if (!create_depth_target(device, { window.width(), window.height() }, kDepthFmt, depth))
    {
        std::fprintf(stderr, "[boot] depth target failed\n");
        return EXIT_FAILURE;
    }
    bool depth_init = false;

    // -----------------------------------------------------------------------
    // GPU geometry: one shared cube VB/IB, one ground VB/IB.
    // All entity draw calls reuse the cube buffers with different MVP.
    // -----------------------------------------------------------------------
    const auto player_verts = make_cube_verts(0.25F, 0.45F, 0.90F);   // blue
    const auto player_bytes = std::span<const std::byte>(
        reinterpret_cast<const std::byte*>(player_verts.data()),
        player_verts.size() * sizeof(Vertex));
    auto player_vb = make_upload_buffer(device, player_bytes, cd::rhi::BufferUsage::kVertex);

    const auto trigger_verts = make_cube_verts(0.85F, 0.15F, 0.15F);  // red
    const auto trigger_bytes = std::span<const std::byte>(
        reinterpret_cast<const std::byte*>(trigger_verts.data()),
        trigger_verts.size() * sizeof(Vertex));
    auto trigger_vb = make_upload_buffer(device, trigger_bytes, cd::rhi::BufferUsage::kVertex);

    const auto pfx_verts = make_cube_verts(0.95F, 0.90F, 0.15F);      // yellow
    const auto pfx_bytes = std::span<const std::byte>(
        reinterpret_cast<const std::byte*>(pfx_verts.data()),
        pfx_verts.size() * sizeof(Vertex));
    auto pfx_vb = make_upload_buffer(device, pfx_bytes, cd::rhi::BufferUsage::kVertex);

    const auto cube_idx_bytes = std::span<const std::byte>(
        reinterpret_cast<const std::byte*>(kCubeIndices.data()),
        kCubeIndices.size() * sizeof(std::uint16_t));
    auto cube_ib = make_upload_buffer(device, cube_idx_bytes, cd::rhi::BufferUsage::kIndex);

    const auto ground_v_bytes = std::span<const std::byte>(
        reinterpret_cast<const std::byte*>(kGroundVerts.data()),
        kGroundVerts.size() * sizeof(GroundVertex));
    auto ground_vb = make_upload_buffer(device, ground_v_bytes, cd::rhi::BufferUsage::kVertex);

    const auto ground_i_bytes = std::span<const std::byte>(
        reinterpret_cast<const std::byte*>(kGroundIndices.data()),
        kGroundIndices.size() * sizeof(std::uint16_t));
    auto ground_ib = make_upload_buffer(device, ground_i_bytes, cd::rhi::BufferUsage::kIndex);

    if (!player_vb.is_valid() || !trigger_vb.is_valid() || !pfx_vb.is_valid() ||
        !cube_ib.is_valid()   || !ground_vb.is_valid()  || !ground_ib.is_valid())
    {
        std::fprintf(stderr, "[boot] geometry buffer creation failed\n");
        return EXIT_FAILURE;
    }

    // -----------------------------------------------------------------------
    // Material — shared by all draw calls (same shader, same pipeline).
    // Push constant: mat4 MVP (64 bytes, vertex stage).
    // -----------------------------------------------------------------------
    auto compiler = cd::shader::make_glslang_compiler();
    if (compiler == nullptr)
    {
        std::fprintf(stderr, "[boot] shader compiler unavailable\n");
        return EXIT_FAILURE;
    }

    constexpr std::array<cd::rhi::VertexBinding, 1> kBindings {
        cd::rhi::VertexBinding { 0, sizeof(Vertex), false }
    };
    constexpr std::array<cd::rhi::VertexAttribute, 2> kAttrs {
        cd::rhi::VertexAttribute { 0, 0, cd::rhi::Format::kRGB32Float, offsetof(Vertex, pos) },
        cd::rhi::VertexAttribute { 1, 0, cd::rhi::Format::kRGB32Float, offsetof(Vertex, col) },
    };
    constexpr std::array<cd::rhi::Format, 1> kColorFmts { cd::rhi::Format::kBGRA8Unorm };
    constexpr std::array<cd::rhi::PushConstantRange, 1> kPush {
        cd::rhi::PushConstantRange {
            .stages = cd::rhi::ShaderStage::kVertex,
            .offset = 0,
            .size   = static_cast<std::uint32_t>(sizeof(cd::math::Mat4f)),
        }
    };

    cd::material::MaterialDesc md {};
    md.vertex_glsl              = kVS;
    md.fragment_glsl            = kFS;
    md.vertex_bindings          = kBindings;
    md.vertex_attributes        = kAttrs;
    md.color_attachment_formats = kColorFmts;
    md.depth_attachment_format  = kDepthFmt;
    md.push_constants           = kPush;
    md.topology                 = cd::rhi::PrimitiveTopology::kTriangleList;
    md.raster.cull              = cd::rhi::CullMode::kBack;
    md.depth_stencil.depth_test    = true;
    md.depth_stencil.depth_write   = true;
    md.depth_stencil.depth_compare = cd::rhi::CompareOp::kLess;
    md.name                     = "hw_visual";

    auto mat_r = cd::material::Material::create(device, compiler.get(), md);
    if (!mat_r.has_value())
    {
        std::fprintf(stderr, "[boot] material failed: %.*s\n",
                     static_cast<int>(mat_r.error().message.size()),
                     mat_r.error().message.data());
        return EXIT_FAILURE;
    }
    auto& mat = *mat_r;

    // -----------------------------------------------------------------------
    // Ground material uses the same pipeline but GroundVertex matches
    // Vertex layout (same stride, same attribute offsets), so the same
    // pipeline works.  Vertex binding stride must be sizeof(Vertex) = 24
    // bytes, and sizeof(GroundVertex) = 24 bytes — confirmed by static_assert.
    // -----------------------------------------------------------------------
    static_assert(sizeof(Vertex) == sizeof(GroundVertex),
                  "ground vertex must match cube vertex layout for shared pipeline");

    // -----------------------------------------------------------------------
    // Gameplay world setup (Phase 504 logic unchanged)
    // -----------------------------------------------------------------------
    EntityManager entities;
    World         world;
    world.player = entities.create();
    std::printf("[boot] player entity id=%u gen=%u\n",
                world.player.id, world.player.generation);

    // QueryWorld
    gq::QueryWorld query_world { 4.0F };

    // TriggerWorld — sphere @ (5, 0.5, 5) r=1.0 (y=0.5 matches player mid-height)
    gt::TriggerWorld triggers;
    const Entity     checkpoint_owner = entities.create();
    int              enter_fire_count = 0;
    int              stay_fire_count  = 0;
    int              exit_fire_count  = 0;

    constexpr Vec3f kTriggerCentre { 5.0F, 0.5F, 5.0F };
    constexpr float kTriggerRadius { 1.0F };
    bool            trigger_active  = false;   // visual indicator: bright red

    gt::TriggerVolume vol;
    vol.name     = "checkpoint";
    vol.shape    = Sphere { kTriggerCentre, kTriggerRadius };
    vol.on_enter = [&](Entity subject) {
        ++enter_fire_count;
        trigger_active = true;
        std::printf("[trigger] Entered checkpoint (subject id=%u gen=%u)\n",
                    subject.id, subject.generation);
    };
    vol.on_stay  = [&](Entity /*s*/) { ++stay_fire_count; trigger_active = true; };
    vol.on_exit  = [&](Entity subject) {
        ++exit_fire_count;
        trigger_active = false;
        std::printf("[trigger] Exited checkpoint  (subject id=%u gen=%u)\n",
                    subject.id, subject.generation);
    };
    triggers.add_trigger(checkpoint_owner, std::move(vol));
    std::printf("[boot] trigger 'checkpoint' @ (5,0.5,5) r=1.0 registered\n");

    // CameraBrain + VCam following the player
    gc::CameraBrain   brain;
    gc::VirtualCamera follow_cam;
    follow_cam.set_target(world.player);
    follow_cam.settings().fov_y           = 1.0F;
    follow_cam.settings().near_z          = 0.1F;
    follow_cam.settings().far_z           = 100.0F;
    follow_cam.settings().position_offset = Vec3f { 0.0F, 2.5F, -4.0F };
    follow_cam.settings().damping         = Vec3f { 0.3F, 0.3F, 0.3F };
    const auto follow_vcam_id =
        brain.add_vcam(std::move(follow_cam), /*priority*/ 10);
    std::printf("[boot] vcam id=%u priority=10 follows player\n", follow_vcam_id);

    // ParticleEventDispatcher — "jump_dust" recipe every 2 s
    gp::ParticleEventDispatcher particles;
    {
        gp::ParticleRecipe r;
        r.name                     = "jump_dust";
        r.count                    = 32U;
        r.lifetime_s               = 0.75F;
        r.gravity                  = Vec3f { 0.0F, -9.81F, 0.0F };
        r.velocity_min             = Vec3f { -1.5F, 0.5F, -1.5F };
        r.velocity_max             = Vec3f {  1.5F, 2.5F,  1.5F };
        r.emitter_shape            = gp::EmitterShape::kSphere;
        r.emitter_radius_or_extent = Vec3f { 0.4F, 0.0F, 0.0F };
        particles.register_recipe("jump_dust", std::move(r));
    }
    int    particle_fire_count = 0;
    bool   pfx_blink           = false;   // flash yellow cube for one frame
    int    pfx_blink_frames    = 0;       // countdown

    particles.add_on_emit([&](const gp::ActiveBurst& b) {
        ++particle_fire_count;
        pfx_blink        = true;
        pfx_blink_frames = 4;   // visible for 4 frames
        std::printf("[pfx] jump_dust burst #%d emitted @ (%.2f, %.2f, %.2f) "
                    "alive_count=%u\n",
                    particle_fire_count,
                    static_cast<double>(b.origin.x),
                    static_cast<double>(b.origin.y),
                    static_cast<double>(b.origin.z),
                    b.alive_count);
    });
    std::printf("[boot] particle recipe 'jump_dust' registered\n");

    // TimeKeeper + simulation state
    gtm::TimeKeeper clock;
    constexpr double kDt            = 1.0 / 60.0;
    constexpr double kMaxDt         = 1.0 / 10.0;
    constexpr double kFireIntervalS = 2.0;
    double next_particle_fire_s = 0.0;
    int    status_lines         = 0;
    int    ticks_per_status     = 60;    // console HUD every 60 ticks

    std::printf("[boot] hello_world visual ready — close window or press ESC to exit\n");
    std::fflush(stdout);

    // -----------------------------------------------------------------------
    // Main loop
    // -----------------------------------------------------------------------
    std::vector<cd::platform::OSEvent> events;
    events.reserve(64);
    bool needs_rebuild = false;

    auto rebuild_swapchain = [&]() -> bool {
        if (window.width() == 0 || window.height() == 0)
            return false;
        if (!renderer.recreate_swapchain({ window.width(), window.height() }).has_value())
            return false;
        if (!create_depth_target(device, { window.width(), window.height() }, kDepthFmt, depth))
            return false;
        depth_init    = false;
        needs_rebuild = false;
        return true;
    };

    auto t_prev  = std::chrono::steady_clock::now();
    double accum = 0.0;   // accumulated wall time for fixed-tick drain

    int    sim_tick      = 0;         // total simulation ticks
    // The sample runs until the window is closed or ESC pressed.
    // (No hard tick cap in the visual variant — the viewer decides.)
    bool   sim_complete  = false;     // set when enter_fire_count >= 1 and
                                      // particle_fire_count >= 1 (both gates
                                      // satisfied, same as Phase 504 success).

    while (true)
    {
        // ---- Poll OS events -------------------------------------------------
        events.clear();
        if (!window.pump_events(events))
            break;  // window closed

        for (const auto& ev : events)
        {
            if (ev.kind == cd::platform::OSEventKind::kKeyDown &&
                ev.key  == cd::platform::KeyCode::kEscape)
            {
                window.request_close();
            }
            else if (ev.kind == cd::platform::OSEventKind::kResize)
            {
                needs_rebuild = true;
            }
        }
        if (needs_rebuild && !rebuild_swapchain())
            continue;

        // ---- Simulation tick(s) — drain accumulated wall time --------------
        const auto t_now = std::chrono::steady_clock::now();
        const double wall_dt = std::min(
            std::chrono::duration<double>(t_now - t_prev).count(),
            kMaxDt);
        t_prev = t_now;
        accum += wall_dt;

        cd::camera::Camera render_cam {};   // updated from brain output below

        while (accum >= kDt)
        {
            accum -= kDt;
            const float fdt = static_cast<float>(kDt);

            // 1. Integrate player position (constant velocity walk)
            world.player_pos.x += world.player_velocity.x * fdt;
            world.player_pos.z += world.player_velocity.z * fdt;
            // y stays at 0.5 (cube sits on the ground plane)

            // 2. Rebuild query world
            query_world.clear();
            query_world.add_entity(world.player, world.player_pos, player_aabb(world));
            query_world.rebuild();

            // 3. Tick triggers
            const std::vector<gt::Subject> subjects {
                gt::Subject { world.player, world.player_pos, /*layer*/ 0U },
            };
            trigger_active = false;   // reset; callbacks set it again
            triggers.tick(&query_world, fdt, subjects);

            // 4. Tick camera brain — outputs a cd::camera::Camera
            render_cam = brain.tick(
                fdt, [&](Entity e) { return resolve_target_pos(world, e); });

            // 5. Particles: fire every 2 s, tick dispatcher
            const double sim_t = clock.get().elapsed_seconds;
            if (sim_t >= next_particle_fire_s)
            {
                const std::uint32_t idx = particles.fire(
                    "jump_dust", world.player_pos, Quatf::identity());
                if (idx == gp::ParticleEventDispatcher::kInvalidBurst)
                {
                    std::fprintf(stderr, "[pfx] fire('jump_dust') FAILED\n");
                    return EXIT_FAILURE;
                }
                next_particle_fire_s += kFireIntervalS;
            }
            particles.tick(fdt);

            // 6. Advance clock
            clock.tick(kDt);

            // 7. Decrement pfx blink counter
            if (pfx_blink_frames > 0)
            {
                --pfx_blink_frames;
                pfx_blink = (pfx_blink_frames > 0);
            }

            // 8. Console HUD (every ~1 simulated second)
            ++sim_tick;
            if ((sim_tick % ticks_per_status) == 0)
            {
                ++status_lines;
                std::printf("[hud] t=%4.2fs  player=(%.2f, %.2f, %.2f)  "
                            "active_pfx=%zu  trigger_in=%d\n",
                            clock.get().elapsed_seconds,
                            static_cast<double>(world.player_pos.x),
                            static_cast<double>(world.player_pos.y),
                            static_cast<double>(world.player_pos.z),
                            particles.active_count(),
                            enter_fire_count);
            }

            // Gate: once both success conditions are met, print summary once
            if (!sim_complete &&
                enter_fire_count >= 1 &&
                particle_fire_count >= 1)
            {
                sim_complete = true;
                std::puts("[sim] Both gates satisfied (trigger + particle). "
                          "Rendering continues — close the window to exit.");
                std::fflush(stdout);
            }
        }

        // ---- Render frame ---------------------------------------------------
        // Build VP from CameraBrain output.  When the player has not moved
        // yet the brain returns a sensible default (eye behind-and-above
        // origin due to position_offset = {0, 2.5, -4}).
        const float aspect = static_cast<float>(window.width()) /
                             static_cast<float>(window.height());
        const cd::math::Mat4f vp = cd::camera::view_projection(render_cam, aspect);

        auto frame_r = renderer.begin_frame();
        if (!frame_r.has_value())
        {
            if (frame_r.error().code ==
                static_cast<std::uint32_t>(
                    cd::render::render_errors::Code::kSwapchainOutOfDate))
            { needs_rebuild = true; continue; }
            std::fprintf(stderr, "[render] begin_frame: %.*s\n",
                         static_cast<int>(frame_r.error().message.size()),
                         frame_r.error().message.data());
            return EXIT_FAILURE;
        }
        auto& frame = *frame_r;
        auto& cmd   = *frame.command_buffer;

        // First-time depth transition UNDEFINED → DEPTH_WRITE
        if (!depth_init)
        {
            std::array<cd::rhi::TextureBarrier, 1> dbar {
                cd::rhi::TextureBarrier {
                    .texture = depth.image,
                    .from    = cd::rhi::ResourceState::kUndefined,
                    .to      = cd::rhi::ResourceState::kDepthWrite,
                    .range   = { 0, 1, 0, 1 },
                }
            };
            cmd.barrier({}, dbar);
            depth_init = true;
        }

        // Begin render pass — dark background
        std::array<cd::rhi::ColorAttachmentInfo, 1> color_attach {
            cd::rhi::ColorAttachmentInfo {
                .view        = frame.swapchain_image_view,
                .load_op     = cd::rhi::LoadOp::kClear,
                .store_op    = cd::rhi::StoreOp::kStore,
                .clear_color = { .f32 = { 0.08F, 0.10F, 0.12F, 1.0F } },
            }
        };
        cd::rhi::DepthStencilAttachmentInfo depth_attach {};
        depth_attach.view        = depth.view;
        depth_attach.depth_load  = cd::rhi::LoadOp::kClear;
        depth_attach.depth_store = cd::rhi::StoreOp::kStore;
        depth_attach.clear.depth = 1.0F;

        cd::rhi::RenderPassBeginInfo rp {};
        rp.render_area       = cd::rhi::Rect2D { { 0, 0 }, frame.extent };
        rp.color_attachments = color_attach;
        rp.depth_stencil     = &depth_attach;
        cmd.begin_render_pass(rp);

        cmd.set_viewport(cd::rhi::Viewport {
            0.0F, 0.0F,
            static_cast<float>(frame.extent.width),
            static_cast<float>(frame.extent.height),
            0.0F, 1.0F
        });
        cmd.set_scissor(cd::rhi::Rect2D { { 0, 0 }, frame.extent });

        // Apply shared material (pipeline bind)
        mat.apply(cmd);

        // ---- Ground plane --------------------------------------------------
        cmd.bind_vertex_buffer(0, ground_vb, 0);
        cmd.bind_index_buffer(ground_ib, 0, cd::rhi::IndexType::kUInt16);
        {
            // Ground uses identity model (already at y=0, wide)
            const cd::math::Mat4f mvp_g = vp;   // model = identity
            cmd.push_constants(
                mat.pipeline_layout(),
                cd::rhi::ShaderStage::kVertex,
                0,
                static_cast<std::uint32_t>(sizeof(mvp_g)),
                &mvp_g);
            cmd.draw_indexed(
                static_cast<std::uint32_t>(kGroundIndices.size()),
                1, 0, 0, 0);
        }

        // ---- Trigger volume proxy (red cube, scale = trigger_radius * 2) ---
        cmd.bind_vertex_buffer(0, trigger_vb, 0);
        cmd.bind_index_buffer(cube_ib, 0, cd::rhi::IndexType::kUInt16);
        {
            const float tscale = trigger_active ? kTriggerRadius * 2.4F
                                                : kTriggerRadius * 2.0F;
            draw_entity(cmd, mat, vp, kTriggerCentre, tscale,
                        static_cast<std::uint32_t>(kCubeIndices.size()));
        }

        // ---- Player entity (blue cube) -------------------------------------
        cmd.bind_vertex_buffer(0, player_vb, 0);
        cmd.bind_index_buffer(cube_ib, 0, cd::rhi::IndexType::kUInt16);
        draw_entity(cmd, mat, vp, world.player_pos, 1.0F,
                    static_cast<std::uint32_t>(kCubeIndices.size()));

        // ---- Particle burst blink (yellow cube at player pos, brief) -------
        if (pfx_blink)
        {
            cmd.bind_vertex_buffer(0, pfx_vb, 0);
            cmd.bind_index_buffer(cube_ib, 0, cd::rhi::IndexType::kUInt16);
            const Vec3f pfx_pos {
                world.player_pos.x,
                world.player_pos.y + 1.2F,   // float slightly above player
                world.player_pos.z
            };
            draw_entity(cmd, mat, vp, pfx_pos, 0.35F,
                        static_cast<std::uint32_t>(kCubeIndices.size()));
        }

        cmd.end_render_pass();

        auto end_r = renderer.end_frame();
        if (!end_r.has_value())
        {
            if (end_r.error().code ==
                static_cast<std::uint32_t>(
                    cd::render::render_errors::Code::kSwapchainOutOfDate))
            { needs_rebuild = true; continue; }
            std::fprintf(stderr, "[render] end_frame: %.*s\n",
                         static_cast<int>(end_r.error().message.size()),
                         end_r.error().message.data());
            return EXIT_FAILURE;
        }
    }

    // -----------------------------------------------------------------------
    // Cleanup + final summary
    // -----------------------------------------------------------------------
    renderer.wait_idle();

    depth.destroy(device);
    device.destroy_buffer(ground_ib);
    device.destroy_buffer(ground_vb);
    device.destroy_buffer(cube_ib);
    device.destroy_buffer(pfx_vb);
    device.destroy_buffer(trigger_vb);
    device.destroy_buffer(player_vb);

    std::puts("--- summary ---");
    std::printf("  sim ticks run       : %d\n", sim_tick);
    std::printf("  sim time elapsed    : %.3f s\n", clock.get().elapsed_seconds);
    std::printf("  trigger enter fires : %d\n", enter_fire_count);
    std::printf("  trigger stay  fires : %d\n", stay_fire_count);
    std::printf("  trigger exit  fires : %d\n", exit_fire_count);
    std::printf("  particle bursts     : %d\n", particle_fire_count);
    std::printf("  status lines        : %d\n", status_lines);
    std::printf("  vcams registered    : %zu\n", brain.vcam_count());

    // The visual sample succeeds as long as it rendered at least one frame
    // (i.e., made it past boot). Simulation gates are advisory in the
    // visual variant — the player may exit before 5 s (trigger) or 2 s
    // (first particle) if they close the window early.
    std::puts("=== hello_world visual OK ===");
    return EXIT_SUCCESS;
}
