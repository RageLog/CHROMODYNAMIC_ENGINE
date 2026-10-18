// =============================================================================
// CHROMODYNAMIC - samples/chromodynamic_studio
//
// Phase 138 / v0.99.64 - mega-showcase: a single ImGui-docked window
// running every major marathon subsystem live so a fresh observer can
// see "what does this engine do today?" in one place.
//
// Panels rendered (DockSpace, all visible at once):
//   * 3D Viewport - analytical-sky skybox + 5x5 PBR sphere sweep +
//     procedural primitive entities (cube / sphere / cone / cylinder /
//     torus from cd::asset::Primitives) + ECS-driven orbit camera.
//   * Scene tree - entity list, click to select.
//   * Inspector - DragFloat3 live-edit transform; EditHistory
//     captures drag-release as a single command.
//   * Audio meter - Mixer ??' Compressor ??' SimpleReverb ??' LowPass ??'
//     Limiter chain running continuously on a synthetic source;
//     panel shows comp gain reduction (dB), limiter activity, peak.
//   * Net sim ticker - SnapshotBuffer + DeltaWriter + LatencyStats +
//     Throttle simulating client/server traffic at 60 Hz, ASCII
//     table per second of stats.
//   * Random viz - PCG32 + Box-Muller histograms refreshed every
//     ~2 s.
//   * Counters - CounterTable snapshot (frames, draws, commands).
//   * History log - EditHistory + palette + sample-side events.
//   * Command Palette popup (Ctrl+Shift+P) - 15+ registered
//     commands including all four "Select primitive" entries,
//     transform resets, history clear, audio mute, net sim toggle,
//     random reseed, save/load.
//
// Stays at marathon discipline (sample pattern, single main.cpp,
// no engine apps-layer mimicry; that lands at v1.0+ time).
// =============================================================================
#include <cd/anim/Animation.hpp>
#include <cd/anim/GpuSkinning.hpp>
#include <cd/anim/Skeleton.hpp>
#include <cd/asset/AssetId.hpp>
#include <cd/asset/AssetRegistry.hpp>
#include <cd/asset/AsyncStreamer.hpp>
#include <cd/asset/Primitives.hpp>
#include <cd/asset/image/Image.hpp>
#include <cd/asset/StreamRequest.hpp>
#include <cd/asset/gltf/GltfLoader.hpp>
#include <cd/asset/gltf/SkinnedMeshBridge.hpp>
#include <cd/asset/json/Json.hpp>
#include <cd/atmosphere/Atmosphere.hpp>
#include <cd/audio/Compressor.hpp>
#include <cd/audio/IAudioBackend.hpp>
#include <cd/audio/Limiter.hpp>
#include <cd/audio/LowPass.hpp>
#include <cd/audio/Mixer.hpp>
#include <cd/audio/SimpleReverb.hpp>
#include <cd/audio/WasapiBackend.hpp>
#include <cd/brdf/ltc/Ltc.hpp>
#include <cd/brdf/multiscatter/TurquinMultiScatter.hpp>
#include <cd/brdf/sheen_clearcoat/SheenClearcoat.hpp>
#include <cd/brdf/sss/Sss.hpp>
#include <cd/camera/Camera.hpp>
#include <cd/camera/Frustum.hpp>
#include <cd/concurrency/JobGraph.hpp>
#include <cd/concurrency/ParallelFor.hpp>
#include <cd/concurrency/WorkStealingThreadPool.hpp>
#include <cd/core/CounterTable.hpp>
#include <cd/ddgi/Ddgi.hpp>
#include <cd/debug_draw/DebugDraw.hpp>
#include <cd/debug_line/DebugLine.hpp>
#include <cd/decal/Decal.hpp>
#include <cd/ecs/Entity.hpp>
#include <cd/ecs/World.hpp>
#include <cd/editor/AxisGizmo.hpp>
#include <cd/editor/CommandPalette.hpp>
#include <cd/editor/EditHistory.hpp>
#include <cd/editor/SelectionOutline.hpp>
#include <cd/editor/TransformCommands.hpp>
#include <cd/editor/panel/CompositeFxBinding.hpp>
#include <cd/frame_timing/FrameTimeRing.hpp>
#include <cd/gpu_particles/GpuParticles.hpp>
#include <cd/input/Axis.hpp>
// phase927 deps wired: cd::mesh_shader / cd::virtual_geometry /
// cd::virtual_textures added to CMakeLists DEPS so the live demos
// below can call into their public CPU APIs.
#include <cd/mesh_shader/Meshlet.hpp>
#include <cd/virtual_geometry/VirtualGeometry.hpp>
#include <cd/virtual_textures/VirtualTextures.hpp>
#include <cd/ibl/BrdfLut.hpp>
#include <cd/ibl/Cubemap.hpp>
#include <cd/ibl/IrradianceConvolution.hpp>
#include <cd/ibl/PrefilteredSpecular.hpp>
#include <cd/ibl_gpu/Upload.hpp>
#include <cd/light/Attenuation.hpp>
#include <cd/light/CascadedShadow.hpp>
#include <cd/light/ClusterGrid.hpp>
#include <cd/light/ColorTemperature.hpp>
#include <cd/light/Light.hpp>
#include <cd/light_shafts/LightShafts.hpp>
#include <cd/material/AnalyticalSkyMaterial.hpp>
#include <cd/material/Material.hpp>
#include <cd/nrc/Nrc.hpp>
#include <cd/restir_di/Reservoir.hpp>
#include <cd/restir_gi/GiReservoir.hpp>
#include <cd/texture_synth/Earth.hpp>
#include <cd/texture_synth/Noise.hpp>

// W8-AR: <cd/material/StandardPbrMaterial.hpp> include REMOVED.
// chromodynamic_studio no longer uses the dedicated StandardPbr pipeline;
// the PBR demo spheres are unified into the kPrimFS path. The
// library header stays available for studio_pbr and external samples.
#include <cd/framegraph/Targets.hpp>
#include <cd/math/Matrix.hpp>
#include <cd/math/CubicBezier.hpp>
#include <cd/math/QuatSlerp.hpp>
#include <cd/math/Quaternion.hpp>
#include <cd/math/Random.hpp>
#include <cd/math/Transform.hpp>
#include <cd/math/Vector.hpp>
#include <cd/net/DeltaWriter.hpp>
#include <cd/net/LatencyStats.hpp>
#include <cd/net/SnapshotBuffer.hpp>
#include <cd/net/Throttle.hpp>
#include <cd/platform/Window.hpp>
#include <cd/post/bloom/Bloom.hpp>
#include <cd/post/composite/Composite.hpp>
#include <cd/post/dof/Dof.hpp>
#include <cd/post/exposure/Setup.hpp>
#include <cd/post/gtao/Gtao.hpp>
#include <cd/post/motion_blur/MotionBlur.hpp>
#include <cd/post/smaa/Smaa.hpp>
#include <cd/post/ssr/Ssr.hpp>
#include <cd/post/taa/Taa.hpp>
#include <cd/render/MeshUpload.hpp>
#include <cd/render/PlanarShadow.hpp>
#include <cd/render/Renderer.hpp>
#include <cd/rhi/Barriers.hpp>
#include <cd/rhi/ICommandBuffer.hpp>
#include <cd/rhi/IDevice.hpp>
#include <cd/rhi/vulkan/VulkanDevice.hpp>
#include <cd/scene/Scene.hpp>
#include <cd/scene/SceneCameraController.hpp>
#include <cd/scene/Serializer.hpp>
#include <cd/shader/Compiler.hpp>
// phase1174 — behavior_designer fold (ADR-20260614). cd::ui_* overlay
// stack: a cd::ui::renderer_rhi::Submitter (Route B inline GLSL) +
// DrawBatcher render the library BehaviorDesigner panel as a golden-gated
// ImGui-coexisting overlay. See the boot/record blocks below.
#include <cd/ui/font/Font.hpp>
#include <cd/ui/layout/Flex.hpp>  // phase1177 — studio_ui fold (Flex demo tree)
#include <cd/ui/renderer/DrawBatcher.hpp>
#include <cd/ui/renderer_rhi/Submitter.hpp>
#include <cd/ui/theme/Theme.hpp>
#include <cd/ui/widgets/Widgets.hpp>
#include <cd/editor/panel_behavior_designer/BehaviorDesigner.hpp>
#include <cd/velocity/Velocity.hpp>
#include <cd/volumetric/clouds/Clouds.hpp>
#include <cd/volumetric/fog/Fog.hpp>
// phase512-volumetric-fog-wire: phase 469 froxel grid + GLSL kernel
// strings + Wronski quadratic warp helpers. The CPU FroxelGrid stays
// on the host until the 3D LUT GPU compute path lands; the symbols
// from this header are exercised below so the wire-up is one #include
// away from the GPU dispatch.
#include <cd/volumetric/VolumetricFog.hpp>
#include <cd/world_container/ProjectIo.hpp>
#include <cd/world_container/World.hpp>
#include <cd/ui/ChromaUi.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cd/core/Compat.hpp>  // compat::read_env_var (CD_HDR_LANES)
#include <cstring>
#include <deque>
#include <fstream>
#include <functional>
#include <ios>
#include <numbers>
#include <optional>
#include <random>
#include <ranges>
#include <span>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "PrimShader.hpp"
#include "StudioLighting.hpp"
#include "StudioRayTracingTypes.hpp"
#include "StudioPbrSphereGrid.hpp"
#include "StudioRenderFxState.hpp"
#include "StudioDdgiPipeline.hpp"
#include "StudioDiagnosticOverlays.hpp"
#include "StudioSkinnedMesh.hpp"
#include "StudioTlasRing.hpp"
#include "StudioViewportOverlays.hpp"
#include "StudioTlasBuilder.hpp"
#include "StudioSkinnedAnimator.hpp"
#include "StudioBootBindless.hpp"
#include "StudioBootGpu.hpp"
#include "StudioBootProject.hpp"
#include "StudioBootPostFx.hpp"
#include "StudioBootScene.hpp"
#include "StudioAudio.hpp"
#include "StudioIblBaker.hpp"
#include "StudioAppState.hpp"
#include "StudioRenderTargets.hpp"
#include "StudioMaterialPipeline.hpp"
#include "StudioShaderWatch.hpp"
#include "StudioGltfLoader.hpp"
#include "StudioSceneAssetIngest.hpp"
#include "StudioMeshRegistry.hpp"
#include "StudioEngineCommands.hpp"
#include "StudioViewportPicker.hpp"
#include "StudioFxCommands.hpp"
#include "SponzaFixtures.hpp"
#include "StudioCliOptions.hpp"

#include <cd/app/run.hpp>

// ============================================================================
// T1.7 phase543 -- Sponza golden-image readback wiring.
//
// CLI flag parsed in main() before cd::app::run<>:
//   --golden-fixture N            ; N in 0..4 selects cd::studio_rt::
//                                   sponza_fixtures::kFixtures[N].
//   --golden-out <path.png>       ; PNG file written via the GoldenCapture
//                                   helper after one full rendered frame.
//   --golden-frames N             ; capture happens on frame N-1 (default 3).
//
// Effect when --golden-fixture is set:
//   * Camera eye/target/fov_y are overridden in on_boot from kFixtures[N].
//   * scene_cam auto-spin + free-look update are skipped so the captured
//     frame is byte-deterministic for the chosen fixture.
//   * After end_frame on the configured "golden frame", the swapchain is
//     copied to a host-visible buffer and dumped to --golden-out as PNG.
//   * The app then requests shutdown and exits 0 on success / non-zero
//     on capture failure.
//
// Without --golden-fixture the runtime path is unchanged.
//
// phase825-rt-chrome-sponza-golden-cli-extract: the parser implementation
// lives in StudioCliOptions.hpp so a unit test can exercise it without
// bringing up Vulkan + the App lifecycle. See test_chromodynamic_studio_golden_cli.cpp.
// ============================================================================
// (golden::CliOptions / options() / parse() / enabled() declared in
// StudioCliOptions.hpp included above with the other Studio* sample headers.)

// ============================================================================
// File-scope types needed by StudioApp::EngineState + the three lifecycle
// overrides.  They were previously in the anonymous namespace; moving them
// here makes them visible to StudioApp.hpp (included above) and to the
// post-namespace implementation block (on_boot / on_frame / on_shutdown).
// ============================================================================

// ---- Per-entity 3D draw data -----------------------------------------------
enum class PrimitiveKind : std::uint8_t
{
    kCube,
    kSphere,
    kCone,
    kCylinder,
    kTorus,
    kTorusKnot,
    kGltf,       ///< CesiumMan / animated character
    kSponza,     ///< Sponza atrium static scene — separate from kGltf so
                 ///  the turntable + Z-up correction never touch it
    kCustomGltf, ///< User-loaded disk .gltf/.glb mesh from Content Browser
};

struct SceneEntity
{
    cd::ecs::Entity handle {};
    std::string name;
    cd::math::Vec3f tint { 1.0F, 1.0F, 1.0F };
    float metallic { 0.0F };
    float roughness { 0.5F };
    float multiscatter_comp { 0.0F }; // Turquin (2019) energy compensation [0..1]
    float iridescence_nm { 0.0F };    // Belcour-Barla (2017) thin-film thickness [0..1000 nm]
    PrimitiveKind kind { PrimitiveKind::kCube };
    bool is_pbr { false };
    // phase983-pbr-texture-fix: per-entity opt-in for albedo texture
    // sampling on PBR entities. The PBR grid path used to hard-gate
    // fx_params[1] = 0 for is_pbr=true draws, which silently disabled
    // texture sampling for every PBR sphere. Setting use_texture=true
    // (via the R-Showcase R2 panel or default for designated spheres)
    // flips fx_params[1] to 1.0 so the shader samples cd_albedo_tex
    // and multiplies by tint instead of using the vertex-coloured
    // procedural path.
    bool use_texture { false };
    int custom_texture_id { 0 }; // 0=None, 1=Earth/Disk, 2=Checker, 3=Brick, 4=Carbon, 5=Marble, 6=Wood, 120..250=DiskTexture
    std::string custom_texture_path {};
    std::string custom_mesh_path {};
    bool visible { true };
    bool locked { false };
    cd::math::Mat4f prev_model { cd::math::Mat4f::identity() };
    bool prev_model_valid { false };
};

[[maybe_unused]] [[nodiscard]] static inline PrimitiveKind kind_from_name(std::string_view n) noexcept
{
    if (n == "Sphere")     return PrimitiveKind::kSphere;
    if (n == "Cone")       return PrimitiveKind::kCone;
    if (n == "Cylinder")   return PrimitiveKind::kCylinder;
    if (n == "Torus")      return PrimitiveKind::kTorus;
    if (n == "TorusKnot")  return PrimitiveKind::kTorusKnot;
    if (n == "Gltf")       return PrimitiveKind::kGltf;
    if (n == "Sponza")     return PrimitiveKind::kSponza;
    if (n == "CustomGltf") return PrimitiveKind::kCustomGltf;
    return PrimitiveKind::kCube;
}

// phase1140 (on_boot extraction batch 6 prep): GpuTexture2D +
// create_texture_rgba8 moved to StudioRenderTargets.hpp (cd_studio) so
// boot helpers in headers can create procedural textures. Using-decls
// keep every existing unqualified reference compiling unchanged.
using cd_studio::GpuTexture2D;
using cd_studio::create_texture_rgba8;

// phase1146 (on_boot extraction batch 7): setup_world_container moved to
// StudioBootProject.hpp (cd_studio). The only remaining unqualified callsite
// sits inside the #if 0 M2E legacy block, so no using-decl is needed
// (misc-unused-using-decls).

// ---- Random viz histogram --------------------------------------------------
struct Histogram
{
    std::vector<std::size_t> bins;
    float lo { 0 };
    float hi { 1 };

    void rebuild(std::span<const float> samples, float lo_, float hi_, int n_bins)
    {
        lo = lo_; hi = hi_;
        bins.assign(static_cast<std::size_t>(n_bins), 0);
        const float inv = static_cast<float>(n_bins) / (hi - lo);
        for (float s : samples)
        {
            if (s < lo || s >= hi) continue;
            int idx = static_cast<int>((s - lo) * inv);
            if (idx >= 0 && idx < n_bins) ++bins[static_cast<std::size_t>(idx)];
        }
    }
};

// ---- Selection + light row -------------------------------------------------
enum class SelKind : std::uint8_t { kEntity = 0, kLight = 1 };

struct LightRow
{
    std::string name;
    cd::light::Light light;
    bool  enabled { true };
    float kelvin  { 6500.0F };
};

// ---- Editor gizmo state ----------------------------------------------------
enum class GizmoMode : std::uint8_t { kTranslate = 0, kRotate = 1, kScale = 2 };

struct GizmoState
{
    bool      visible                   { true };
    GizmoMode mode                      { GizmoMode::kTranslate };
    ImVec2    drag_anchor               { 0, 0 };
    cd::math::Vec3f drag_world_start    {};
    cd::math::Vec3f drag_plane_hit_start {};
    cd::math::Vec3f drag_scale_start    { 1.0F, 1.0F, 1.0F };
    cd::math::Quatf drag_rot_start      {};
    cd::math::Vec3f light_drag_dir_start     { 0.0F, -1.0F, 0.0F };
    cd::math::Vec3f light_drag_tangent_start { 1.0F,  0.0F, 0.0F };
    float light_drag_range_start  { 0.0F };
    float light_drag_area_w_start { 1.0F };
    float light_drag_area_h_start { 1.0F };
    float drag_initial_offset     { 0.0F };
    bool  drag_use_ray_plane      { false };
    bool  was_hovered             { false };
};

// ---- Camera motion-blur basis snapshot -------------------------------------
struct PrevCamBasis
{
    cd::math::Vec3f right { 1.0F, 0.0F,  0.0F };
    cd::math::Vec3f up    { 0.0F, 1.0F,  0.0F };
    cd::math::Vec3f fwd   { 0.0F, 0.0F, -1.0F };
    cd::math::Vec3f pos   { 0.0F, 0.0F,  0.0F };
    float half_w { 1.0F };
    float half_h { 1.0F };
    bool  valid  { false };
};

// ============================================================================
// StudioApp class + EngineState + lifecycle implementations.
// Included HERE (after file-scope types above) so the implementations have
// access to PrimitiveKind, SceneEntity, GizmoState, SelKind, LightRow, etc.
// ============================================================================
#include "StudioApp.hpp"
#include "StudioToolbar.hpp"

namespace
{
std::string   g_studio_capture_png          {};
std::uint32_t g_studio_capture_frame        { 8U };
std::uint32_t g_studio_max_frames           { 0U };
bool          g_studio_start_pie            { false };
bool          g_studio_autopilot_e2e        { false };
[[maybe_unused]] bool g_studio_force_project_manager { false };
std::string   g_studio_capture_gameplay_dir {};
bool          g_studio_demo_all_phases      { false };
bool          g_studio_load_context_demo    { false };
int           g_studio_initial_template     { -1 };
std::string   g_studio_initial_scene        {};
bool          g_studio_has_cli_cam_eye      { false };
cd::math::Vec3f g_studio_cli_cam_eye        {};
bool          g_studio_has_cli_cam_target   { false };
cd::math::Vec3f g_studio_cli_cam_target     {};


// ============================================================================
// Synthetic audio source (drives the DSP chain continuously).
// ============================================================================

constexpr std::uint32_t kAudioSampleRate = 48000;
constexpr std::size_t kAudioBufferLen = 512;  // samples per tick

[[nodiscard]] float square_wave(std::uint64_t i, float hz) noexcept
{
    const float phase = static_cast<float>(i) * hz / static_cast<float>(kAudioSampleRate);
    const float frac = phase - std::floor(phase);
    return (frac < 0.5F) ? 0.55F : -0.55F;
}

[[nodiscard]] float burst_noise(std::uint64_t i) noexcept
{
    const auto cycle = static_cast<std::uint64_t>(kAudioSampleRate / 4);       // 250 ms
    const auto burst_len = static_cast<std::uint64_t>(kAudioSampleRate / 30);  // 33 ms
    if ((i % cycle) >= burst_len)
        return 0.0F;
    std::uint64_t x = i * 2654435761ULL + 0xC0FFEEULL;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    const float n = (static_cast<float>(x & 0xFFFFFFFFu) / static_cast<float>(0xFFFFFFFFu)) * 2.0F - 1.0F;
    return n * 0.7F;
}

// PrimitiveKind, SceneEntity, kind_from_name moved to file scope (before namespace {}).

// ============================================================================
// phase1174 — behavior_designer fold (ADR-20260614).
//
// Theme bridge + font probe ported VERBATIM from the now-deleted
// samples/editor/studio_behavior_designer/main.cpp. They feed the
// cd::ui::renderer_rhi::Submitter overlay that renders the library
// BehaviorDesigner panel. The font is optional (Route B inline shader
// does not sample the atlas today); kept for parity with the source sample.
// ============================================================================

[[nodiscard]] cd::ui::widgets::Color to_widget_color(cd::ui::theme::ColorToken c) noexcept
{
    auto pack = [](float v) noexcept -> std::uint8_t {
        const float clamped = std::clamp(v, 0.0F, 1.0F);
        return static_cast<std::uint8_t>(std::lround(clamped * 255.0F));
    };
    return cd::ui::widgets::Color { pack(c.r), pack(c.g), pack(c.b), pack(c.a) };
}

[[nodiscard]] cd::ui::widgets::Theme build_dark_widget_theme()
{
    const auto dark = cd::ui::theme::k_dark_theme();
    cd::ui::widgets::Theme t {};
    t.background    = to_widget_color(dark.color(cd::ui::theme::PaletteSlot::kBackground));
    t.surface       = to_widget_color(dark.color(cd::ui::theme::PaletteSlot::kSurface));
    t.surface_hover = to_widget_color(dark.color(cd::ui::theme::PaletteSlot::kSurfaceVariant));
    t.surface_press = to_widget_color(dark.color(cd::ui::theme::PaletteSlot::kPrimaryContainer));
    t.accent        = to_widget_color(dark.color(cd::ui::theme::PaletteSlot::kPrimary));
    t.accent_hover  = to_widget_color(dark.color(cd::ui::theme::PaletteSlot::kPrimaryContainer));
    t.text          = to_widget_color(dark.color(cd::ui::theme::PaletteSlot::kOnSurface));
    t.text_dim      = to_widget_color(dark.color(cd::ui::theme::PaletteSlot::kOnSurfaceVariant));
    return t;
}

[[nodiscard]] std::vector<std::uint8_t> find_system_font_for_bd()
{
    static const std::array<const char*, 6> k_candidates {
        "C:/Windows/Fonts/segoeui.ttf",
        "C:/Windows/Fonts/calibri.ttf",
        "C:/Windows/Fonts/arial.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/TTF/DejaVuSans.ttf",
        "/System/Library/Fonts/Helvetica.ttc",
    };
    for (const char* p : k_candidates)
    {
        std::ifstream f(p, std::ios::binary | std::ios::ate);
        if (!f) { continue; }
        const auto sz = static_cast<std::size_t>(f.tellg());
        f.seekg(0);
        std::vector<std::uint8_t> out(sz);
        f.read(reinterpret_cast<char*>(out.data()),
               static_cast<std::streamsize>(sz));
        if (!out.empty()) { return out; }
    }
    return {};
}

// ============================================================================
// phase1177 — studio_ui fold (ADR-20260614 §3.4, Job C).
//
// The Button/Slider/Label/Flex demo ported VERBATIM (semantically) from the
// now-deleted samples/ui/studio_ui/main.cpp. A column FlexTree with three
// leaves (header label / button / slider) is solved each frame for the
// panel rect, the leaf rects drive the widget rects, the widgets tick on
// the (currently empty) input snapshot, and everything is drawn into the
// shared ui_batcher. The Route B inline Submitter is solid-fill only (it
// does not sample the glyph atlas today), so the demo renders as the panel
// background + button face + slider track/knob quads — identical visual
// contract to the BehaviorDesigner overlay it shares the Submitter with.
// ============================================================================

[[nodiscard]] cd::ui::widgets::Rect uiw_to_widget_rect(const cd::ui::layout::Rect& r) noexcept
{
    return cd::ui::widgets::Rect { r.x, r.y, r.width, r.height };
}

/// Build the column-flex demo tree (header label / button / slider leaves).
/// Mirrors studio_ui's build_tree(); writes the node ids back into EngineState.
void uiw_build_tree(cd::ui::layout::FlexTree& tree,
                    cd::ui::layout::NodeId& root,
                    cd::ui::layout::NodeId& label,
                    cd::ui::layout::NodeId& button,
                    cd::ui::layout::NodeId& slider)
{
    namespace ll = cd::ui::layout;
    ll::FlexStyle rs;
    rs.direction   = ll::FlexDirection::kColumn;
    rs.align_items = ll::AlignItems::kStretch;
    rs.padding     = { 16.0F, 16.0F, 16.0F, 16.0F };
    rs.gap_main    = 12.0F;
    root = tree.create_node(rs);

    ll::FlexStyle leaf;
    leaf.flex_shrink      = 0.0F;
    leaf.intrinsic_height = 36.0F;
    label = tree.create_node(leaf);

    leaf.intrinsic_height = 40.0F;
    button = tree.create_node(leaf);

    leaf.intrinsic_height = 28.0F;
    slider = tree.create_node(leaf);

    tree.add_child(root, label);
    tree.add_child(root, button);
    tree.add_child(root, slider);
}

// ============================================================================
// GPU mesh holder.
//
// Phase 290 (Run 7 extract pass): the GpuMesh struct + upload_mesh +
// destroy_mesh helpers moved to cd::render::MeshUpload (header-only).
// The sample brings them into the anonymous namespace via using-
// declarations so existing call sites stay identical.
// ============================================================================

using cd::render::GpuMesh;
using cd::render::destroy_mesh;

// W8-AR: PbrVertex / to_pbr_vertices / upload_pbr_mesh REMOVED.
// They existed to feed the dedicated StandardPbrMaterial pipeline
// with a pos+normal-only stream. After W8-AR the PBR demo spheres
// are ECS entities rendered through the same PrimitiveVertex
// (pos+normal+uv+color) stream every other primitive uses.

// =============================================================================
// Phase 289 / Marathon Run 7 sub-N1A: the embedded kPrimVS / kPrimFS / kShadowVS
// / kShadowFS GLSL strings (~990 lines total) live in PrimShader.hpp +
// PrimShader_kPrimFS.inl + PrimShader_kShadow.inl now. The remaining
// main.cpp call sites were extracted since, so no using-decls are needed.
// =============================================================================


// R3 - Composite pass - shader source + push struct extracted into
// cd::post_composite. chromodynamic_studio just references the namespace via
// the using-decl below.
using CompositePush = cd::post::composite::Push;

// R3 - Multi-mip bloom (Karis 2013) - shader source + push struct
// definitions are extracted into cd::post_bloom. chromodynamic_studio just
// references them via the namespace.
using BloomPrefilterPush = cd::post::bloom::PrefilterPush;
using BloomUpsamplePush = cd::post::bloom::UpsamplePush;

// =============================================================================
// Phase 290 / Marathon Run 7 sub-N1B: PrimPush + LightSlotGpu + LightUboGpu
// layouts + the pack_light_slot() helper live in StudioLighting.hpp. main.cpp
// re-imports the names so existing call sites stay identical.
// =============================================================================
using cd::studio_rt::PrimPush;
using cd::studio_rt::LightUboGpu;
using cd::studio_rt::pack_light_slot;

// GpuTexture2D, create_texture_rgba8 moved to file scope (before namespace {}).

// ============================================================================
// R1 - True IBL helpers (HDR cubemap + diffuse irradiance + BRDF LUT).
//
// Generates a CPU environment cubemap by sampling the analytical sky
// function (same palette as AnalyticalSkyMaterial::sample_env), runs
// cd::ibl convolutions (irradiance + prefiltered specular + BRDF LUT),
// and uploads to GPU as kCube + kCube-with-mips + k2D textures.
// ============================================================================

// ----------------------------------------------------------------------------
// Phase 292 / Marathon Run 7 sub-N1D: planar-shadow projection matrix
// builder moved to cd::render::PlanarShadow.hpp. The helper is brought
// back in scope below via a using-decl so the caster loop's existing
// `make_planar_shadow_matrix(...)` call site stays identical.
// ----------------------------------------------------------------------------
using cd::render::make_planar_shadow_matrix;

// ============================================================================
// Render-target helpers - extracted to cd::framegraph::Targets.
// BloomMipChain extracted to cd::post_bloom.
// ============================================================================
using ColorTarget = cd::framegraph::ColorTarget;
using DepthTarget = cd::framegraph::DepthTarget;
using BloomMipChain = cd::post::bloom::BloomMipChain;
// phase1140: create_depth_target using-decl dropped — the last direct
// caller moved into StudioBootGpu.hpp (ADL resolves it there).
using cd::post::bloom::create_bloom_chain;

// Histogram, SelKind, LightRow moved to file scope (before namespace {}).

// =============================================================================
// Phase 297 / Marathon Run 8 sub-N2A: small self-contained UI panel draw
// helpers extracted from main(). These touch a narrow, well-defined slice
// of frame state (counters table, random-viz histograms, history + log)
// and have no shared draw-order coupling with the rest of the UI loop, so
// they extract cleanly with explicit parameter lists - no FrameContext
// aggregate required yet. The dt-ring static inside draw_counters_panel
// keeps the same single-instance lifetime as before (one named function,
// one static).
// =============================================================================

// ---- draw_selection_outline_overlay ---------------------------------------
inline void draw_selection_outline_overlay(const std::vector<SceneEntity>& entities,
                                           int selected,
                                           SelKind selected_kind,
                                           cd::editor::SelectionOutline& outline,
                                           cd::scene::Scene& scene,
                                           const cd::math::Mat4f& vp,
                                           cd::rhi::Extent2D extent)
{
    // We use the kWireframe style: project the selected entity's
    // world position onto the screen, then draw a circle around it
    // via ImGui's foreground draw list. Cheap, no extra GPU pass,
    // and demonstrates SelectionOutline state end-to-end.
    if (selected_kind == SelKind::kEntity && selected >= 0 && std::cmp_less(selected,entities.size()))
    {
        outline.set(entities[static_cast<std::size_t>(selected)].handle);
    }
    else
    {
        outline.clear();
    }
    outline.clamp_params();
    if (outline.style != cd::editor::OutlineStyle::kNone && !outline.empty())
    {
        const auto vw = static_cast<float>(extent.width);
        const auto vh = static_cast<float>(extent.height);
        // Background draw-list keeps gizmos BEHIND ImGui panels so
        // the selection ring doesn't bleed through Lights / Inspector
        // / Showcase windows (user-reported bug B01).
        auto* dl = ImGui::GetBackgroundDrawList();
        const ImU32 col = ImGui::ColorConvertFloat4ToU32(
            ImVec4(outline.color.x, outline.color.y, outline.color.z, outline.opacity)
        );
        for (const auto& e : outline.entities())
        {
            auto* lt = scene.local(e);
            if (lt == nullptr)
                continue;
            const auto& p = lt->value.position;
            // Project world ??' NDC ??' pixel.
            const cd::math::Vec4f wp { p.x, p.y, p.z, 1.0F };
            cd::math::Vec4f clip {};
            for (std::size_t r = 0; r < 4; ++r)
            {
                clip[r] = vp[0][r] * wp[0] + vp[1][r] * wp[1] + vp[2][r] * wp[2] + vp[3][r] * wp[3];
            }
            if (clip[3] <= 0.25F)
                continue;  // behind or too close to camera near plane
            const float ndc_x = clip[0] / clip[3];
            const float ndc_y = clip[1] / clip[3];
            const float sx = (ndc_x * 0.5F + 0.5F) * vw;
            const float sy = (1.0F - (ndc_y * 0.5F + 0.5F)) * vh;
            if (sx < -vw || sx > 2.0F * vw || sy < -vh || sy > 2.0F * vh)
                continue;
            // Radius matches the outer projected silhouette of the entity.
            const auto& sc = lt->value.scale;
            const float s_max = std::max({ std::abs(sc.x), std::abs(sc.y), std::abs(sc.z), 0.25F });
            const float radius = std::clamp((0.58F * s_max / clip[3]) * vh, 14.0F, 180.0F);
            dl->AddCircle(ImVec2(sx, sy), radius, col, 48, outline.thickness * 1.5F);
            // Crosshair tick marks outside the silhouette ring.
            dl->AddLine(ImVec2(sx - radius - 8.0F, sy), ImVec2(sx - radius - 2.0F, sy), col, outline.thickness);
            dl->AddLine(ImVec2(sx + radius + 2.0F, sy), ImVec2(sx + radius + 8.0F, sy), col, outline.thickness);
            dl->AddLine(ImVec2(sx, sy - radius - 8.0F), ImVec2(sx, sy - radius - 2.0F), col, outline.thickness);
            dl->AddLine(ImVec2(sx, sy + radius + 2.0F), ImVec2(sx, sy + radius + 8.0F), col, outline.thickness);
        }
    }
}

// ---- draw_light_markers_overlay -------------------------------------------
// Each enabled light gets a visual in the viewport when selected in the
// Lights / Inspector panel so unselected lights never clutter the 3D scene
// with floating text or near-plane-exploding wireframe lines.
inline void draw_light_markers_overlay(const std::vector<LightRow>& lights,
                                       int selected,
                                       SelKind selected_kind,
                                       const cd::math::Mat4f& vp,
                                       cd::rhi::Extent2D extent)
{
    const auto vw = static_cast<float>(extent.width);
    const auto vh = static_cast<float>(extent.height);
    // Background draw-list - same fix as the outline drawlist
    // above. Light gizmos / cone edges / range rings no longer
    // bleed across the Lights/Inspector/Showcase panels.
    auto* dl_m = ImGui::GetBackgroundDrawList();
    auto project = [&](const cd::math::Vec3f& p) -> ImVec2
    {
        const cd::math::Vec4f wp { p.x, p.y, p.z, 1.0F };
        cd::math::Vec4f c {};
        for (std::size_t r = 0; r < 4; ++r)
            c[r] = vp[0][r] * wp[0] + vp[1][r] * wp[1] + vp[2][r] * wp[2] + vp[3][r] * wp[3];
        if (c[3] <= 0.25F)
            return { -1.0F, -1.0F };
        const float sx = (c[0] / c[3] * 0.5F + 0.5F) * vw;
        const float sy = (1.0F - (c[1] / c[3] * 0.5F + 0.5F)) * vh;
        if (sx < -vw || sx > 2.0F * vw || sy < -vh || sy > 2.0F * vh)
            return { -1.0F, -1.0F };
        return { sx, sy };
    };
    for (std::size_t li = 0; li < lights.size(); ++li)
    {
        const auto& lrow = lights[li];
        if (!lrow.enabled)
            continue;
        const auto& L = lrow.light;
        const bool sel = (selected_kind == SelKind::kLight && std::cmp_equal(selected,li));
        if (!sel)
            continue;
        const ImU32 col = ImGui::ColorConvertFloat4ToU32(ImVec4(L.color.x, L.color.y, L.color.z, 1.0F));
        const ImU32 col_dim =
            ImGui::ColorConvertFloat4ToU32(ImVec4(L.color.x * 0.6F, L.color.y * 0.6F, L.color.z * 0.6F, 0.7F));
        const ImU32 col_sel =
            ImGui::ColorConvertFloat4ToU32(ImVec4(1.0F, 0.85F, 0.0F, 1.0F));  // golden hover-style for selected
        switch (L.type)
        {
            case cd::light::LightType::kDirectional:
            {
                // Render an arrow from sky position toward scene center.
                cd::math::Vec3f sky_origin { -L.direction.x * 15.0F,
                                             -L.direction.y * 15.0F,
                                             -L.direction.z * 15.0F };
                cd::math::Vec3f tip { sky_origin.x + L.direction.x * 8.0F,
                                      sky_origin.y + L.direction.y * 8.0F,
                                      sky_origin.z + L.direction.z * 8.0F };
                const auto p0 = project(sky_origin);
                const auto p1 = project(tip);
                if (p0.x >= 0.0F && p1.x >= 0.0F)
                {
                    dl_m->AddLine(p0, p1, col, 3.0F);
                    dl_m->AddCircleFilled(p0, 8.0F, col);
                    if (sel)
                        dl_m->AddCircle(p0, 16.0F, col_sel, 16, 3.0F);
                    dl_m->AddText(ImVec2(p0.x + 10.0F, p0.y - 8.0F), col, "SUN");
                }
                break;
            }
            case cd::light::LightType::kPoint:
            {
                const auto p = project(L.position);
                if (p.x >= 0.0F)
                {
                    dl_m->AddCircleFilled(p, 10.0F, col);
                    dl_m->AddCircle(p, 14.0F, col_dim, 12, 2.0F);
                    if (sel)
                        dl_m->AddCircle(p, 18.0F, col_sel, 16, 3.0F);
                    // Range ring - only draw when this light is selected
                    // so unselected lights show just a dot/icon instead
                    // of a noisy 16-segment circle that cuts through
                    // every nearby mesh (user-reported B04 clutter).
                    if (sel)
                    {
                        for (int i = 0; i < 24; ++i)
                        {
                            const float t0 = static_cast<float>(i) / 24.0F * 2.0F * std::numbers::pi_v<float>;
                            const float t1 = static_cast<float>(i + 1) / 24.0F * 2.0F * std::numbers::pi_v<float>;
                            cd::math::Vec3f a { L.position.x + std::cos(t0) * L.range,
                                                L.position.y,
                                                L.position.z + std::sin(t0) * L.range };
                            cd::math::Vec3f b { L.position.x + std::cos(t1) * L.range,
                                                L.position.y,
                                                L.position.z + std::sin(t1) * L.range };
                            const auto pa = project(a);
                            const auto pb = project(b);
                            if (pa.x >= 0.0F && pb.x >= 0.0F)
                                dl_m->AddLine(pa, pb, col_dim, 1.5F);
                        }
                    }
                    dl_m->AddText(ImVec2(p.x + 14.0F, p.y - 8.0F), col, "POINT");
                }
                break;
            }
            case cd::light::LightType::kSpot:
            {
                const auto p_apex = project(L.position);
                // Far disk at range along direction.
                cd::math::Vec3f far_center { L.position.x + L.direction.x * L.range,
                                             L.position.y + L.direction.y * L.range,
                                             L.position.z + L.direction.z * L.range };
                // Use a tangent basis on the cone axis.
                cd::math::Vec3f up { 0, 1, 0 };
                if (std::abs(L.direction.y) > 0.95F)
                    up = { 1, 0, 0 };
                cd::math::Vec3f rgt { L.direction.y * up.z - L.direction.z * up.y,
                                      L.direction.z * up.x - L.direction.x * up.z,
                                      L.direction.x * up.y - L.direction.y * up.x };
                const float rgt_len = std::sqrt(rgt.x * rgt.x + rgt.y * rgt.y + rgt.z * rgt.z);
                if (rgt_len > 1e-5F)
                {
                    rgt.x /= rgt_len;
                    rgt.y /= rgt_len;
                    rgt.z /= rgt_len;
                }
                cd::math::Vec3f bt { L.direction.y * rgt.z - L.direction.z * rgt.y,
                                     L.direction.z * rgt.x - L.direction.x * rgt.z,
                                     L.direction.x * rgt.y - L.direction.y * rgt.x };
                // outer cone half-angle from cos_outer
                const float outer_angle = std::acos(std::clamp(L.cos_outer_cone, -1.0F, 1.0F));
                const float disk_r = L.range * std::tan(outer_angle);
                // Cone edges + far-disk circle - drawn only when the
                // spot is selected. Unselected lights show just the
                // apex icon so they don't clutter the scene with rays
                // through every nearby mesh (user-reported B04).
                if (sel)
                {
                    const int kEdges = 4;
                    std::array<cd::math::Vec3f, kEdges + 1> rim {};
                    for (int i = 0; i <= kEdges; ++i)
                    {
                        const float t = static_cast<float>(i) / static_cast<float>(kEdges) * 2.0F * std::numbers::pi_v<float>;
                        const float ct = std::cos(t);
                        const float st = std::sin(t);
                        rim[static_cast<std::size_t>(i)] = { far_center.x + (rgt.x * ct + bt.x * st) * disk_r,
                                                             far_center.y + (rgt.y * ct + bt.y * st) * disk_r,
                                                             far_center.z + (rgt.z * ct + bt.z * st) * disk_r };
                    }
                    // Apex ? 4 edge points.
                    for (int i = 0; i < kEdges; ++i)
                    {
                        const auto pe = project(rim[static_cast<std::size_t>(i)]);
                        if (p_apex.x >= 0.0F && pe.x >= 0.0F)
                            dl_m->AddLine(p_apex, pe, col_dim, 1.5F);
                    }
                    // Far-disk rim - close the cone visually.
                    for (int i = 0; i < kEdges; ++i)
                    {
                        const auto pa = project(rim[static_cast<std::size_t>(i)]);
                        const auto pb = project(rim[static_cast<std::size_t>(i) + 1U]);
                        if (pa.x >= 0.0F && pb.x >= 0.0F)
                            dl_m->AddLine(pa, pb, col_dim, 1.5F);
                    }
                }
                if (p_apex.x >= 0.0F)
                {
                    dl_m->AddCircleFilled(p_apex, 8.0F, col);
                    if (sel)
                        dl_m->AddCircle(p_apex, 16.0F, col_sel, 16, 3.0F);
                    dl_m->AddText(ImVec2(p_apex.x + 10.0F, p_apex.y - 8.0F), col, "SPOT");
                }
                break;
            }
            case cd::light::LightType::kRectArea:
            case cd::light::LightType::kDiskArea:
            {
                // Derive tangent + bitangent from L.direction
                // exactly the way the FS does - so when the user
                // rotates the area light's direction via the
                // Inspector or gizmo, the visual rectangle
                // rotates with it. Closes 'area donunce gorseli
                // donmuyor' bug.
                cd::math::Vec3f ln = L.direction;
                const float lnl = std::sqrt(ln.x * ln.x + ln.y * ln.y + ln.z * ln.z);
                if (lnl > 1e-5F)
                {
                    ln.x /= lnl;
                    ln.y /= lnl;
                    ln.z /= lnl;
                }
                else
                {
                    ln = { 0.0F, 0.0F, -1.0F };
                }
                // W8-O: read the SAME area_tangent the shader uses.
                // Earlier wireframe derived its tangent via Frisvad
                // while the shader read the uploaded tangent — when
                // the user rotated the rect via the gizmo, the
                // wireframe rotated by Frisvad's smooth derivation
                // and the actual lit polygon rotated by the
                // user-controlled tangent, so the two visibly
                // disagreed. Use light.area_tangent for both.
                cd::math::Vec3f t = L.area_tangent;
                const float tll = std::sqrt(t.x * t.x + t.y * t.y + t.z * t.z);
                if (tll > 1e-5F)
                {
                    t.x /= tll;
                    t.y /= tll;
                    t.z /= tll;
                }
                else
                {
                    t = { 1.0F, 0.0F, 0.0F };
                }
                // Re-orthogonalise tangent against the (possibly
                // dragged) normal — same trick the rotate gizmo
                // applies after rotating both.
                const float pr = t.x * ln.x + t.y * ln.y + t.z * ln.z;
                t.x -= pr * ln.x;
                t.y -= pr * ln.y;
                t.z -= pr * ln.z;
                const float tnl = std::sqrt(t.x * t.x + t.y * t.y + t.z * t.z);
                if (tnl > 1e-5F)
                {
                    t.x /= tnl;
                    t.y /= tnl;
                    t.z /= tnl;
                }
                else
                {
                    t = { 1.0F, 0.0F, 0.0F };
                }
                // bitangent = normal x tangent (matches the shader's
                // cross(N, T) for B_rect).
                cd::math::Vec3f b { ln.y * t.z - ln.z * t.y, ln.z * t.x - ln.x * t.z, ln.x * t.y - ln.y * t.x };
                // Project 4 corners.
                const float hw = L.area_width * 0.5F;
                const float hh = L.area_height * 0.5F;
                cd::math::Vec3f c0 { L.position.x - t.x * hw - b.x * hh,
                                     L.position.y - t.y * hw - b.y * hh,
                                     L.position.z - t.z * hw - b.z * hh };
                cd::math::Vec3f c1 { L.position.x + t.x * hw - b.x * hh,
                                     L.position.y + t.y * hw - b.y * hh,
                                     L.position.z + t.z * hw - b.z * hh };
                cd::math::Vec3f c2 { L.position.x + t.x * hw + b.x * hh,
                                     L.position.y + t.y * hw + b.y * hh,
                                     L.position.z + t.z * hw + b.z * hh };
                cd::math::Vec3f c3 { L.position.x - t.x * hw + b.x * hh,
                                     L.position.y - t.y * hw + b.y * hh,
                                     L.position.z - t.z * hw + b.z * hh };
                const auto p0 = project(c0);
                const auto p1 = project(c1);
                const auto p2 = project(c2);
                const auto p3 = project(c3);
                if (p0.x >= 0.0F && p1.x >= 0.0F && p2.x >= 0.0F && p3.x >= 0.0F)
                {
                    const float thickness = sel ? 4.0F : 2.0F;
                    const ImU32 use_col = sel ? col_sel : col;
                    dl_m->AddLine(p0, p1, use_col, thickness);
                    dl_m->AddLine(p1, p2, use_col, thickness);
                    dl_m->AddLine(p2, p3, use_col, thickness);
                    dl_m->AddLine(p3, p0, use_col, thickness);
                    dl_m->AddText(p0, col, "AREA");
                    // W8-R: explicit normal arrow so the user can see
                    // which side is emissive (one-sided rect lights
                    // only illuminate +N hemisphere). Arrow shoots
                    // from the rect centre along +ln by 1/3 of the
                    // longer side length, big enough to be visible
                    // but not overwhelming.
                    const float arrow_len = std::max(L.area_width, L.area_height) * 0.6F + 0.3F;
                    const cd::math::Vec3f arrow_tip { L.position.x + ln.x * arrow_len,
                                                      L.position.y + ln.y * arrow_len,
                                                      L.position.z + ln.z * arrow_len };
                    const auto p_centre = project(L.position);
                    const auto p_tip = project(arrow_tip);
                    if (p_centre.x >= 0.0F && p_tip.x >= 0.0F)
                    {
                        dl_m->AddLine(p_centre, p_tip, use_col, sel ? 3.0F : 2.0F);
                        // Tiny circle at tip = arrow head substitute.
                        dl_m->AddCircleFilled(p_tip, sel ? 5.0F : 3.5F, use_col);
                    }
                }
                break;
            }
        }
    }
}

// =============================================================================
// Phase 302 / Marathon Run 8 sub-N2F: Command Palette popup extracted. Small
// (~50 lines) but the last of the genuinely self-contained UI panel-style
// regions in the main render loop. Gizmo + R-Showcase remain and need a
// dedicated phase each (gizmo carries deep drag-state coupling, R-Showcase
// needs an FxState struct refactor for its 26-knob parameter surface).
// =============================================================================

// ---- draw_command_palette_popup -------------------------------------------
// Modal-ish floating popup centred horizontally near the top of the
// viewport. Shows filtered command labels from cd::editor::CommandPalette;
// Enter or Selectable click invokes the highlighted entry and dismisses
// the popup. Caller manages palette_visible + palette_query through key
// events; this helper only handles the per-frame draw + invoke.
inline void draw_command_palette_popup(cd::editor::CommandPalette& palette,
                                       bool& palette_visible,
                                       std::string& palette_query,
                                       cd::rhi::Extent2D extent)
{
    if (palette_visible)
    {
        const auto vw_p = static_cast<float>(extent.width);
        const float pw = 520.0F;
        const float ph = 360.0F;
        ImGui::SetNextWindowPos(ImVec2((vw_p - pw) * 0.5F, 80.0F), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(pw, ph), ImGuiCond_Always);
        if (ImGui::Begin(
                "Command Palette",
                &palette_visible,
                ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings |
                    ImGuiWindowFlags_NoDocking
            ))
        {
            if (ImGui::IsWindowAppearing())
                ImGui::SetKeyboardFocusHere();
            char buf[128] {};
            std::snprintf(buf, sizeof(buf), "%s", palette_query.c_str());
            if (ImGui::InputText("##q", buf, sizeof(buf)))
                palette_query = buf;
            ImGui::Separator();
            const auto hits = palette.filter(palette_query);
            if (hits.empty())
            {
                ImGui::TextDisabled("no match (%zu commands)", palette.size());
            }
            else
            {
                for (std::size_t i = 0; i < hits.size() && i < 24; ++i)
                {
                    const auto& e = palette.at(hits[i]);
                    char row[160] {};
                    std::snprintf(row, sizeof(row), "  %s", e.label.c_str());
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
}

// =============================================================================
// Phase 295 / Marathon Run 7 sub-N1G: scene-bootstrap helpers extracted
// from main(). These are sample-local (operate on the anon-namespace
// SceneEntity / PrimitiveKind) so they live in main.cpp rather than a
// library header. main() shrinks and the boot region reads as four
// named call sites instead of three nested initializer blocks.
// =============================================================================

// ---- setup_world_container ------------------------------------------------
// phase1146 (on_boot extraction batch 7): moved VERBATIM to
// StudioBootProject.hpp (cd_studio) — the file-scope using-decl above keeps
// the unqualified callsites compiling unchanged.

// ---- spawn_primitive_seeds -----------------------------------------------
// Spawn the original 5-primitive showcase row (Cube/Sphere/Cone/
// Cylinder/Torus) across x=-2.4..+2.4 at y=0. The saturated artistic
// palette preserves enough off-channel content for each tint to read
// distinctly without going to pure RGB.
inline void spawn_primitive_seeds(cd::scene::Scene& scene,
                                  std::vector<SceneEntity>& entities)
{
    struct Seed
    {
        const char* name {};
        cd::math::Vec3f pos;
        cd::math::Vec3f tint;
        PrimitiveKind k {};
    };
    const std::array<Seed, 5> seeds {
        {
            { "Cube",     { -2.4F, 0.0F, 0.0F }, { 1.00F, 0.10F, 0.10F }, PrimitiveKind::kCube     },
            { "Sphere",   { -1.2F, 0.0F, 0.0F }, { 0.20F, 0.95F, 0.30F }, PrimitiveKind::kSphere   },
            { "Cone",     {  0.0F, 0.0F, 0.0F }, { 0.15F, 0.40F, 1.00F }, PrimitiveKind::kCone     },
            { "Cylinder", {  1.2F, 0.0F, 0.0F }, { 1.00F, 0.75F, 0.15F }, PrimitiveKind::kCylinder },
            { "Torus",    {  2.4F, 0.0F, 0.0F }, { 0.90F, 0.15F, 0.90F }, PrimitiveKind::kTorus    },
        }
    };
    for (const auto& s : seeds)
    {
        SceneEntity e;
        e.handle = scene.create_node();
        e.name = s.name;
        e.tint = s.tint;
        e.kind = s.k;
        scene.local(e.handle)->value.position = s.pos;
        entities.push_back(std::move(e));
    }
}

// ---- spawn_sponza_and_cesiumman_entities ---------------------------------
// Spawn two glTF entities:
//   1. Sponza Atrium (kSponza) — static scene, identity rotation (glTF
//      Khronos Sponza is already Y-up), scale 0.01 to convert raw
//      centimeter vertices to meters. Placed at origin.
//   2. CesiumMan (kGltf) — animated character inside the nave. Z-up
//      -> Y-up correction (-90 deg X) applied because CesiumMan.glb
//      is authored in Z-up.  Falls back to a procedural Earth sphere
//      when CesiumMan was not found on disk.
//
// When neither Sponza nor CesiumMan is available the old single Earth
// fallback entity is produced (kGltf, procedural).
inline void spawn_sponza_and_cesiumman_entities(cd::scene::Scene& scene,
                                                const GpuMesh& sponza_mesh,
                                                std::string_view sponza_loaded_name,
                                                const GpuMesh& cesium_mesh,
                                                std::string_view cesium_loaded_name,
                                                std::vector<SceneEntity>& entities)
{
    // --- Sponza entity (kSponza) ---
    if (sponza_mesh.vb.is_valid())
    {
        SceneEntity es;
        es.handle = scene.create_node();
        es.name   = std::string { "Sponza (" } + std::string { sponza_loaded_name } + ")";
        es.tint   = { 1.0F, 1.0F, 1.0F };
        es.kind   = PrimitiveKind::kSponza;
        // Sponza vertices are in centimetres (glTF node carries 0.008 scale
        // which our loader ignores at vertex-read time).  0.01 brings it to
        // a metre-scale room that the default camera sees comfortably.
        // Identity rotation: Khronos Sponza glTF is Y-up, no correction needed.
        scene.local(es.handle)->value.position = { 0.0F, 0.0F, 0.0F };
        scene.local(es.handle)->value.scale    = { 0.01F, 0.01F, 0.01F };
        // Identity quaternion (w=1) — no rotation.
        scene.local(es.handle)->value.rotation = { 0.0F, 0.0F, 0.0F, 1.0F };
        entities.push_back(std::move(es));
    }

    // --- CesiumMan entity (kGltf) ---
    {
        SceneEntity ec;
        ec.handle = scene.create_node();
        ec.tint   = { 1.0F, 1.0F, 1.0F };
        ec.kind   = PrimitiveKind::kGltf;
        if (cesium_mesh.vb.is_valid())
        {
            ec.name = std::string { "CesiumMan (" } + std::string { cesium_loaded_name } + ")";
            // phase832-default-view-unblock-pbr-grid:
            // CesiumMan was at world origin (0, 0, 0) which sat **directly
            // in front of** the phase799-relocated PBR demo grid (at Z=0,
            // X∈[-1.58,+1.58], Y∈[0.55,3.70]). The default M2 camera at
            // (0, 2.5, +8) → (0, 1, 0) saw CesiumMan filling the centre
            // of the view; the chrome grid behind it was invisible at
            // app launch.
            //
            // Move CesiumMan to world (-4, 0, -1.5) — off to the entrance
            // (-X) side of the nave, slightly toward the back wall. The
            // character still inhabits Sponza, still casts shadows, still
            // reflects on chrome (it is in the TLAS), but it no longer
            // occludes the front of the grid from the default camera.
            scene.local(ec.handle)->value.position = { -4.0F, 0.0F, -1.5F };
            scene.local(ec.handle)->value.scale    = { 2.2F, 2.2F, 2.2F };
            // X -90 deg rotation (CesiumMan.glb is Z-up -> engine Y-up).
            scene.local(ec.handle)->value.rotation = { -0.7071068F, 0.0F, 0.0F, 0.7071068F };
        }
        else if (!sponza_mesh.vb.is_valid())
        {
            // No Sponza and no CesiumMan: procedural Earth fallback.
            ec.name = "Earth (procedural showcase)";
            scene.local(ec.handle)->value.position = { -4.5F, 0.7F, 0.0F };
            scene.local(ec.handle)->value.scale    = { 1.5F, 1.5F, 1.5F };
            // Identity rotation for procedural Earth.
            scene.local(ec.handle)->value.rotation = { 0.0F, 0.0F, 0.0F, 1.0F };
        }
        else
        {
            // Sponza loaded but no CesiumMan: skip the kGltf entity to avoid
            // an empty mesh placeholder.
            return;
        }
        entities.push_back(std::move(ec));
    }
}

// ---- spawn_chrome_probe_entity --------------------------------------------
// phase797-rt-chrome-sponza-probe: spawn ONE chrome sphere at the world
// position locked in SponzaFixtures.hpp (kChromeProbePosition). This
// sphere is the iteration anchor for the chrome-reflection golden loop:
// the fixture-#5 camera (`chrome_probe` slug) frames it with curtains
// + columns behind, and `--golden-fixture 5 --golden-out X.png` writes
// a deterministic PNG that I (the agent) can read back and verify.
//
// The probe is only spawned when fixture #5 is active so the normal
// chromodynamic_studio scene stays byte-identical for every other fixture and
// every interactive run. Metallic=1, roughness=0.04 (near-mirror) so
// the SSBO reflection branch dominates and any RT-energy bug is
// immediately visible on the captured PNG.
inline void spawn_chrome_probe_entity(cd::scene::Scene&         scene,
                                      std::vector<SceneEntity>& entities)
{
    SceneEntity e;
    e.handle    = scene.create_node();
    e.name      = "ChromeProbe (phase797)";
    e.kind      = PrimitiveKind::kSphere;
    e.tint      = cd::math::Vec3f { 0.95F, 0.93F, 0.88F };  // matches PBR grid chrome albedo
    e.is_pbr    = true;
    e.metallic  = 1.0F;
    e.roughness = 0.04F;
    scene.local(e.handle)->value.position = {
        cd::studio_rt::sponza_fixtures::kChromeProbePosition[0],
        cd::studio_rt::sponza_fixtures::kChromeProbePosition[1],
        cd::studio_rt::sponza_fixtures::kChromeProbePosition[2],
    };
    const float k = cd::studio_rt::sponza_fixtures::kChromeProbeScale;
    scene.local(e.handle)->value.scale    = { k, k, k };
    entities.push_back(std::move(e));
}

// ---- spawn_pbr_grid_entities ---------------------------------------------
// Consume the 4x4 PbrGridSlot array from StudioPbrSphereGrid.hpp and assemble
// one SceneEntity per slot. Sample-local SceneEntity / PrimitiveKind
// assembly stays here; pure data + math lives in the header.
inline void spawn_pbr_grid_entities(cd::scene::Scene& scene,
                                    std::vector<SceneEntity>& entities)
{
    // phase999-pbr-default-texture-off (was phase983): spawn the PBR
    // grid with EVERY sphere starting on the procedural-color path.
    // The user opts into the albedo texture path per-entity via the
    // Inspector "Albedo texture (PBR opt-in)" checkbox or grid-wide
    // via the R-Showcase R2 panel buttons. See phase999 comment in
    // the loop body for the user-feedback rationale.
    for (const auto& slot : cd::studio_rt::build_pbr_demo_grid())
    {
        SceneEntity e;
        e.handle = scene.create_node();
        e.name = slot.name;
        e.kind = PrimitiveKind::kSphere;
        e.tint = slot.tint;
        e.is_pbr = true;
        e.metallic = slot.metallic;
        e.roughness = slot.roughness;
        // phase999-pbr-default-texture-off: user feedback (Run 26)
        // pointed out that auto-texturing the dielectric column makes
        // the non-metallic spheres look WRONG ("metalic olmayan
        // objelerde kendi textureleri varmış gibi gördüm"). Roll
        // back the default seed -- every PBR sphere starts with
        // use_texture = false (procedural color). The user opts in
        // per-entity via the Inspector "Albedo texture (PBR opt-in)"
        // checkbox or grid-wide via the R-Showcase R2 panel buttons
        // (Textured ALL / Textured RIGHT COL). kPbrGridTexturedColumn
        // constant in StudioPbrSphereGrid.hpp remains as the documented
        // default-on column for the RIGHT COL preset, but no longer
        // applied at spawn time.
        e.use_texture = false;
        scene.local(e.handle)->value.position = slot.position;
        scene.local(e.handle)->value.scale = { slot.scale, slot.scale, slot.scale };
        entities.push_back(std::move(e));
    }
}

// ---- upload_multi_light_ubo ------------------------------------------------
// Walks every enabled non-sun light and packs into the descriptor-bound UBO.
// Up to kMaxLights (8) slots; extras drop silently (caller can read the
// "lights_active" counter to see how many landed). Directional lights are
// driven through PrimPush.sun_dir, not the multi-light UBO -- pack_light_slot
// returns false for them so they're skipped automatically here.
inline void upload_multi_light_ubo(cd::rhi::IDevice& device,
                                   cd::rhi::BufferHandle lights_ubo,
                                   const std::vector<LightRow>& lights,
                                   cd::core::CounterTable& counters)
{
    LightUboGpu ubo {};
    ubo.count = 0;
    for (const auto& lrow : lights)
    {
        if (!lrow.enabled)
            continue;
        if (ubo.count >= cd::studio_rt::kMaxLights)
            break;
        if (!pack_light_slot(ubo.slots[ubo.count], lrow.light))
            continue;
        ++ubo.count;
    }
    static LightUboGpu s_prev_ubo {};
    static cd::rhi::BufferHandle s_prev_buf {};
    if (lights_ubo != s_prev_buf || std::memcmp(&ubo, &s_prev_ubo, sizeof(ubo)) != 0)
    {
        (void)device.upload_buffer(
            lights_ubo,
            0,
            std::span<const std::byte>(reinterpret_cast<const std::byte*>(&ubo), sizeof(ubo))
        );
        s_prev_ubo = ubo;
        s_prev_buf = lights_ubo;
    }
    counters.set("lights_active", ubo.count);
}

// GizmoMode, GizmoState, PrevCamBasis moved to file scope (before namespace {}).

// ---- SunLight + resolve_sun_light -----------------------------------------
// Per-frame extracted from main loop in phase 307 (N4B). The sun slot
// (direction + linear RGB + clamped strength + ambient hemisphere weight)
// is rebuilt every frame from the first enabled directional light. If no
// directional light is enabled, every field stays at the dark default so
// the scene fades to (near-)black -- user feedback: "isik yoksa golge
// yada isik beklemem".
struct SunLight
{
    cd::math::Vec3f dir { 0.0F, -1.0F, 0.0F };
    cd::math::Vec3f col { 0.0F, 0.0F, 0.0F };
    float strength { 0.0F };
    float ambient_w { 0.0F };
    bool has_sun { false };
};

inline SunLight resolve_sun_light(const std::vector<LightRow>& lights)
{
    SunLight s {};
    for (const auto& lrow : lights)
    {
        if (!lrow.enabled)
            continue;
        if (lrow.light.type != cd::light::LightType::kDirectional)
            continue;
        s.dir = lrow.light.direction;
        s.col = lrow.light.color;
        s.strength = std::min(2.5F, lrow.light.intensity / 80000.0F);
        // Sky hemisphere tied to sun being enabled: no sun, no sky bounce.
        s.ambient_w = 0.18F;
        s.has_sun = true;
        break;
    }
    return s;
}

// ---- fill_prim_push_shared --------------------------------------------------
// Floor + ECS entity draws each push a PrimPush, and the fx + sun + camera
// fields are identical between them (R-Showcase knobs + sun frame state +
// camera-pos for parallax). Pulled out in phase 308 (N4C-prep) so the
// downstream floor/ECS helpers only have to set the per-mesh fields
// (mvp / model / tint / per-entity fx_params4 [PBR vs BRDF]).
inline void fill_prim_push_shared(PrimPush& pp,
                                  const cd_studio::StudioRenderFx& fx,
                                  const SunLight& sun,
                                  const cd::camera::Camera& cam)
{
    pp.sun_dir[0] = sun.dir.x;
    pp.sun_dir[1] = sun.dir.y;
    pp.sun_dir[2] = sun.dir.z;
    pp.sun_dir[3] = sun.strength;
    pp.sun_color[0] = sun.col.x;
    pp.sun_color[1] = sun.col.y;
    pp.sun_color[2] = sun.col.z;
    pp.sun_color[3] = sun.ambient_w;
    pp.fx_params[0] = static_cast<float>(fx.tonemap_op);
    pp.fx_params[2] = fx.gtao_strength;
    pp.fx_params[3] = fx.bloom_strength;
    pp.fx_params2[0] = fx.smaa_strength;
    pp.fx_params2[1] = fx.motion_blur;
    pp.fx_params2[2] = fx.taa_amount;
    pp.fx_params2[3] = fx.dof_strength;
    pp.fx_params3[0] = fx.fog_density;
    pp.fx_params3[1] = fx.aerial_perspective;
    pp.fx_params3[2] = fx.clouds_coverage;
    pp.fx_params3[3] = fx.light_shafts;
    pp.camera_pos[0] = cam.eye.x;
    pp.camera_pos[1] = cam.eye.y;
    pp.camera_pos[2] = cam.eye.z;
    pp.camera_pos[3] = 0.0F;
}

// ---- draw_sky_pass --------------------------------------------------------
// Rebuild the camera-space basis (fwd / right / up + half-extents),
// fill cd::material::AnalyticalSkyPush, then apply the sky material +
// push constants + draw the fullscreen triangle. Tints the sky with the
// directional sun_col at full sky-tint blend so the CCT slider in the
// Lights panel propagates to the sky too.
inline void draw_sky_pass(cd::rhi::IDrawRecorder& cmd,
                          const cd::camera::Camera& cam,
                          float aspect,
                          const SunLight& sun,
                          cd::material::Material& sky_material)
{
    cd::math::Vec3f forward { cam.target.x - cam.eye.x,
                              cam.target.y - cam.eye.y,
                              cam.target.z - cam.eye.z };
    const float fl = std::sqrt(forward.x * forward.x + forward.y * forward.y + forward.z * forward.z);
    forward.x /= fl;
    forward.y /= fl;
    forward.z /= fl;
    constexpr cd::math::Vec3f world_up { 0.0F, 1.0F, 0.0F };
    cd::math::Vec3f sky_right { forward.y * world_up.z - forward.z * world_up.y,
                                forward.z * world_up.x - forward.x * world_up.z,
                                forward.x * world_up.y - forward.y * world_up.x };
    const float rl = std::sqrt(sky_right.x * sky_right.x + sky_right.y * sky_right.y + sky_right.z * sky_right.z);
    sky_right.x /= rl;
    sky_right.y /= rl;
    sky_right.z /= rl;
    const cd::math::Vec3f sky_up { sky_right.y * forward.z - sky_right.z * forward.y,
                                   sky_right.z * forward.x - sky_right.x * forward.z,
                                   sky_right.x * forward.y - sky_right.y * forward.x };
    const float half_h = std::tan(cam.fov_y * 0.5F);
    const float half_w = half_h * aspect;

    cd::material::AnalyticalSkyPush spush {};
    spush.cam_right[0] = sky_right.x;
    spush.cam_right[1] = sky_right.y;
    spush.cam_right[2] = sky_right.z;
    spush.cam_right[3] = half_w;
    spush.cam_up[0] = sky_up.x;
    spush.cam_up[1] = sky_up.y;
    spush.cam_up[2] = sky_up.z;
    spush.cam_up[3] = half_h;
    spush.cam_fwd[0] = forward.x;
    spush.cam_fwd[1] = forward.y;
    spush.cam_fwd[2] = forward.z;
    spush.cam_fwd[3] = 0.0F;
    spush.sun_dir[0] = sun.dir.x;
    spush.sun_dir[1] = sun.dir.y;
    spush.sun_dir[2] = sun.dir.z;
    spush.sun_dir[3] = sun.strength;
    spush.sun_color[0] = sun.col.x;
    spush.sun_color[1] = sun.col.y;
    spush.sun_color[2] = sun.col.z;
    spush.sun_color[3] = 1.0F;  // full sky-tint blend
    sky_material.apply(cmd);
    cmd.push_constants(
        sky_material.pipeline_layout(),
        cd::rhi::ShaderStage::kVertex | cd::rhi::ShaderStage::kFragment,
        0,
        sizeof(spush),
        &spush
    );
    cmd.draw(3, 1, 0, 0);
}

// ---- draw_planar_shadows ---------------------------------------------------
// Faz 1.5 planar projective shadows: each enabled caster ECS entity is
// flattened onto the floor (y = floor_y) via make_planar_shadow_matrix
// (taken from cd::render::PlanarShadow, Run 7 N1D) and re-drawn with
// the tint.w=0 sentinel that triggers the prim FS shadow-bypass path
// (flat dark output, no lighting).
//
// Parallel-prep / serial-draw split (X1E pattern, phase 287): each
// shadow PrimPush is built in parallel into a pre-sized scratch
// vector; the draw pass remains serial because Vulkan command-buffer
// recording is not thread-safe per buffer (ADR-015).
//
// The MeshFor template lets the helper accept main()'s `mesh_for`
// lambda without dragging the GpuMesh registry into a public header.
template <typename MeshFor>
inline void draw_planar_shadows(cd::rhi::IDrawRecorder& cmd,
                                const SunLight& sun,
                                float floor_y,
                                float shadow_lift,
                                const std::vector<SceneEntity>& entities,
                                const cd::scene::Scene& scene,
                                const cd::math::Mat4f& vp,
                                cd::material::Material& prim_material,
                                cd::core::CounterTable& counters,
                                const MeshFor& mesh_for)
{
    // Skip when sun is disabled or pointing upward.
    // DeMorgan with per-term negation (NOT operator flips) so NaN
    // comparisons keep their exact pre-rewrite behaviour.
    if (!(sun.strength > 1e-4F) || !(sun.dir.y < -1e-3F))
        return;
    const auto S = make_planar_shadow_matrix(sun.dir, floor_y, shadow_lift);
    PrimPush sp {};
    // Shadow tint: tint.w < 0.5 triggers shader bypass; rgb is the shadow
    // color (linear, post-tonemap output).
    sp.tint[0] = 0.04F;
    sp.tint[1] = 0.04F;
    sp.tint[2] = 0.05F;
    sp.tint[3] = 0.0F;
    // Zero out lighting fields - shadow path doesn't read them but keep the
    // push deterministic for SPIR-V validators.
    sp.sun_dir[0] = sp.sun_dir[1] = sp.sun_dir[2] = sp.sun_dir[3] = 0.0F;
    sp.sun_color[0] = sp.sun_color[1] = sp.sun_color[2] = sp.sun_color[3] = 0.0F;
    sp.fx_params[0] = sp.fx_params[1] = sp.fx_params[2] = sp.fx_params[3] = 0.0F;
    sp.fx_params2[0] = sp.fx_params2[1] = sp.fx_params2[2] = sp.fx_params2[3] = 0.0F;
    sp.fx_params3[0] = sp.fx_params3[1] = sp.fx_params3[2] = sp.fx_params3[3] = 0.0F;
    sp.camera_pos[0] = sp.camera_pos[1] = sp.camera_pos[2] = sp.camera_pos[3] = 0.0F;
    sp.fx_params4[0] = sp.fx_params4[1] = sp.fx_params4[2] = sp.fx_params4[3] = 0.0F;

    std::vector<PrimPush> plan_push(entities.size());
    std::vector<std::uint8_t> plan_valid(entities.size(), 0u);
    auto prep_planar_one = [&](std::size_t i)
    {
        const auto& ent = entities[i];
        if (!ent.visible || ent.kind == PrimitiveKind::kSponza)
            return;
        const auto& mesh = mesh_for(ent.kind);
        if (!mesh.vb.is_valid())
            return;
        auto* lt = scene.local(ent.handle);
        if (lt == nullptr)
            return;
        // Skip large ground/floor slabs sitting at or near floor_y to avoid
        // full-screen planar shadow self-overdraw.
        if ((lt->value.scale.x >= 10.0F && lt->value.scale.z >= 10.0F) ||
            ent.name.find("Floor") != std::string::npos)
            return;
        const auto model = cd::math::to_mat4(lt->value);
        const auto shadow_model = S * model;
        PrimPush& dst = plan_push[i];
        dst = sp;
        dst.mvp = vp * shadow_model;
        dst.model = shadow_model;
        plan_valid[i] = 1u;
    };
    if (entities.size() >= 256u)
    {
        cd::concurrency::parallel_for(std::size_t { 0 }, entities.size(), prep_planar_one);
    }
    else
    {
        for (std::size_t i = 0; i < entities.size(); ++i)
            prep_planar_one(i);
    }
    for (std::size_t i = 0; i < entities.size(); ++i)
    {
        if (plan_valid[i] == 0u)
            continue;
        const auto& ent = entities[i];
        const auto& mesh = mesh_for(ent.kind);
        cmd.bind_vertex_buffer(0, mesh.vb, 0);
        cmd.bind_index_buffer(mesh.ib, 0, mesh.index_type);
        cmd.push_constants(
            prim_material.pipeline_layout(),
            cd::rhi::ShaderStage::kVertex | cd::rhi::ShaderStage::kFragment,
            0,
            sizeof(PrimPush),
            &plan_push[i]
        );
        cmd.draw_indexed(mesh.index_count, 1, 0, 0, 0);
        counters.increment("draws_shadow");
    }
}

// ---- draw_shadow_map_pass --------------------------------------------------
// Faz 1.6 CSM (single-cascade variant): each enabled directional-light
// sun position drives an orthographic light_vp; entities are re-drawn
// into the depth-only shadow_target via shadow_material; main pass
// later samples this depth attachment with PCF + bias.
// shadow_initialised_on_gpu tracks the once-per-process first-frame
// transition; main owns the bool so re-creating the renderer or window
// can reset it cleanly.
template <typename MeshFor>
inline void draw_shadow_map_pass(cd::rhi::ICommandBuffer& cmd,
                                 const SunLight& sun,
                                 cd::rhi::IDevice& device,
                                 cd::rhi::BufferHandle shadow_ubo,
                                 const cd::framegraph::DepthTarget& shadow_target,
                                 bool& shadow_initialised_on_gpu,
                                 cd::material::Material& shadow_material,
                                 cd::rhi::Extent2D shadow_map_size,
                                 const std::vector<SceneEntity>& entities,
                                 const cd::scene::Scene& scene,
                                 const MeshFor& mesh_for,
                                 bool shadow_dirty = true)
{
    if (!shadow_dirty && shadow_initialised_on_gpu)
        return;

    cd::math::Vec3f sd = sun.has_sun ? sun.dir : cd::math::Vec3f { -0.4F, -0.9F, -0.2F };
    {
        const float sd_len = std::sqrt(sd.x * sd.x + sd.y * sd.y + sd.z * sd.z);
        if (sd_len > 1e-4F) { sd.x /= sd_len; sd.y /= sd_len; sd.z /= sd_len; }
        else { sd = { 0.0F, -1.0F, 0.0F }; }
    }
    // phase451-csm: ortho shrunk 80x80 -> 25x25 extent, far 100 -> 60,
    // eye dist 40 -> 25. The previous 80m extent was sized for "any
    // sun angle covers Sponza" but the actual scene (Sponza at 0.01
    // scale + entity cluster) fits in ±10m. Halving the extent gives
    // 4x shadow texel density (2048/25 ≈ 8cm tex vs 4cm prior) and
    // the depth range is 60m so the matching NDC bias of 0.00005
    // (phase 451 shader) is ~3mm world — small enough for cube /
    // character shadows to register on Sponza without overlap.
    const cd::math::Vec3f eye { -sd.x * 25.0F, -sd.y * 25.0F, -sd.z * 25.0F };
    const cd::math::Vec3f tgt { 0.0F, 0.0F, 0.0F };
    const cd::math::Vec3f up =
        (std::fabs(sd.y) > 0.99F) ? cd::math::Vec3f { 0.0F, 0.0F, 1.0F } : cd::math::Vec3f { 0.0F, 1.0F, 0.0F };
    const auto light_view = cd::math::look_at(eye, tgt, up);
    const auto light_proj = cd::math::ortho(-12.5F, 12.5F, -12.5F, 12.5F, 0.1F, 60.0F);
    const cd::math::Mat4f light_vp = light_proj * light_view;
    (void)device.upload_buffer(shadow_ubo, 0, std::span<const std::byte>(reinterpret_cast<const std::byte*>(&light_vp), sizeof(light_vp)));
    if (!shadow_initialised_on_gpu)
    {
        std::array<cd::rhi::TextureBarrier, 1> sb { cd::rhi::TextureBarrier { .texture = shadow_target.image, .from = cd::rhi::ResourceState::kUndefined, .to = cd::rhi::ResourceState::kDepthWrite, .range = { .base_mip = 0, .mip_count = 1, .base_layer = 0, .layer_count = 1 } } };
        cmd.barrier({}, sb);
        shadow_initialised_on_gpu = true;
    }
    else
    {
        std::array<cd::rhi::TextureBarrier, 1> sb { cd::rhi::TextureBarrier { .texture = shadow_target.image, .from = cd::rhi::ResourceState::kShaderResource, .to = cd::rhi::ResourceState::kDepthWrite, .range = { .base_mip = 0, .mip_count = 1, .base_layer = 0, .layer_count = 1 } } };
        cmd.barrier({}, sb);
    }
    {
        cd::rhi::DepthStencilAttachmentInfo sda {};
        sda.view = shadow_target.view;
        sda.depth_load = cd::rhi::LoadOp::kClear;
        sda.depth_store = cd::rhi::StoreOp::kStore;
        sda.clear.depth = 1.0F;
        cd::rhi::RenderPassBeginInfo srp {};
        srp.render_area = cd::rhi::Rect2D { { 0, 0 }, shadow_map_size };
        srp.color_attachments = {};
        srp.depth_stencil = &sda;
        cmd.begin_render_pass(srp);
        cmd.set_viewport(cd::rhi::Viewport { 0.0F, 0.0F, static_cast<float>(shadow_map_size.width), static_cast<float>(shadow_map_size.height), 0.0F, 1.0F });
        cmd.set_scissor(cd::rhi::Rect2D { { 0, 0 }, shadow_map_size });
        if (sun.has_sun)
        {
            shadow_material.apply(cmd);
            std::vector<cd::math::Mat4f> csm_light_mvp(entities.size());
            std::vector<std::uint8_t> csm_valid(entities.size(), 0u);
            auto prep_csm_one = [&](std::size_t i) {
                const auto& ent = entities[i];
                if (!ent.visible) return;
                if (ent.name.find("Ground_Floor") != std::string::npos &&
                    !ent.use_texture && ent.custom_texture_id == 0)
                    return;
                const auto& mesh = mesh_for(ent.kind);
                if (!mesh.vb.is_valid()) return;
                auto* lt = scene.local(ent.handle);
                if (lt == nullptr) return;
                const auto model = cd::math::to_mat4(lt->value);
                csm_light_mvp[i] = light_vp * model;
                csm_valid[i] = 1u;
            };
            if (entities.size() >= 256u)
            {
                cd::concurrency::parallel_for(std::size_t { 0 }, entities.size(), prep_csm_one);
            }
            else
            {
                for (std::size_t i = 0; i < entities.size(); ++i)
                    prep_csm_one(i);
            }
            for (std::size_t i = 0; i < entities.size(); ++i)
            {
                if (csm_valid[i] == 0u) continue;
                const auto& ent = entities[i];
                const auto& mesh = mesh_for(ent.kind);
                cmd.bind_vertex_buffer(0, mesh.vb, 0);
                cmd.bind_index_buffer(mesh.ib, 0, mesh.index_type);
                cmd.push_constants(shadow_material.pipeline_layout(), cd::rhi::ShaderStage::kVertex, 0, sizeof(cd::math::Mat4f), &csm_light_mvp[i]);
                cmd.draw_indexed(mesh.index_count, 1, 0, 0, 0);
            }
        }
        cmd.end_render_pass();
    }
    {
        std::array<cd::rhi::TextureBarrier, 1> sb { cd::rhi::TextureBarrier { .texture = shadow_target.image, .from = cd::rhi::ResourceState::kDepthWrite, .to = cd::rhi::ResourceState::kShaderResource, .range = { .base_mip = 0, .mip_count = 1, .base_layer = 0, .layer_count = 1 } } };
        cmd.barrier({}, sb);
    }
}

// ---- update_and_draw_gizmo --------------------------------------------------
// Editor gizmo overlay - translate / rotate / scale modes against either an
// ECS entity or a non-directional light. Heavy ImGui + cd::editor::AxisGizmo
// + EditHistory plumbing extracted from the render loop in phase 314
// (Marathon Run 10 N5, the largest single per-frame extraction in this
// marathon: ~690 lines body).
inline void update_and_draw_gizmo(cd::editor::AxisGizmo& gizmo,
                                  GizmoState& gizmo_state,
                                  bool& pending_pick,
                                  int selected,
                                  SelKind selected_kind,
                                  std::vector<SceneEntity>& entities,
                                  std::vector<LightRow>& lights,
                                  cd::scene::Scene& scene,
                                  cd::editor::EditHistory& history,
                                  const std::function<void(std::string)>& log_push,
                                  const cd::math::Mat4f& vp,
                                  const cd::camera::Camera& cam,
                                  cd::platform::IWindow& window,
                                  cd::rhi::Extent2D extent)
{
    // ---- Phase 152 - axis-translation gizmo (ImGui overlay) ----
    // Project the selected entity's world position to screen,
    // draw three colored axis arrows, do hover/click drag in
    // screen-space, map back into world delta along the active
    // axis, and push a TranslateCommand on release.
    // Gizmo target can be either an entity transform OR a light's
    // position. The lambda below makes the same draw + drag code
    // path applicable to both - point/spot/area lights drag their
    // position; directional lights have no world position so they
    // skip the gizmo.
    auto gizmo_target_pos = [&]() -> cd::math::Vec3f*
    {
        if (selected < 0)
            return nullptr;
        if (selected_kind == SelKind::kEntity)
        {
            if (std::cmp_greater_equal(selected,entities.size()))
                return nullptr;
            const auto& sel_ent = entities[static_cast<std::size_t>(selected)];
            if (!sel_ent.visible || sel_ent.locked)
                return nullptr;
            if (auto* lt = scene.local(sel_ent.handle))
                return &lt->value.position;
            return nullptr;
        }
        if (selected_kind == SelKind::kLight)
        {
            if (std::cmp_greater_equal(selected,lights.size()))
                return nullptr;
            auto& L = lights[static_cast<std::size_t>(selected)].light;
            if (L.type == cd::light::LightType::kDirectional)
                return nullptr;
            return &L.position;
        }
        return nullptr;
    };

    if (!gizmo_state.visible || gizmo_target_pos() == nullptr)
    {
        gizmo_state.was_hovered = false;
    }
    if (gizmo_state.visible && gizmo_target_pos() != nullptr)
    {
        cd::math::Vec3f* target_pos = gizmo_target_pos();
        // For entity targets, also need transform record for full
        // rotate/scale ops; for light targets, only position drag.
        const bool target_is_entity = (selected_kind == SelKind::kEntity);
        cd::ecs::Entity sel_ent =
            target_is_entity ? entities[static_cast<std::size_t>(selected)].handle : cd::ecs::Entity {};
        cd::scene::LocalTransform* lt = target_is_entity ? scene.local(sel_ent) : nullptr;
        if (target_pos != nullptr)
        {
            gizmo.set_target(*target_pos);
            const auto vw = static_cast<float>(extent.width);
            const auto vh = static_cast<float>(extent.height);
            const auto& tgt = gizmo.target();
            const cd::math::Vec4f wp_org { tgt.x, tgt.y, tgt.z, 1.0F };
            cd::math::Vec4f clip_org {};
            for (std::size_t r = 0; r < 4; ++r)
                clip_org[r] = vp[0][r] * wp_org[0] + vp[1][r] * wp_org[1] + vp[2][r] * wp_org[2] + vp[3][r] * wp_org[3];
            const float w_org = clip_org[3];
            auto project = [&](const cd::math::Vec3f& p) -> ImVec2
            {
                if (w_org <= 0.20F)
                    return { -1.0F, -1.0F };
                const cd::math::Vec4f wp { p.x, p.y, p.z, 1.0F };
                cd::math::Vec4f c {};
                for (std::size_t r = 0; r < 4; ++r)
                    c[r] = vp[0][r] * wp[0] + vp[1][r] * wp[1] + vp[2][r] * wp[2] + vp[3][r] * wp[3];
                if (c[3] <= 0.20F)
                    return { -1.0F, -1.0F };
                // Divide by w_org (the gizmo origin's clip w) rather than c[3] so
                // off-center gizmos project onto the local tangent plane at tgt
                // without false perspective deflection toward the screen center.
                const float sx = (c[0] / w_org * 0.5F + 0.5F) * vw;
                const float sy = (1.0F - (c[1] / w_org * 0.5F + 0.5F)) * vh;
                if (sx < -vw || sx > 2.0F * vw || sy < -vh || sy > 2.0F * vh)
                    return { -1.0F, -1.0F };
                return { sx, sy };
            };
            const float dx_cam = tgt.x - cam.eye.x;
            const float dy_cam = tgt.y - cam.eye.y;
            const float dz_cam = tgt.z - cam.eye.z;
            const float cam_dist = std::sqrt(dx_cam * dx_cam + dy_cam * dy_cam + dz_cam * dz_cam);
            const float inv_dist = (cam_dist > 1e-4F) ? (1.0F / cam_dist) : 0.0F;
            const float vx_dir = dx_cam * inv_dist;
            const float vy_dir = dy_cam * inv_dist;
            const float vz_dir = dz_cam * inv_dist;
            const float kAxisLen = std::clamp(cam_dist * 0.18F, 0.42F, 1.25F);
            const ImVec2 p_org = project(tgt);
            const ImVec2 p_x = project({ tgt.x + kAxisLen, tgt.y, tgt.z });
            const ImVec2 p_y = project({ tgt.x, tgt.y + kAxisLen, tgt.z });
            const ImVec2 p_z = project({ tgt.x, tgt.y, tgt.z + kAxisLen });
            auto axis_screen_len = [&](ImVec2 p) -> float
            {
                if (p.x < 0.0F)
                    return 0.0F;
                const float dx = p.x - p_org.x;
                const float dy = p.y - p_org.y;
                return std::sqrt(dx * dx + dy * dy);
            };
            const bool show_x = (p_x.x >= 0.0F) &&
                ((std::abs(vx_dir) < 0.92F && axis_screen_len(p_x) >= 14.0F) ||
                 gizmo.active_axis() == cd::editor::GizmoAxis::kX);
            const bool show_y = (p_y.x >= 0.0F) &&
                ((std::abs(vy_dir) < 0.92F && axis_screen_len(p_y) >= 14.0F) ||
                 gizmo.active_axis() == cd::editor::GizmoAxis::kY);
            const bool show_z = (p_z.x >= 0.0F) &&
                ((std::abs(vz_dir) < 0.92F && axis_screen_len(p_z) >= 14.0F) ||
                 gizmo.active_axis() == cd::editor::GizmoAxis::kZ);

            if (p_org.x >= 0.0F)
            {
                auto* dl = ImGui::GetBackgroundDrawList();
                auto axis_color_imgui = [](cd::editor::GizmoAxis a)
                {
                    const auto c = cd::editor::axis_color(a);
                    return ImGui::ColorConvertFloat4ToU32(ImVec4(c.x, c.y, c.z, 1.0F));
                };
                const ImU32 cx = axis_color_imgui(cd::editor::GizmoAxis::kX);
                const ImU32 cy = axis_color_imgui(cd::editor::GizmoAxis::kY);
                const ImU32 cz = axis_color_imgui(cd::editor::GizmoAxis::kZ);

                auto thick = [&](cd::editor::GizmoAxis a) -> float
                {
                    const bool all_active = (gizmo.hover() == cd::editor::GizmoAxis::kXYZ ||
                                             gizmo.active_axis() == cd::editor::GizmoAxis::kXYZ);
                    return (all_active || gizmo.hover() == a || gizmo.active_axis() == a) ? 5.0F : 3.0F;
                };

                if (gizmo_state.mode != GizmoMode::kRotate)
                {
                    if (show_x)
                        dl->AddLine(p_org, p_x, cx, thick(cd::editor::GizmoAxis::kX));
                    if (show_y)
                        dl->AddLine(p_org, p_y, cy, thick(cd::editor::GizmoAxis::kY));
                    if (show_z)
                        dl->AddLine(p_org, p_z, cz, thick(cd::editor::GizmoAxis::kZ));
                }
                const bool center_active = (gizmo.hover() == cd::editor::GizmoAxis::kXYZ ||
                                            gizmo.active_axis() == cd::editor::GizmoAxis::kXYZ);
                const ImU32 center_ring_col = ImGui::ColorConvertFloat4ToU32(
                    center_active ? ImVec4(1.0F, 0.88F, 0.25F, 1.0F)
                                  : ImVec4(0.35F, 0.92F, 0.98F, 0.90F));
                const ImU32 center_fill_col = ImGui::ColorConvertFloat4ToU32(
                    center_active ? ImVec4(1.0F, 0.88F, 0.25F, 1.0F)
                                  : ImVec4(0.95F, 0.96F, 0.98F, 0.95F));
                dl->AddCircle(p_org, 9.5F, center_ring_col, 24, center_active ? 2.5F : 1.8F);
                dl->AddCircleFilled(p_org, 5.0F, center_fill_col);

                // Arrowheads (filled triangles) + axis labels ("X", "Y", "Z").
                auto arrowhead_and_label = [&](ImVec2 from, ImVec2 to, ImU32 col, const char* lbl)
                {
                    const float dx = to.x - from.x;
                    const float dy = to.y - from.y;
                    const float len = std::sqrt(dx * dx + dy * dy);
                    if (len < 6.0F)
                        return;
                    const float nx = dx / len;
                    const float ny = dy / len;
                    const float sx = -ny;
                    const float sy = nx;
                    const float kHead = std::clamp(len * 0.28F, 5.5F, 11.0F);
                    const float kWing = kHead * 0.50F;
                    const ImVec2 a = to;
                    const ImVec2 b { to.x - nx * kHead + sx * kWing, to.y - ny * kHead + sy * kWing };
                    const ImVec2 c { to.x - nx * kHead - sx * kWing, to.y - ny * kHead - sy * kWing };
                    dl->AddTriangleFilled(a, b, c, col);
                    dl->AddText(ImVec2(to.x + nx * 8.0F - 4.0F, to.y + ny * 8.0F - 6.0F), col, lbl);
                };
                if (gizmo_state.mode == GizmoMode::kTranslate)
                {
                    if (show_x)
                        arrowhead_and_label(p_org, p_x, cx, "X");
                    if (show_y)
                        arrowhead_and_label(p_org, p_y, cy, "Y");
                    if (show_z)
                        arrowhead_and_label(p_org, p_z, cz, "Z");
                }
                else if (gizmo_state.mode == GizmoMode::kRotate)
                {
                    constexpr int kRingSeg = 48;
                    const float kRingRad = kAxisLen;
                    auto draw_ring = [&](cd::math::Vec3f u, cd::math::Vec3f v, ImU32 c, float t)
                    {
                        for (int i = 0; i < kRingSeg; ++i)
                        {
                            const float a = static_cast<float>(i) / kRingSeg * 2.0F * std::numbers::pi_v<float>;
                            const float b = static_cast<float>(i + 1) / kRingSeg * 2.0F * std::numbers::pi_v<float>;
                            const float ca0 = std::cos(a);
                            const float sa0 = std::sin(a);
                            const float cb0 = std::cos(b);
                            const float sb0 = std::sin(b);
                            cd::math::Vec3f wa { tgt.x + (u.x * ca0 + v.x * sa0) * kRingRad,
                                                 tgt.y + (u.y * ca0 + v.y * sa0) * kRingRad,
                                                 tgt.z + (u.z * ca0 + v.z * sa0) * kRingRad };
                            cd::math::Vec3f wb { tgt.x + (u.x * cb0 + v.x * sb0) * kRingRad,
                                                 tgt.y + (u.y * cb0 + v.y * sb0) * kRingRad,
                                                 tgt.z + (u.z * cb0 + v.z * sb0) * kRingRad };
                            const auto pa = project(wa);
                            const auto pb = project(wb);
                            if (pa.x >= 0.0F && pb.x >= 0.0F)
                                dl->AddLine(pa, pb, c, t);
                        }
                    };
                    const float th_x = (gizmo.hover() == cd::editor::GizmoAxis::kX) ? 4.0F : 2.0F;
                    const float th_y = (gizmo.hover() == cd::editor::GizmoAxis::kY) ? 4.0F : 2.0F;
                    const float th_z = (gizmo.hover() == cd::editor::GizmoAxis::kZ) ? 4.0F : 2.0F;
                    draw_ring({ 0, 1, 0 }, { 0, 0, 1 }, cx, th_x);
                    draw_ring({ 1, 0, 0 }, { 0, 0, 1 }, cy, th_y);
                    draw_ring({ 1, 0, 0 }, { 0, 1, 0 }, cz, th_z);
                }
                else  // kScale
                {
                    const auto cube_at = [&](ImVec2 c, ImU32 col, const char* lbl)
                    {
                        const ImVec2 a { c.x - 5, c.y - 5 };
                        const ImVec2 b { c.x + 5, c.y + 5 };
                        dl->AddRectFilled(a, b, col);
                        dl->AddText(ImVec2(c.x + 7.0F, c.y - 6.0F), col, lbl);
                    };
                    if (show_x)
                        cube_at(p_x, cx, "X");
                    if (show_y)
                        cube_at(p_y, cy, "Y");
                    if (show_z)
                        cube_at(p_z, cz, "Z");
                }
                // Mode label offset to upper-left of origin so it doesn't collide with +X/+Z arrows.
                const char* mode_lbl = "S";
                if (gizmo_state.mode == GizmoMode::kTranslate)
                    mode_lbl = "T";
                else if (gizmo_state.mode == GizmoMode::kRotate)
                    mode_lbl = "R";
                dl->AddText(
                    ImVec2(p_org.x - 14.0F, p_org.y - 16.0F),
                    ImGui::ColorConvertFloat4ToU32(ImVec4(1, 1, 1, 0.85F)),
                    mode_lbl
                );

                // Hover test.
                const ImVec2 mp = ImGui::GetIO().MousePos;
                auto dist_to_seg = [](ImVec2 a, ImVec2 b, ImVec2 p)
                {
                    const float dx = b.x - a.x;
                    const float dy = b.y - a.y;
                    const float L2 = dx * dx + dy * dy;
                    if (L2 < 1e-4F)
                        return std::sqrt((p.x - a.x) * (p.x - a.x) + (p.y - a.y) * (p.y - a.y));
                    const float t = std::clamp(((p.x - a.x) * dx + (p.y - a.y) * dy) / L2, 0.0F, 1.0F);
                    const float qx = a.x + t * dx;
                    const float qy = a.y + t * dy;
                    return std::sqrt((p.x - qx) * (p.x - qx) + (p.y - qy) * (p.y - qy));
                };
                cd::editor::GizmoAxis best = cd::editor::GizmoAxis::kNone;
                float best_d = gizmo.hover_tolerance_pixels;
                const float center_dist_px = std::hypot(mp.x - p_org.x, mp.y - p_org.y);
                if (center_dist_px <= 13.5F)
                {
                    // Center all-axis handle has strict priority over individual axis segments
                    // (which all originate at p_org and would otherwise steal center clicks).
                    best = cd::editor::GizmoAxis::kXYZ;
                    best_d = center_dist_px;
                }
                else if (gizmo_state.mode == GizmoMode::kRotate)
                {
                    constexpr int kHoverSeg = 48;
                    const float kHoverRad = kAxisLen;  // matches kRingRad above
                    auto ring_dist = [&](cd::math::Vec3f u, cd::math::Vec3f v) -> float
                    {
                        float min_d = std::numeric_limits<float>::infinity();
                        ImVec2 prev {};
                        bool prev_ok = false;
                        for (int i = 0; i <= kHoverSeg; ++i)
                        {
                            const float a = static_cast<float>(i) / kHoverSeg * 2.0F * std::numbers::pi_v<float>;
                            const float ca = std::cos(a);
                            const float sa = std::sin(a);
                            const cd::math::Vec3f w { tgt.x + (u.x * ca + v.x * sa) * kHoverRad,
                                                      tgt.y + (u.y * ca + v.y * sa) * kHoverRad,
                                                      tgt.z + (u.z * ca + v.z * sa) * kHoverRad };
                            const auto pw = project(w);
                            if (pw.x >= 0.0F)
                            {
                                if (prev_ok)
                                {
                                    const float d = dist_to_seg(prev, pw, mp);
                                    min_d = std::min(d, min_d);
                                }
                                prev = pw;
                                prev_ok = true;
                            }
                            else
                            {
                                prev_ok = false;
                            }
                        }
                        return min_d;
                    };
                    const float dx = ring_dist({ 0, 1, 0 }, { 0, 0, 1 });
                    const float dy = ring_dist({ 1, 0, 0 }, { 0, 0, 1 });
                    const float dz = ring_dist({ 1, 0, 0 }, { 0, 1, 0 });
                    if (dx < best_d)
                    {
                        best_d = dx;
                        best = cd::editor::GizmoAxis::kX;
                    }
                    if (dy < best_d)
                    {
                        best_d = dy;
                        best = cd::editor::GizmoAxis::kY;
                    }
                    if (dz < best_d)
                    {
                        best_d = dz;
                        best = cd::editor::GizmoAxis::kZ;
                    }
                }
                else  // translate / scale - axis-arrow hover
                {
                    if (show_x)
                    {
                        if (auto d = dist_to_seg(p_org, p_x, mp); d < best_d)
                        {
                            best_d = d;
                            best = cd::editor::GizmoAxis::kX;
                        }
                    }
                    if (show_y)
                    {
                        if (auto d = dist_to_seg(p_org, p_y, mp); d < best_d)
                        {
                            best_d = d;
                            best = cd::editor::GizmoAxis::kY;
                        }
                    }
                    if (show_z)
                    {
                        if (auto d = dist_to_seg(p_org, p_z, mp); d < best_d)
                        {
                            best_d = d;
                            best = cd::editor::GizmoAxis::kZ;
                        }
                    }
                }
                const bool over_imgui_ui =
                    cd::ui::chroma::state().mouse_in_any_window ||
                    !cd::ui::chroma::state().active_menu_name.empty() ||
                    cd::ui::chroma::state().open_popup_id != 0U ||
                    (ImGui::GetIO().WantCaptureMouse && ImGui::IsAnyItemHovered());
                if (over_imgui_ui && !gizmo.is_dragging())
                {
                    best = cd::editor::GizmoAxis::kNone;
                }
                gizmo.set_hover(best);
                gizmo_state.was_hovered = (best != cd::editor::GizmoAxis::kNone);
                // If the mouse is hovering an axis arrow AND a left-
                // click is pending from the OS event loop, the gizmo
                // wins over the 3D pick path - suppress the pick.
                if (pending_pick && best != cd::editor::GizmoAxis::kNone)
                {
                    pending_pick = false;
                }
                struct CamBasis3D
                {
                    cd::math::Vec3f fwd { 0.0F, 0.0F, -1.0F };
                    cd::math::Vec3f rgt { 1.0F, 0.0F,  0.0F };
                    cd::math::Vec3f up  { 0.0F, 1.0F,  0.0F };
                    bool valid { false };
                };
                auto compute_cam_basis = [&]() -> CamBasis3D
                {
                    CamBasis3D b {};
                    cd::math::Vec3f fwd { cam.target.x - cam.eye.x,
                                          cam.target.y - cam.eye.y,
                                          cam.target.z - cam.eye.z };
                    const float fl = std::sqrt(fwd.x * fwd.x + fwd.y * fwd.y + fwd.z * fwd.z);
                    if (fl < 1e-5F)
                        return b;
                    fwd.x /= fl;
                    fwd.y /= fl;
                    fwd.z /= fl;
                    const cd::math::Vec3f wup { 0.0F, 1.0F, 0.0F };
                    cd::math::Vec3f rgt { fwd.y * wup.z - fwd.z * wup.y,
                                          fwd.z * wup.x - fwd.x * wup.z,
                                          fwd.x * wup.y - fwd.y * wup.x };
                    const float rl = std::sqrt(rgt.x * rgt.x + rgt.y * rgt.y + rgt.z * rgt.z);
                    if (rl < 1e-5F)
                        return b;
                    rgt.x /= rl;
                    rgt.y /= rl;
                    rgt.z /= rl;
                    const cd::math::Vec3f up_v { rgt.y * fwd.z - rgt.z * fwd.y,
                                                 rgt.z * fwd.x - rgt.x * fwd.z,
                                                 rgt.x * fwd.y - rgt.y * fwd.x };
                    b.fwd   = fwd;
                    b.rgt   = rgt;
                    b.up    = up_v;
                    b.valid = true;
                    return b;
                };
                // Ray-plane projection onto the camera-facing view plane passing through `plane_org`.
                // Used by the center all-axis handle (kXYZ) to move objects freely in 3D (X, Y, Z).
                auto ray_view_plane_hit = [&](ImVec2 mouse_pixel,
                                              cd::math::Vec3f plane_org) -> std::optional<cd::math::Vec3f>
                {
                    const auto vw_w = static_cast<float>(window.width());
                    const auto vh_w = static_cast<float>(window.height());
                    if (vw_w < 1.0F || vh_w < 1.0F)
                        return std::nullopt;
                    const auto cb = compute_cam_basis();
                    if (!cb.valid)
                        return std::nullopt;
                    const float ndc_x = (2.0F * mouse_pixel.x / vw_w) - 1.0F;
                    const float ndc_y = 1.0F - (2.0F * mouse_pixel.y / vh_w);
                    const float tan_half = std::tan(cam.fov_y * 0.5F);
                    const float sx = (vw_w / vh_w) * tan_half;
                    const float sy = tan_half;
                    cd::math::Vec3f rdir { cb.fwd.x + cb.rgt.x * ndc_x * sx + cb.up.x * ndc_y * sy,
                                           cb.fwd.y + cb.rgt.y * ndc_x * sx + cb.up.y * ndc_y * sy,
                                           cb.fwd.z + cb.rgt.z * ndc_x * sx + cb.up.z * ndc_y * sy };
                    const float rdl = std::sqrt(rdir.x * rdir.x + rdir.y * rdir.y + rdir.z * rdir.z);
                    if (rdl < 1e-5F)
                        return std::nullopt;
                    rdir.x /= rdl;
                    rdir.y /= rdl;
                    rdir.z /= rdl;
                    const float denom = rdir.x * cb.fwd.x + rdir.y * cb.fwd.y + rdir.z * cb.fwd.z;
                    if (std::fabs(denom) < 1e-4F)
                        return std::nullopt;
                    const float t = ((plane_org.x - cam.eye.x) * cb.fwd.x +
                                     (plane_org.y - cam.eye.y) * cb.fwd.y +
                                     (plane_org.z - cam.eye.z) * cb.fwd.z) / denom;
                    if (t < 0.0F)
                        return std::nullopt;
                    return cd::math::Vec3f { cam.eye.x + rdir.x * t,
                                             cam.eye.y + rdir.y * t,
                                             cam.eye.z + rdir.z * t };
                };
                // Ray-plane projection of a screen pixel onto the
                // active axis. Returns the signed distance along
                // the axis from `world_start` to the hit point,
                // or std::optional() if the plane is too parallel
                // to the camera ray (caller falls back to the
                // screen-space dot method below). The plane is
                // the one containing the axis with normal
                // = normalize(cross(axis, cross(view, axis))) -
                // the most camera-facing orientation. Closes the
                // "gizmo ileri-geri yapinca objeler isinlaniyor"
                // teleport bug.
                auto ray_axis_offset = [&](cd::editor::GizmoAxis axis,
                                           ImVec2 mouse_pixel,
                                           cd::math::Vec3f world_start) -> std::optional<float>
                {
                    const auto vw_w = static_cast<float>(window.width());
                    const auto vh_w = static_cast<float>(window.height());
                    if (vw_w < 1.0F || vh_w < 1.0F)
                        return std::nullopt;
                    const auto cb = compute_cam_basis();
                    if (!cb.valid)
                        return std::nullopt;
                    const float ndc_x = (2.0F * mouse_pixel.x / vw_w) - 1.0F;
                    const float ndc_y = 1.0F - (2.0F * mouse_pixel.y / vh_w);
                    const float tan_half = std::tan(cam.fov_y * 0.5F);
                    const float sx = (vw_w / vh_w) * tan_half;
                    const float sy = tan_half;
                    cd::math::Vec3f rdir { cb.fwd.x + cb.rgt.x * ndc_x * sx + cb.up.x * ndc_y * sy,
                                           cb.fwd.y + cb.rgt.y * ndc_x * sx + cb.up.y * ndc_y * sy,
                                           cb.fwd.z + cb.rgt.z * ndc_x * sx + cb.up.z * ndc_y * sy };
                    const float rdl = std::sqrt(rdir.x * rdir.x + rdir.y * rdir.y + rdir.z * rdir.z);
                    if (rdl < 1e-5F)
                        return std::nullopt;
                    rdir.x /= rdl;
                    rdir.y /= rdl;
                    rdir.z /= rdl;
                    // Axis unit vector + plane normal.
                    cd::math::Vec3f a { 0, 0, 0 };
                    if (axis == cd::editor::GizmoAxis::kX)
                        a = { 1, 0, 0 };
                    else if (axis == cd::editor::GizmoAxis::kY)
                        a = { 0, 1, 0 };
                    else if (axis == cd::editor::GizmoAxis::kZ)
                        a = { 0, 0, 1 };
                    cd::math::Vec3f c1 { cb.fwd.y * a.z - cb.fwd.z * a.y,
                                         cb.fwd.z * a.x - cb.fwd.x * a.z,
                                         cb.fwd.x * a.y - cb.fwd.y * a.x };
                    cd::math::Vec3f n { a.y * c1.z - a.z * c1.y, a.z * c1.x - a.x * c1.z, a.x * c1.y - a.y * c1.x };
                    const float nl = std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
                    if (nl < 1e-5F)
                        return std::nullopt;
                    n.x /= nl;
                    n.y /= nl;
                    n.z /= nl;
                    const float denom = rdir.x * n.x + rdir.y * n.y + rdir.z * n.z;
                    if (std::fabs(denom) < 1e-4F)
                        return std::nullopt;
                    const float t = ((world_start.x - cam.eye.x) * n.x + (world_start.y - cam.eye.y) * n.y +
                                     (world_start.z - cam.eye.z) * n.z) /
                                    denom;
                    if (t < 0.0F)
                        return std::nullopt;
                    const cd::math::Vec3f hit { cam.eye.x + rdir.x * t,
                                                cam.eye.y + rdir.y * t,
                                                cam.eye.z + rdir.z * t };
                    return (hit.x - world_start.x) * a.x + (hit.y - world_start.y) * a.y +
                           (hit.z - world_start.z) * a.z;
                };

                if (!gizmo.is_dragging() && best != cd::editor::GizmoAxis::kNone &&
                    ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !over_imgui_ui)
                {
                    gizmo.begin_drag(best, *target_pos);
                    gizmo_state.drag_anchor = mp;
                    gizmo_state.drag_world_start = *target_pos;
                    if (lt != nullptr)
                    {
                        gizmo_state.drag_scale_start = lt->value.scale;
                        gizmo_state.drag_rot_start = lt->value.rotation;
                    }
                    // Capture light start state for gaps #16/#17:
                    // R-mode rotates light.direction; S-mode scales
                    // light.range / area_width / area_height.
                    if (selected_kind == SelKind::kLight && selected >= 0 &&
                        std::cmp_less(selected, lights.size()))
                    {
                        const auto& L = lights[static_cast<std::size_t>(selected)].light;
                        gizmo_state.light_drag_dir_start = L.direction;
                        gizmo_state.light_drag_tangent_start = L.area_tangent;
                        gizmo_state.light_drag_range_start = L.range;
                        gizmo_state.light_drag_area_w_start = L.area_width;
                        gizmo_state.light_drag_area_h_start = L.area_height;
                    }
                    // Capture the initial ray-plane axis offset (or view-plane hit for kXYZ)
                    // so subsequent moves give delta = current - initial (no jump at click).
                    if (best == cd::editor::GizmoAxis::kXYZ)
                    {
                        if (auto hit = ray_view_plane_hit(mp, *target_pos); hit.has_value())
                        {
                            gizmo_state.drag_plane_hit_start = *hit;
                            gizmo_state.drag_use_ray_plane = true;
                        }
                        else
                        {
                            gizmo_state.drag_plane_hit_start = *target_pos;
                            gizmo_state.drag_use_ray_plane = false;
                        }
                    }
                    else if (auto off = ray_axis_offset(best, mp, *target_pos); off.has_value())
                    {
                        gizmo_state.drag_initial_offset = *off;
                        gizmo_state.drag_use_ray_plane = true;
                    }
                    else
                    {
                        gizmo_state.drag_use_ray_plane = false;
                    }
                }
                if (gizmo.is_dragging())
                {
                    const bool is_xyz = (gizmo.active_axis() == cd::editor::GizmoAxis::kXYZ);
                    ImVec2 axis_screen_end = p_x;
                    if (gizmo.active_axis() == cd::editor::GizmoAxis::kY)
                        axis_screen_end = p_y;
                    else if (gizmo.active_axis() == cd::editor::GizmoAxis::kZ)
                        axis_screen_end = p_z;
                    const float ax_dx = axis_screen_end.x - p_org.x;
                    const float ax_dy = axis_screen_end.y - p_org.y;
                    const float ax_len_px = std::sqrt(ax_dx * ax_dx + ax_dy * ax_dy);
                    // Two paths: ray-plane (preferred, robust) vs
                    // screen-space dot (fallback for rotate/scale
                    // which use angular / exponential math).
                    float delta_world = 0.0F;
                    if (!is_xyz && gizmo_state.drag_use_ray_plane && gizmo_state.mode == GizmoMode::kTranslate)
                    {
                        if (auto off = ray_axis_offset(gizmo.active_axis(), mp, gizmo_state.drag_world_start);
                            off.has_value())
                        {
                            delta_world = *off - gizmo_state.drag_initial_offset;
                        }
                    }
                    if (is_xyz)
                    {
                        const float mouse_dx = mp.x - gizmo_state.drag_anchor.x;
                        const float mouse_dy = mp.y - gizmo_state.drag_anchor.y;
                        delta_world = (mouse_dx - mouse_dy) * 0.015F;
                    }
                    else if (ax_len_px > 1.0F)
                    {
                        // Screen-space path (rotate/scale, or
                        // ray-plane fallback). delta_world stays 0
                        // for translate when ray-plane worked.
                        const float nx = ax_dx / ax_len_px;
                        const float ny = ax_dy / ax_len_px;
                        const float mouse_dx = mp.x - gizmo_state.drag_anchor.x;
                        const float mouse_dy = mp.y - gizmo_state.drag_anchor.y;
                        const float dot_px = mouse_dx * nx + mouse_dy * ny;
                        const float world_per_px = kAxisLen / ax_len_px;
                        if (!gizmo_state.drag_use_ray_plane || gizmo_state.mode != GizmoMode::kTranslate)
                        {
                            delta_world = dot_px * world_per_px;
                        }
                    }
                    if (is_xyz || ax_len_px > 1.0F || gizmo_state.drag_use_ray_plane)
                    {
                        // Only translate works for both entities and
                        // lights; rotate/scale need a transform record
                        // and are gated on lt != nullptr.
                        switch (gizmo_state.mode)
                        {
                            case GizmoMode::kTranslate:
                            {
                                cd::math::Vec3f cur = gizmo_state.drag_world_start;
                                switch (gizmo.active_axis())
                                {
                                    case cd::editor::GizmoAxis::kX:
                                        cur.x += delta_world;
                                        break;
                                    case cd::editor::GizmoAxis::kY:
                                        cur.y += delta_world;
                                        break;
                                    case cd::editor::GizmoAxis::kZ:
                                        cur.z += delta_world;
                                        break;
                                    case cd::editor::GizmoAxis::kXYZ:
                                    {
                                        bool applied = false;
                                        if (gizmo_state.drag_use_ray_plane)
                                        {
                                            if (auto hit = ray_view_plane_hit(mp, gizmo_state.drag_world_start);
                                                hit.has_value())
                                            {
                                                cur.x += hit->x - gizmo_state.drag_plane_hit_start.x;
                                                cur.y += hit->y - gizmo_state.drag_plane_hit_start.y;
                                                cur.z += hit->z - gizmo_state.drag_plane_hit_start.z;
                                                applied = true;
                                            }
                                        }
                                        if (!applied)
                                        {
                                            const auto cb = compute_cam_basis();
                                            const float mouse_dx = mp.x - gizmo_state.drag_anchor.x;
                                            const float mouse_dy = mp.y - gizmo_state.drag_anchor.y;
                                            const float world_per_px = std::max(0.5F, cam_dist) * 0.0022F;
                                            cur.x += (cb.rgt.x * mouse_dx - cb.up.x * mouse_dy) * world_per_px;
                                            cur.y += (cb.rgt.y * mouse_dx - cb.up.y * mouse_dy) * world_per_px;
                                            cur.z += (cb.rgt.z * mouse_dx - cb.up.z * mouse_dy) * world_per_px;
                                        }
                                        break;
                                    }
                                    default:
                                        break;
                                }
                                *target_pos = cur;
                                gizmo.update_drag(cur);
                                break;
                            }
                            case GizmoMode::kScale:
                            {
                                const float factor = std::exp(delta_world * 0.5F);
                                if (lt != nullptr)
                                {
                                    cd::math::Vec3f cur = gizmo_state.drag_scale_start;
                                    switch (gizmo.active_axis())
                                    {
                                        case cd::editor::GizmoAxis::kX:
                                            cur.x *= factor;
                                            break;
                                        case cd::editor::GizmoAxis::kY:
                                            cur.y *= factor;
                                            break;
                                        case cd::editor::GizmoAxis::kZ:
                                            cur.z *= factor;
                                            break;
                                        case cd::editor::GizmoAxis::kXYZ:
                                            cur.x *= factor;
                                            cur.y *= factor;
                                            cur.z *= factor;
                                            break;
                                        default:
                                            break;
                                    }
                                    cur.x = std::max(cur.x, 0.05F);
                                    cur.y = std::max(cur.y, 0.05F);
                                    cur.z = std::max(cur.z, 0.05F);
                                    lt->value.scale = cur;
                                }
                                else if (selected_kind == SelKind::kLight && selected >= 0 &&
                                         std::cmp_less(selected, lights.size()))
                                {
                                    // gap #17: scale-mode gizmo on a
                                    // light edits its area-of-effect.
                                    // Point/Spot: range. Rect-area:
                                    // X=width, Y=height. Disk: width
                                    // (= radius in our convention).
                                    auto& L = lights[static_cast<std::size_t>(selected)].light;
                                    const auto axis = gizmo.active_axis();
                                    if (L.type == cd::light::LightType::kPoint ||
                                        L.type == cd::light::LightType::kSpot)
                                    {
                                        float r = gizmo_state.light_drag_range_start * factor;
                                        r = std::max(r, 0.1F);
                                        r = std::min(r, 200.0F);
                                        L.range = r;
                                    }
                                    else if (L.type == cd::light::LightType::kRectArea)
                                    {
                                        float w = gizmo_state.light_drag_area_w_start;
                                        float h = gizmo_state.light_drag_area_h_start;
                                        if (axis == cd::editor::GizmoAxis::kX || axis == cd::editor::GizmoAxis::kZ ||
                                            axis == cd::editor::GizmoAxis::kXYZ)
                                            w *= factor;
                                        if (axis == cd::editor::GizmoAxis::kY || axis == cd::editor::GizmoAxis::kZ ||
                                            axis == cd::editor::GizmoAxis::kXYZ)
                                            h *= factor;
                                        L.area_width = std::clamp(w, 0.05F, 50.0F);
                                        L.area_height = std::clamp(h, 0.05F, 50.0F);
                                    }
                                    else if (L.type == cd::light::LightType::kDiskArea)
                                    {
                                        float w = gizmo_state.light_drag_area_w_start * factor;
                                        L.area_width = std::clamp(w, 0.05F, 50.0F);
                                        L.area_height = L.area_width;  // radius
                                    }
                                }
                                break;
                            }
                            case GizmoMode::kRotate:
                            {
                                cd::math::Quatf q { 0, 0, 0, 1 };
                                if (gizmo.active_axis() == cd::editor::GizmoAxis::kXYZ)
                                {
                                    // Free 2-axis camera-aligned trackball rotation:
                                    // horizontal mouse drag yaws around camera up,
                                    // vertical mouse drag pitches around camera right.
                                    const auto cb = compute_cam_basis();
                                    const float mouse_dx = mp.x - gizmo_state.drag_anchor.x;
                                    const float mouse_dy = mp.y - gizmo_state.drag_anchor.y;
                                    const float yaw_ang   = mouse_dx * 0.012F;
                                    const float pitch_ang = mouse_dy * 0.012F;
                                    const float cos_y = std::cos(yaw_ang * 0.5F);
                                    const float sin_y = std::sin(yaw_ang * 0.5F);
                                    const cd::math::Quatf q_yaw {
                                        cb.up.x * sin_y, cb.up.y * sin_y, cb.up.z * sin_y, cos_y };
                                    const float cos_p = std::cos(pitch_ang * 0.5F);
                                    const float sin_p = std::sin(pitch_ang * 0.5F);
                                    const cd::math::Quatf q_pitch {
                                        cb.rgt.x * sin_p, cb.rgt.y * sin_p, cb.rgt.z * sin_p, cos_p };
                                    q = cd::math::Quatf {
                                        q_yaw.w * q_pitch.x + q_yaw.x * q_pitch.w + q_yaw.y * q_pitch.z - q_yaw.z * q_pitch.y,
                                        q_yaw.w * q_pitch.y - q_yaw.x * q_pitch.z + q_yaw.y * q_pitch.w + q_yaw.z * q_pitch.x,
                                        q_yaw.w * q_pitch.z + q_yaw.x * q_pitch.y - q_yaw.y * q_pitch.x + q_yaw.z * q_pitch.w,
                                        q_yaw.w * q_pitch.w - q_yaw.x * q_pitch.x - q_yaw.y * q_pitch.y - q_yaw.z * q_pitch.z
                                    };
                                }
                                else
                                {
                                    // Compute the angle the mouse has swept around the
                                    // gizmo center since drag start (atan2 difference).
                                    const float anchor_dx = gizmo_state.drag_anchor.x - p_org.x;
                                    const float anchor_dy = gizmo_state.drag_anchor.y - p_org.y;
                                    const float cur_dx = mp.x - p_org.x;
                                    const float cur_dy = mp.y - p_org.y;
                                    if (std::sqrt(anchor_dx * anchor_dx + anchor_dy * anchor_dy) < 5.0F)
                                        break;  // too close to center, ignore
                                    const float a_anchor = std::atan2(anchor_dy, anchor_dx);
                                    const float a_now = std::atan2(cur_dy, cur_dx);
                                    // W7-C: mouse coords have Y-down so atan2
                                    // gives a screen-CCW reading; world-space
                                    // convention is right-hand (CCW about +axis
                                    // looking from +axis toward origin). The
                                    // sign was therefore inverted — drag CW in
                                    // screen was producing positive (CCW)
                                    // rotation. Negate to match user intent.
                                    float ang = a_anchor - a_now;
                                    while (ang > std::numbers::pi_v<float>)
                                        ang -= 2.0F * std::numbers::pi_v<float>;
                                    while (ang < -std::numbers::pi_v<float>)
                                        ang += 2.0F * std::numbers::pi_v<float>;
                                    const float ca = std::cos(ang * 0.5F);
                                    const float sa = std::sin(ang * 0.5F);
                                    switch (gizmo.active_axis())
                                    {
                                        case cd::editor::GizmoAxis::kX:
                                            q = { sa, 0, 0, ca };
                                            break;
                                        case cd::editor::GizmoAxis::kY:
                                            q = { 0, sa, 0, ca };
                                            break;
                                        case cd::editor::GizmoAxis::kZ:
                                            q = { 0, 0, sa, ca };
                                            break;
                                        default:
                                            break;
                                    }
                                }
                                if (lt != nullptr)
                                {
                                    const auto& a = q;
                                    const auto& b = gizmo_state.drag_rot_start;
                                    lt->value.rotation =
                                        cd::math::Quatf { a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
                                                          a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
                                                          a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
                                                          a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z };
                                }
                                else if (selected_kind == SelKind::kLight && selected >= 0 &&
                                         std::cmp_less(selected, lights.size()))
                                {
                                    // gap #16: rotate-mode gizmo rotates
                                    // a light's direction. W8-N: also
                                    // rotates area_tangent so the rect's
                                    // local +X (uploaded to GPU as
                                    // slot.tangent.xyz) stays in sync —
                                    // each gizmo axis now drives an
                                    // independent rotation of the full
                                    // basis instead of just the normal.
                                    auto rotate_v = [&](cd::math::Vec3f v)
                                    {
                                        const cd::math::Vec3f t { q.w * v.x + q.y * v.z - q.z * v.y,
                                                                  q.w * v.y + q.z * v.x - q.x * v.z,
                                                                  q.w * v.z + q.x * v.y - q.y * v.x };
                                        const float tw = -(q.x * v.x + q.y * v.y + q.z * v.z);
                                        return cd::math::Vec3f { tw * -q.x + t.x * q.w + t.y * -q.z - t.z * -q.y,
                                                                 tw * -q.y - t.x * -q.z + t.y * q.w + t.z * -q.x,
                                                                 tw * -q.z + t.x * -q.y - t.y * -q.x + t.z * q.w };
                                    };
                                    auto norm_v = [](cd::math::Vec3f v)
                                    {
                                        const float l = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
                                        return (l > 1e-5F) ? cd::math::Vec3f { v.x / l, v.y / l, v.z / l } : v;
                                    };
                                    auto& Lr = lights[static_cast<std::size_t>(selected)].light;
                                    const auto rd_dir = rotate_v(gizmo_state.light_drag_dir_start);
                                    Lr.direction = norm_v(rd_dir);
                                    // Tangent rotates too (only meaningful
                                    // for area lights; harmless for spot /
                                    // point — direction-derived rendering
                                    // ignores tangent there).
                                    auto rt = rotate_v(gizmo_state.light_drag_tangent_start);
                                    // Re-orthogonalize tangent against new
                                    // direction to stay perpendicular.
                                    const auto dn = Lr.direction;
                                    const float pr = rt.x * dn.x + rt.y * dn.y + rt.z * dn.z;
                                    rt.x -= pr * dn.x;
                                    rt.y -= pr * dn.y;
                                    rt.z -= pr * dn.z;
                                    Lr.area_tangent = norm_v(rt);
                                }
                                break;
                            }
                        }
                        if (target_is_entity && lt != nullptr)
                        {
                            cd::studio::studio_world().home_transforms[sel_ent.id] = lt->value;
                            cd::studio::studio_world().euler_cache_deg[sel_ent.id] =
                                cd::studio::quat_to_euler_deg(lt->value.rotation);
                            scene.update_transforms();
                        }
                    }
                    if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
                    {
                        const auto delta = gizmo.end_drag();
                        (void)delta;
                        switch (gizmo_state.mode)
                        {
                            case GizmoMode::kTranslate:
                            {
                                const float dx = target_pos->x - gizmo_state.drag_world_start.x;
                                const float dy = target_pos->y - gizmo_state.drag_world_start.y;
                                const float dz = target_pos->z - gizmo_state.drag_world_start.z;
                                if (std::abs(dx) + std::abs(dy) + std::abs(dz) > 1e-4F)
                                {
                                    if (target_is_entity && lt != nullptr)
                                    {
                                        // Roll back live mutation + push undoable command.
                                        *target_pos = gizmo_state.drag_world_start;
                                        history.push(
                                            std::make_unique<cd::editor::TranslateCommand>(
                                                scene,
                                                sel_ent,
                                                cd::math::Vec3f { dx, dy, dz }
                                            )
                                        );
                                        log_push("[gizmo] entity translate (undoable)");
                                    }
                                    else
                                    {
                                        log_push("[gizmo] light translate applied");
                                    }
                                }
                                break;
                            }
                            case GizmoMode::kScale:
                                if (target_is_entity && lt != nullptr)
                                {
                                    const cd::math::Vec3f end_sc = lt->value.scale;
                                    const cd::math::Vec3f st_sc  = gizmo_state.drag_scale_start;
                                    if (std::abs(end_sc.x - st_sc.x) +
                                        std::abs(end_sc.y - st_sc.y) +
                                        std::abs(end_sc.z - st_sc.z) > 1e-4F)
                                    {
                                        const cd::math::Vec3f factor {
                                            (std::abs(st_sc.x) > 1e-5F) ? (end_sc.x / st_sc.x) : 1.0F,
                                            (std::abs(st_sc.y) > 1e-5F) ? (end_sc.y / st_sc.y) : 1.0F,
                                            (std::abs(st_sc.z) > 1e-5F) ? (end_sc.z / st_sc.z) : 1.0F
                                        };
                                        lt->value.scale = st_sc;
                                        history.push(std::make_unique<cd::editor::ScaleCommand>(
                                            scene, sel_ent, factor));
                                        log_push("[gizmo] entity scale (undoable)");
                                    }
                                }
                                break;
                            case GizmoMode::kRotate:
                                if (target_is_entity && lt != nullptr)
                                {
                                    const cd::math::Quatf end_rot = lt->value.rotation;
                                    const cd::math::Quatf st_rot  = gizmo_state.drag_rot_start;
                                    if (std::abs(end_rot.x - st_rot.x) +
                                        std::abs(end_rot.y - st_rot.y) +
                                        std::abs(end_rot.z - st_rot.z) +
                                        std::abs(end_rot.w - st_rot.w) > 1e-4F)
                                    {
                                        lt->value.rotation = st_rot;
                                        history.push(std::make_unique<cd::editor::RotateCommand>(
                                            scene, sel_ent, end_rot));
                                        log_push("[gizmo] entity rotate (undoable)");
                                    }
                                }
                                break;
                        }
                        if (target_is_entity && lt != nullptr)
                        {
                            cd::studio::studio_world().home_transforms[sel_ent.id] = lt->value;
                            cd::studio::studio_world().euler_cache_deg[sel_ent.id] =
                                cd::studio::quat_to_euler_deg(lt->value.rotation);
                            scene.update_transforms();
                        }
                    }
                }
            }
        }
    }
}

// ---- velocity_gbuffer_pass ------------------------------------------------
// R3 phase 227 velocity G-Buffer pass: re-draws every entity mesh into
// gbuf_velocity with velocity_material; FS writes (curr_uv - prev_uv).
// Depth attachment in kDepthRead so the velocity raster matches the HDR
// pass visible surface per pixel (no overdraw soup).
//
// Per-entity push: prev_vp * prev_model and curr_vp * curr_model (vp is
// always un-jittered so TAA jitter doesn't pollute the velocity output).
// Snapshots ent.prev_model on the way out so next frame can re-project.
//
// Barriers in/out: gbuf_velocity Color->Shader (with Undefined seed on
// first frame); depth DepthWrite->DepthRead before, DepthRead->Shader
// after, so composite can sample both.
template <typename MeshFor>
inline void velocity_gbuffer_pass(cd::rhi::ICommandBuffer& cmd,
                                  std::uint32_t frame_idx,
                                  const cd::framegraph::ColorTarget& gbuf_velocity,
                                  const cd::framegraph::DepthTarget& depth,
                                  cd::rhi::Extent2D extent,
                                  cd::material::Material& velocity_material,
                                  std::vector<SceneEntity>& entities,
                                  const cd::scene::Scene& scene,
                                  const cd::math::Mat4f& prev_vp_unjittered,
                                  const cd::math::Mat4f& vp_unjittered,
                                  const MeshFor& mesh_for,
                                  bool force_first_frame = false)
{
    // R3 phase 227 - velocity G-Buffer pass. Re-draws every entity
    // mesh using velocity_material; FS writes (curr_uv - prev_uv).
    // Depth attachment in kDepthRead so the velocity raster matches
    // the HDR pass's visible surface per pixel (no overdraw soup).
    {
        const cd::rhi::ResourceState vel_prev =
            (frame_idx == 0 || force_first_frame) ? cd::rhi::ResourceState::kUndefined : cd::rhi::ResourceState::kShaderResource;
        std::array<cd::rhi::TextureBarrier, 2> vb {
            cd::rhi::TextureBarrier { .texture = gbuf_velocity.image,
                                     .from = vel_prev,
                                     .to = cd::rhi::ResourceState::kColorAttachment,
                                     .range = { 0, 1, 0, 1 } },
            cd::rhi::TextureBarrier { .texture = depth.image,
                                     .from = cd::rhi::ResourceState::kDepthWrite,
                                     .to = cd::rhi::ResourceState::kDepthRead,
                                     .range = { 0, 1, 0, 1 } }
        };
        cmd.barrier({}, vb);

        std::array<cd::rhi::ColorAttachmentInfo, 1> vel_ca {
            cd::rhi::ColorAttachmentInfo { .view = gbuf_velocity.view,
                                          .load_op = cd::rhi::LoadOp::kClear,
                                          .store_op = cd::rhi::StoreOp::kStore,
                                          .clear_color = { .f32 = { 0.0F, 0.0F, 0.0F, 0.0F } } }
        };
        cd::rhi::DepthStencilAttachmentInfo vel_depth {};
        vel_depth.view = depth.view;
        vel_depth.depth_load = cd::rhi::LoadOp::kLoad;
        vel_depth.depth_store = cd::rhi::StoreOp::kStore;
        cd::rhi::RenderPassBeginInfo vel_rp {};
        vel_rp.render_area = cd::rhi::Rect2D {
            { 0, 0 },
            extent
        };
        vel_rp.color_attachments = vel_ca;
        vel_rp.depth_stencil = &vel_depth;
        cmd.begin_render_pass(vel_rp);
        cmd.set_viewport(
            cd::rhi::Viewport { 0.0F,
                                0.0F,
                                static_cast<float>(extent.width),
                                static_cast<float>(extent.height),
                                0.0F,
                                1.0F }
        );
        cmd.set_scissor(
            cd::rhi::Rect2D {
                { 0, 0 },
                extent
        }
        );
        velocity_material.apply(cmd);
        // Per-entity draw: push prev/curr vp*model, draw the same
        // mesh the HDR pass used. CesiumMan path: prim entities
        // share kPrimBindings layout so the velocity VS reads
        // their position attribute without a re-bind shape change.
        for (auto& ent : entities)
        {
            if (!ent.visible)
                continue;
            const auto& mesh = mesh_for(ent.kind);
            if (!mesh.vb.is_valid())
                continue;
            auto* lt = scene.local(ent.handle);
            if (lt == nullptr)
                continue;
            cmd.bind_vertex_buffer(0, mesh.vb, 0);
            cmd.bind_index_buffer(mesh.ib, 0, mesh.index_type);

            const auto curr_model_mat = cd::math::to_mat4(lt->value);
            const auto& prev_model_mat = ent.prev_model_valid ? ent.prev_model : curr_model_mat;

            struct VelocityPush
            {
                cd::math::Mat4f prev_vp_model;
                cd::math::Mat4f curr_vp_model;
            } vpush;

            vpush.prev_vp_model = prev_vp_unjittered * prev_model_mat;
            vpush.curr_vp_model = vp_unjittered * curr_model_mat;
            cmd.push_constants(
                velocity_material.pipeline_layout(),
                cd::rhi::ShaderStage::kVertex,
                0,
                sizeof(vpush),
                &vpush
            );
            cmd.draw_indexed(mesh.index_count, 1, 0, 0, 0);

            // Snapshot for next frame's velocity reprojection.
            ent.prev_model = curr_model_mat;
            ent.prev_model_valid = true;
        }
        cmd.end_render_pass();

        std::array<cd::rhi::TextureBarrier, 2> vb2 {
            cd::rhi::TextureBarrier { .texture = gbuf_velocity.image,
                                     .from = cd::rhi::ResourceState::kColorAttachment,
                                     .to = cd::rhi::ResourceState::kShaderResource,
                                     .range = { 0, 1, 0, 1 } },
            cd::rhi::TextureBarrier { .texture = depth.image,
                                     .from = cd::rhi::ResourceState::kDepthRead,
                                     .to = cd::rhi::ResourceState::kShaderResource,
                                     .range = { 0, 1, 0, 1 } }
        };
        cmd.barrier({}, vb2);
    }
}

// ---- run_bloom_chain ------------------------------------------------------
// R3 bloom: 7 fullscreen-triangle passes against the dedicated bloom mip
// chain - 1 prefilter (HDR -> mip0 with soft-knee threshold), 3 downsamples
// (mip0->1, 1->2, 2->3), 3 upsamples (mip3->2, 2->1, 1->0 with additive
// blend via LoadOp::kLoad). Each sub-pass barriers its target into
// ColorAttachment (or Undefined seed on first frame) then back to
// ShaderResource so the next pass can sample.
//
// All 7 invocations share the inner run_bloom_pass lambda; thresholds
// stay hardcoded as W4 visual baseline (1.10 threshold, 0.50 knee, radius
// 1.0 / intensity 1.0 per upsample level).
//
// Phase 511: `ev_stops` is the current scene auto-exposed EV (from
// `cd::post::exposure::Setup::current_ev()`). The prefilter shader scales
// its threshold + knee by exp2(-ev_stops), so the bloom highlight pick-off
// tracks the scene's auto-exposure. ev_stops = 0 (default) reproduces the
// pre-phase-511 behaviour exactly, because exp2(0) = 1.
inline void run_bloom_chain(cd::rhi::ICommandBuffer& cmd,
                            std::uint32_t frame_idx,
                            BloomMipChain& bloom_chain,
                            cd::material::Material& bloom_prefilter_material,
                            cd::material::MaterialInstance& bloom_prefilter_inst,
                            cd::material::Material& bloom_downsample_material,
                            std::array<cd::material::MaterialInstance, 3>& bloom_down_insts,
                            cd::material::Material& bloom_upsample_material,
                            std::array<cd::material::MaterialInstance, 3>& bloom_up_insts,
                            float ev_stops = 0.0F,
                            bool force_first_frame = false)
{
    auto run_bloom_pass = [&](cd::material::Material& mat,
                              cd::material::MaterialInstance& inst,
                              ColorTarget& dst,
                              cd::rhi::LoadOp load_op,
                              std::span<const std::byte> push_bytes,
                              bool first_frame)
    {
        std::array<cd::rhi::TextureBarrier, 1> tb {
            cd::rhi::TextureBarrier { .texture = dst.image,
                                     .from = first_frame ? cd::rhi::ResourceState::kUndefined
                                                          : cd::rhi::ResourceState::kShaderResource,
                                     .to = cd::rhi::ResourceState::kColorAttachment,
                                     .range = { 0, 1, 0, 1 } }
        };
        cmd.barrier({}, tb);

        std::array<cd::rhi::ColorAttachmentInfo, 1> ca {
            cd::rhi::ColorAttachmentInfo { .view = dst.view,
                                          .load_op = load_op,
                                          .store_op = cd::rhi::StoreOp::kStore,
                                          .clear_color = { .f32 = { 0.0F, 0.0F, 0.0F, 1.0F } } }
        };
        cd::rhi::RenderPassBeginInfo rp {};
        rp.render_area = cd::rhi::Rect2D { { 0, 0 }, dst.extent };
        rp.color_attachments = ca;
        rp.depth_stencil = nullptr;
        cmd.begin_render_pass(rp);
        cmd.set_viewport(cd::rhi::Viewport { 0.0F, 0.0F,
                                             static_cast<float>(dst.extent.width),
                                             static_cast<float>(dst.extent.height),
                                             0.0F, 1.0F });
        cmd.set_scissor(cd::rhi::Rect2D { { 0, 0 }, dst.extent });
        mat.apply(cmd);
        inst.bind(cmd, 0);
        if (!push_bytes.empty())
        {
            cmd.push_constants(mat.pipeline_layout(),
                               cd::rhi::ShaderStage::kFragment,
                               0,
                               static_cast<std::uint32_t>(push_bytes.size()),
                               push_bytes.data());
        }
        cmd.draw(3, 1, 0, 0);
        cmd.end_render_pass();

        std::array<cd::rhi::TextureBarrier, 1> tb2 {
            cd::rhi::TextureBarrier { .texture = dst.image,
                                     .from = cd::rhi::ResourceState::kColorAttachment,
                                     .to = cd::rhi::ResourceState::kShaderResource,
                                     .range = { 0, 1, 0, 1 } }
        };
        cmd.barrier({}, tb2);
    };

    const bool bloom_first_frame = (frame_idx == 0 || force_first_frame);
    // 1) Prefilter: HDR -> mip0 (soft-knee threshold, EV-scaled per phase 511).
    {
        BloomPrefilterPush bpp {};
        bpp.params[0] = 1.10F;     // threshold (linear HDR units)
        bpp.params[1] = 0.50F;     // knee
        bpp.params[2] = ev_stops;  // scene EV in stops (phase 511)
        bpp.params[3] = 0.0F;
        std::span<const std::byte> bytes { reinterpret_cast<const std::byte*>(&bpp), sizeof(bpp) };
        run_bloom_pass(bloom_prefilter_material, bloom_prefilter_inst,
                       bloom_chain.mips[0], cd::rhi::LoadOp::kClear, bytes, bloom_first_frame);
    }
    // 2) Downsample chain: mip0 -> 1, 1 -> 2, 2 -> 3.
    for (std::uint32_t i = 0; i < 3; ++i)
    {
        run_bloom_pass(bloom_downsample_material, bloom_down_insts[i],
                       bloom_chain.mips[i + 1], cd::rhi::LoadOp::kClear, {}, bloom_first_frame);
    }
    // 3) Upsample chain: mip3 -> 2, 2 -> 1, 1 -> 0 (additive blend).
    //    LoadOp::kLoad preserves the prior pass output we are adding onto.
    for (std::uint32_t i = 0; i < 3; ++i)
    {
        const std::uint32_t dst_index = 3U - 1U - i;  // 2, 1, 0
        BloomUpsamplePush bup {};
        bup.params[0] = 1.0F;  // radius (px scale)
        bup.params[1] = 1.0F;  // intensity per level
        bup.params[2] = 0.0F;
        bup.params[3] = 0.0F;
        std::span<const std::byte> bytes { reinterpret_cast<const std::byte*>(&bup), sizeof(bup) };
        run_bloom_pass(bloom_upsample_material, bloom_up_insts[i],
                       bloom_chain.mips[dst_index], cd::rhi::LoadOp::kLoad, bytes, bloom_first_frame);
    }
}

// ---- begin_composite_pass --------------------------------------------------
// Heavy composite-pass extraction (phase 317 / Marathon Run 10 N6C, ~285
// lines body): TAA history ping-pong barrier, swapchain + history render
// pass open, composite_material apply, CompositePush fill (tonemap + AO +
// DOF + light shafts + atmospheric fog + camera basis for SSR + prev-cam
// motion blur), draw, prev_cam_basis snapshot for next frame, prev_vp
// snapshot for next frame's velocity pass.
//
// IMPORTANT: this helper OPENS the swapchain render pass but does NOT
// close it. The caller draws ImGui inside the same pass after this
// returns and then calls cmd.end_render_pass(). That keeps the swapchain
// + history dual-attachment configuration consistent across composite
// + ImGui without recreating the pass.
inline void begin_composite_pass(cd::rhi::ICommandBuffer& cmd,
                                 std::uint32_t frame_idx,
                                 cd::rhi::TextureViewHandle swapchain_view,
                                 cd::rhi::Extent2D extent,
                                 std::array<ColorTarget, 2>& history_targets,
                                 std::array<cd::rhi::ResourceState, 2>& history_states,
                                 cd::material::Material& composite_material,
                                 std::array<cd::material::MaterialInstance, 2>& composite_insts,
                                 const cd_studio::StudioRenderFx& fx,
                                 const std::vector<LightRow>& lights,
                                 const cd::camera::Camera& cam,
                                 std::chrono::steady_clock::time_point frame_loop_start,
                                 PrevCamBasis& prev_cam_basis,
                                 cd::math::Mat4f& prev_vp_unjittered,
                                 bool& prev_vp_valid,
                                 const cd::math::Mat4f& vp_unjittered)
{
    // TAA ping-pong selection. composite_insts[read_idx] has its
    // binding=4 wired to history_targets[read_idx]; we render into
    // history_targets[write_idx] (= the OTHER one) as the 2nd
    // color attachment so next frame can read it.
    const std::uint32_t read_idx = frame_idx & 1U;
    const std::uint32_t write_idx = 1U - read_idx;

    // Barrier the two history targets: read side ??' ShaderResource,
    // write side ??' ColorAttachment.
    {
        std::array<cd::rhi::TextureBarrier, 2> hb {
            cd::rhi::TextureBarrier { .texture = history_targets[read_idx].image,
                                     .from = history_states[read_idx],
                                     .to = cd::rhi::ResourceState::kShaderResource,
                                     .range = { 0, 1, 0, 1 } },
            cd::rhi::TextureBarrier { .texture = history_targets[write_idx].image,
                                     .from = history_states[write_idx],
                                     .to = cd::rhi::ResourceState::kColorAttachment,
                                     .range = { 0, 1, 0, 1 } }
        };
        cmd.barrier({}, hb);
        history_states[read_idx] = cd::rhi::ResourceState::kShaderResource;
        history_states[write_idx] = cd::rhi::ResourceState::kColorAttachment;
    }

    std::array<cd::rhi::ColorAttachmentInfo, 2> swap_attach {
        cd::rhi::ColorAttachmentInfo { .view = swapchain_view,
                                      .load_op = cd::rhi::LoadOp::kClear,
                                      .store_op = cd::rhi::StoreOp::kStore,
                                      .clear_color = { .f32 = { 0.0F, 0.0F, 0.0F, 1.0F } } },
        cd::rhi::ColorAttachmentInfo { .view = history_targets[write_idx].view,
                                      .load_op = cd::rhi::LoadOp::kClear,
                                      .store_op = cd::rhi::StoreOp::kStore,
                                      .clear_color = { .f32 = { 0.0F, 0.0F, 0.0F, 1.0F } } }
    };
    cd::rhi::RenderPassBeginInfo swap_rp {};
    swap_rp.render_area = cd::rhi::Rect2D {
        { 0, 0 },
        extent
    };
    swap_rp.color_attachments = swap_attach;
    swap_rp.depth_stencil = nullptr;
    cmd.begin_render_pass(swap_rp);
    cmd.set_viewport(
        cd::rhi::Viewport { 0.0F,
                            0.0F,
                            static_cast<float>(extent.width),
                            static_cast<float>(extent.height),
                            0.0F,
                            1.0F }
    );
    cmd.set_scissor(
        cd::rhi::Rect2D {
            { 0, 0 },
            extent
    }
    );
    composite_material.apply(cmd);
    composite_insts[read_idx].bind(cmd, 0);
    CompositePush cp {};
    cp.fx[0] = static_cast<float>(fx.tonemap_op);
    cp.fx[1] = fx.exposure;
    cp.fx[2] = fx.saturation_boost;
    cp.fx[3] = fx.bloom_post;
    cp.ao[0] = fx.ao_strength;
    // B05: 4 px was nearly invisible at 1600?900. Bumped to 20 px
    // so the crease darkening reads at typical viewport sizes.
    cp.ao[1] = 20.0F;
    cp.ao[2] = cam.near_z;
    cp.ao[3] = cam.far_z;
    // DOF - wired from the existing UI slider. Focus on cam.target
    // (length(eye - target)), default 4 m range, 8 px max blur.
    const float focus_dist = cd::math::length(
        cd::math::Vec3f { cam.eye.x - cam.target.x, cam.eye.y - cam.target.y, cam.eye.z - cam.target.z }
    );
    cp.dof[0] = fx.dof_strength;
    cp.dof[1] = focus_dist;
    cp.dof[2] = 4.0F;  // focus range (m) - pixels within ??range stay sharp
    cp.dof[3] = 8.0F;  // max blur radius (px)
    // Light shafts - project the first enabled directional light's
    // sun position to screen-space UV (sun lives at infinity in
    // direction -L). If sun is behind camera (fwd_dot ??? 0) we
    // signal disabled via negative strength.
    cp.shafts[0] = 0.5F;
    cp.shafts[1] = 0.5F;
    cp.shafts[2] = -1.0F;  // disabled until a directional light + visible sun
    cp.shafts[3] = 1.0F;
    // sun_col.w packs the volumetric-clouds coverage (composite uses it
    // for the sky-region fBm cloud overlay). RGB filled in the loop
    // below from the first enabled directional light's colour.
    cp.sun_col[0] = 0.0F;
    cp.sun_col[1] = 0.0F;
    cp.sun_col[2] = 0.0F;
    cp.sun_col[3] = fx.clouds_coverage;
    // phase872-sun-col-direction-bug-fix: the loop below early-exits
    // (break) on every "sun is behind camera" or "shafts geometry
    // degenerate" branch, which left cp.sun_col at zero whenever the
    // camera yawed past 90° from the sun. The composite shader's
    // `sun_amt = length(sun_col.rgb) * 0.5` then collapsed to zero
    // and `cloud_lit *= mix(0.04, 1.0, 0)` darkened the cloud overlay
    // to near-black — exactly the user-reported "180° yaw colour
    // flip". sun_col is now set UNCONDITIONALLY on the first enabled
    // directional light below; the shafts geometry continues to
    // gate the shafts amount, not the colour.
    for (const auto& lrow : lights)
    {
        if (!lrow.enabled)
            continue;
        if (lrow.light.type != cd::light::LightType::kDirectional)
            continue;
        // Set the colour FIRST — this is the per-frame "sun is on"
        // signal the composite needs regardless of view direction.
        cp.sun_col[0] = lrow.light.color.x;
        cp.sun_col[1] = lrow.light.color.y;
        cp.sun_col[2] = lrow.light.color.z;
        const cd::math::Vec3f to_sun { -lrow.light.direction.x, -lrow.light.direction.y, -lrow.light.direction.z };
        // Compute camera basis (forward/right/up). Same construction
        // as the sky/PBR push setup right above.
        const cd::math::Vec3f cam_fwd_n { cam.target.x - cam.eye.x,
                                          cam.target.y - cam.eye.y,
                                          cam.target.z - cam.eye.z };
        const float cam_fwd_len =
            std::sqrt(cam_fwd_n.x * cam_fwd_n.x + cam_fwd_n.y * cam_fwd_n.y + cam_fwd_n.z * cam_fwd_n.z);
        if (cam_fwd_len < 1e-6F)
            break;
        const cd::math::Vec3f f { cam_fwd_n.x / cam_fwd_len, cam_fwd_n.y / cam_fwd_len, cam_fwd_n.z / cam_fwd_len };
        const cd::math::Vec3f shaft_up_axis { 0.0F, 1.0F, 0.0F };
        const cd::math::Vec3f r_raw { f.y * shaft_up_axis.z - f.z * shaft_up_axis.y,
                                      f.z * shaft_up_axis.x - f.x * shaft_up_axis.z,
                                      f.x * shaft_up_axis.y - f.y * shaft_up_axis.x };
        const float r_len = std::sqrt(r_raw.x * r_raw.x + r_raw.y * r_raw.y + r_raw.z * r_raw.z);
        if (r_len < 1e-6F)
            break;
        const cd::math::Vec3f r { r_raw.x / r_len, r_raw.y / r_len, r_raw.z / r_len };
        const cd::math::Vec3f u { r.y * f.z - r.z * f.y, r.z * f.x - r.x * f.z, r.x * f.y - r.y * f.x };
        const float fwd_dot = to_sun.x * f.x + to_sun.y * f.y + to_sun.z * f.z;
        if (fwd_dot <= 0.0F)
            break;  // sun behind camera
        const float r_dot = to_sun.x * r.x + to_sun.y * r.y + to_sun.z * r.z;
        const float u_dot = to_sun.x * u.x + to_sun.y * u.y + to_sun.z * u.z;
        const float aspect_l = static_cast<float>(extent.width) / static_cast<float>(extent.height);
        const float half_h_l = std::tan(cam.fov_y * 0.5F);
        const float half_w_l = half_h_l * aspect_l;
        const float sun_ndc_x = (r_dot / fwd_dot) / half_w_l;
        const float sun_ndc_y = (u_dot / fwd_dot) / half_h_l;
        cp.shafts[0] = 0.5F + 0.5F * sun_ndc_x;
        cp.shafts[1] = 0.5F - 0.5F * sun_ndc_y;
        // W4-E: smoother edge fade. Old fade hit zero exactly at the
        // [-1, 1] NDC boundary, so off-screen sun caused shafts to
        // pop. New shape uses smoothstep with a half-NDC overshoot
        // so shafts taper gracefully across the edge.
        const float ndc_max = std::max(std::abs(sun_ndc_x), std::abs(sun_ndc_y));
        // 0 at ndc_max=1.5 (just off-screen), 1 at ndc_max<=0.5
        // (well-inside). Smoothstep(1.5, 0.5, ndc_max) follows the
        // requested orientation.
        float edge_fade = 1.0F;
        {
            const float t = std::clamp((1.5F - ndc_max) / 1.0F, 0.0F, 1.0F);
            edge_fade = t * t * (3.0F - 2.0F * t);
        }
        cp.shafts[2] = fx.shafts_strength * edge_fade;
        // W4-E: gentler decay so shafts visibly reach across the
        // frame instead of dying within ~25% of UV distance from
        // sun. Was 3.5; 1.6 keeps shafts readable at the corners.
        cp.shafts[3] = 1.6F;  // decay (per UV distance)
        // phase872: sun_col was already set at the top of the loop;
        // no need to re-assign here.
        break;
    }
    // Atmospheric fog (uniform exp-haze) + aerial perspective (sky
    // horizon tint with distance). Reuses the existing UI sliders
    // so the composite is now the *one* home for these effects.
    // phase512-volumetric-fog-wire: sign bit on atmo[0] selects mode.
    //   positive density -> legacy single-tap exp fog
    //   negative density -> Wronski 2014 integrated single-scatter
    // The composite shader reads abs(pc.atmo.x) as density and tests
    // pc.atmo.x < 0 for the mode flag. Zero density disables either
    // path so the checkbox is a true no-op when density == 0.
    cp.atmo[0] = fx.volumetric_fog_on ? -fx.fog_density : fx.fog_density;
    cp.atmo[1] = fx.aerial_perspective;
    cp.atmo[2] = fx.vignette_strength;
    cp.atmo[3] = fx.film_grain;
    cp.lens[0] = fx.chromab_strength;
    // R5 volumetric fog single-scatter - sun direction packed here.
    // Composite uses (view ? -sun) with Henyey-Greenstein phase to
    // colour the fog along the sun ray. Use first enabled directional
    // light, else neutral (0,-1,0) so no in-scatter shows up.
    cd::math::Vec3f sun_dir_world { 0.0F, -1.0F, 0.0F };
    for (const auto& lrow : lights)
    {
        if (!lrow.enabled)
            continue;
        if (lrow.light.type != cd::light::LightType::kDirectional)
            continue;
        sun_dir_world = lrow.light.direction;
        break;
    }
    cp.lens[1] = sun_dir_world.x;
    cp.lens[2] = sun_dir_world.y;
    cp.lens[3] = sun_dir_world.z;
    // G-Buffer-aware ops: pack camera basis so the composite FS can
    // reconstruct world-space positions per pixel for SSR + normal-
    // aware AO. Match the same basis the sky shader uses (forward
    // = (target-eye)/|...|, right = forward ?- +Y, up = right ?-
    // forward) so SSR rays project consistently.
    {
        const cd::math::Vec3f fwd_raw { cam.target.x - cam.eye.x,
                                        cam.target.y - cam.eye.y,
                                        cam.target.z - cam.eye.z };
        const float ssr_fl = std::sqrt(fwd_raw.x * fwd_raw.x + fwd_raw.y * fwd_raw.y + fwd_raw.z * fwd_raw.z);
        const cd::math::Vec3f fwd =
            (ssr_fl > 1e-6F) ? cd::math::Vec3f { fwd_raw.x / ssr_fl, fwd_raw.y / ssr_fl, fwd_raw.z / ssr_fl }
                             : cd::math::Vec3f { 0.0F, 0.0F, -1.0F };
        constexpr cd::math::Vec3f cam_world_up { 0.0F, 1.0F, 0.0F };
        const cd::math::Vec3f r_raw { fwd.y * cam_world_up.z - fwd.z * cam_world_up.y,
                                      fwd.z * cam_world_up.x - fwd.x * cam_world_up.z,
                                      fwd.x * cam_world_up.y - fwd.y * cam_world_up.x };
        const float ssr_rl = std::sqrt(r_raw.x * r_raw.x + r_raw.y * r_raw.y + r_raw.z * r_raw.z);
        const cd::math::Vec3f right = (ssr_rl > 1e-6F)
                                          ? cd::math::Vec3f { r_raw.x / ssr_rl, r_raw.y / ssr_rl, r_raw.z / ssr_rl }
                                          : cd::math::Vec3f { 1.0F, 0.0F, 0.0F };
        const cd::math::Vec3f up_cam { right.y * fwd.z - right.z * fwd.y,
                                       right.z * fwd.x - right.x * fwd.z,
                                       right.x * fwd.y - right.y * fwd.x };
        const float aspect_l = static_cast<float>(extent.width) / static_cast<float>(extent.height);
        const float half_h_l = std::tan(cam.fov_y * 0.5F);
        const float half_w_l = half_h_l * aspect_l;
        cp.cam_right[0] = right.x;
        cp.cam_right[1] = right.y;
        cp.cam_right[2] = right.z;
        cp.cam_right[3] = half_w_l;
        cp.cam_up[0] = up_cam.x;
        cp.cam_up[1] = up_cam.y;
        cp.cam_up[2] = up_cam.z;
        cp.cam_up[3] = half_h_l;
        cp.cam_fwd[0] = fwd.x;
        cp.cam_fwd[1] = fwd.y;
        // TAA alpha - first frame must blend 0 (history undefined).
        cp.cam_fwd[2] = fwd.z;
        cp.cam_fwd[3] = (frame_idx > 0) ? fx.taa_amount : 0.0F;
        cp.cam_pos[0] = cam.eye.x;
        cp.cam_pos[1] = cam.eye.y;
        // W6-B: w slot carries the composite's anim-time (seconds
        // since the frame loop started) so post-fx that need a
        // monotonic clock — e.g. the volumetric-cloud drift — read
        // it without an extra push-constant slot or a global state
        // buffer. Use the frame-loop epoch instead of steady_clock
        // since-epoch so the noise stays in a sane numeric range.
        cp.cam_pos[2] = cam.eye.z;
        cp.cam_pos[3] = std::chrono::duration<float>(std::chrono::steady_clock::now() - frame_loop_start).count();
    }
    // SSR - wired from the existing UI slider; defaults to 0 (off).
    cp.ssr[0] = fx.ssr_strength;
    cp.ssr[1] = 25.0F;  // max distance (m)
    cp.ssr[2] = 24.0F;  // max steps
    cp.ssr[3] = 1.5F;   // edge-fade aggressiveness

    // Camera-velocity motion blur: pack the prev-frame basis. On
    // the very first frame, mirror current basis (zero velocity).
    {
        const auto& pb = prev_cam_basis;
        const bool first = !pb.valid;
        const cd::math::Vec3f pr =
            first ? cd::math::Vec3f { cp.cam_right[0], cp.cam_right[1], cp.cam_right[2] } : pb.right;
        const cd::math::Vec3f pu = first ? cd::math::Vec3f { cp.cam_up[0], cp.cam_up[1], cp.cam_up[2] } : pb.up;
        const cd::math::Vec3f pf = first ? cd::math::Vec3f { cp.cam_fwd[0], cp.cam_fwd[1], cp.cam_fwd[2] } : pb.fwd;
        const cd::math::Vec3f pp = first ? cd::math::Vec3f { cp.cam_pos[0], cp.cam_pos[1], cp.cam_pos[2] } : pb.pos;
        const float phw = first ? cp.cam_right[3] : pb.half_w;
        const float phh = first ? cp.cam_up[3] : pb.half_h;
        cp.prev_cam_right[0] = pr.x;
        cp.prev_cam_right[1] = pr.y;
        cp.prev_cam_right[2] = pr.z;
        cp.prev_cam_right[3] = phw;
        cp.prev_cam_up[0] = pu.x;
        cp.prev_cam_up[1] = pu.y;
        cp.prev_cam_up[2] = pu.z;
        cp.prev_cam_up[3] = phh;
        cp.prev_cam_fwd[0] = pf.x;
        cp.prev_cam_fwd[1] = pf.y;
        cp.prev_cam_fwd[2] = pf.z;
        cp.prev_cam_fwd[3] = fx.motion_blur;
        cp.prev_cam_pos[0] = pp.x;
        cp.prev_cam_pos[1] = pp.y;
        cp.prev_cam_pos[2] = pp.z;
        cp.prev_cam_pos[3] = 8.0F;  // sample count
    }

    cmd.push_constants(composite_material.pipeline_layout(), cd::rhi::ShaderStage::kFragment, 0, sizeof(cp), &cp);
    cmd.draw(3, 1, 0, 0);

    // Snapshot current camera basis for next frame's velocity
    // reprojection. Done AFTER the push so the next frame can
    // reproject "where was this pixel one frame ago?".
    prev_cam_basis.right = { cp.cam_right[0], cp.cam_right[1], cp.cam_right[2] };
    prev_cam_basis.up = { cp.cam_up[0], cp.cam_up[1], cp.cam_up[2] };
    prev_cam_basis.fwd = { cp.cam_fwd[0], cp.cam_fwd[1], cp.cam_fwd[2] };
    prev_cam_basis.pos = { cp.cam_pos[0], cp.cam_pos[1], cp.cam_pos[2] };
    prev_cam_basis.half_w = cp.cam_right[3];
    prev_cam_basis.half_h = cp.cam_up[3];
    prev_cam_basis.valid = true;
    // Snapshot the un-jittered VP for next frame's velocity pass.
    prev_vp_unjittered = vp_unjittered;
    prev_vp_valid = true;
}

// ---- HdrSceneFrame + begin_hdr_scene_pass ---------------------------------
// Open the R3 HDR + 3 G-Buffer (normal/albedo/MR) + depth attachment render
// pass and return the per-frame VP matrices the downstream sky / floor /
// entity passes need. cd::post::taa::jitter_offset(frame_idx, 8) is the
// Halton(2,3) sequence; we only apply it when TAA is dialled in.
//
// Pass stays open across draw_sky_pass, fill + draw of floor/ECS/planar
// shadow + gizmo overlay. The caller closes it explicitly via
// cmd.end_render_pass() before the composite pass opens the swapchain pass.
struct HdrSceneFrame
{
    cd::math::Mat4f vp;
    cd::math::Mat4f vp_unjittered;
    float aspect { 1.0F };
};

inline HdrSceneFrame make_hdr_frame(std::uint32_t frame_idx,
                                    cd::rhi::Extent2D extent,
                                    const cd::camera::Camera& cam,
                                    float taa_amount)
{
    HdrSceneFrame out {};
    out.aspect = static_cast<float>(extent.width) / static_cast<float>(extent.height);
    out.vp_unjittered = cd::camera::view_projection(cam, out.aspect);

    const cd::math::Vec2f jitter_px =
        (taa_amount > 0.001F) ? cd::post::taa::jitter_offset(frame_idx, 8U) : cd::math::Vec2f { 0.0F, 0.0F };
    const float jx_ndc = jitter_px.x * 2.0F / static_cast<float>(extent.width);
    const float jy_ndc = jitter_px.y * 2.0F / static_cast<float>(extent.height);
    out.vp = out.vp_unjittered;
    for (std::size_t c = 0; c < 4; ++c)
    {
        out.vp[c][0] += jx_ndc * out.vp_unjittered[c][3];
        out.vp[c][1] += jy_ndc * out.vp_unjittered[c][3];
    }
    return out;
}

// phase1121 (X1-FU-F step 3): the barrier + attachment construction is
// shared by the serial open and the parallel-lane open below so the two
// paths can never drift.
inline void hdr_pass_barriers(cd::rhi::ICommandBuffer& cmd,
                              std::uint32_t frame_idx,
                              const cd::framegraph::ColorTarget& hdr_target,
                              const cd::framegraph::ColorTarget& gbuf_normal,
                              const cd::framegraph::ColorTarget& gbuf_albedo,
                              const cd::framegraph::ColorTarget& gbuf_mr)
{
    const cd::rhi::ResourceState prev_state =
        (frame_idx == 0) ? cd::rhi::ResourceState::kUndefined : cd::rhi::ResourceState::kShaderResource;
    std::array<cd::rhi::TextureBarrier, 4> hb {
        cd::rhi::TextureBarrier { .texture = hdr_target.image,
                                 .from = prev_state,
                                 .to = cd::rhi::ResourceState::kColorAttachment,
                                 .range = { 0, 1, 0, 1 } },
        cd::rhi::TextureBarrier { .texture = gbuf_normal.image,
                                 .from = prev_state,
                                 .to = cd::rhi::ResourceState::kColorAttachment,
                                 .range = { 0, 1, 0, 1 } },
        cd::rhi::TextureBarrier { .texture = gbuf_albedo.image,
                                 .from = prev_state,
                                 .to = cd::rhi::ResourceState::kColorAttachment,
                                 .range = { 0, 1, 0, 1 } },
        cd::rhi::TextureBarrier { .texture = gbuf_mr.image,
                                 .from = prev_state,
                                 .to = cd::rhi::ResourceState::kColorAttachment,
                                 .range = { 0, 1, 0, 1 } }
    };
    cmd.barrier({}, hb);
}

/// Attachment bundle for the HDR pass. Self-contained by value: the
/// RenderPassBeginInfo is built by the caller FROM this struct so its
/// span/pointer always reference the caller's living instance.
struct HdrPassAttach
{
    std::array<cd::rhi::ColorAttachmentInfo, 4> color {};
    cd::rhi::DepthStencilAttachmentInfo depth {};
};

inline HdrPassAttach hdr_pass_attach(const cd::framegraph::ColorTarget& hdr_target,
                                     const cd::framegraph::ColorTarget& gbuf_normal,
                                     const cd::framegraph::ColorTarget& gbuf_albedo,
                                     const cd::framegraph::ColorTarget& gbuf_mr,
                                     const cd::framegraph::DepthTarget& depth)
{
    HdrPassAttach a {};
    a.color = {
        cd::rhi::ColorAttachmentInfo { .view = hdr_target.view,
                                      .load_op = cd::rhi::LoadOp::kClear,
                                      .store_op = cd::rhi::StoreOp::kStore,
                                      .clear_color = { .f32 = { 1.0F, 0.0F, 1.0F, 1.0F } } },
        cd::rhi::ColorAttachmentInfo { .view = gbuf_normal.view,
                                      .load_op = cd::rhi::LoadOp::kClear,
                                      .store_op = cd::rhi::StoreOp::kStore,
                                      .clear_color = { .f32 = { 0.0F, 0.0F, 0.0F, 0.0F } } },
        cd::rhi::ColorAttachmentInfo { .view = gbuf_albedo.view,
                                      .load_op = cd::rhi::LoadOp::kClear,
                                      .store_op = cd::rhi::StoreOp::kStore,
                                      .clear_color = { .f32 = { 0.0F, 0.0F, 0.0F, 0.0F } } },
        cd::rhi::ColorAttachmentInfo { .view = gbuf_mr.view,
                                      .load_op = cd::rhi::LoadOp::kClear,
                                      .store_op = cd::rhi::StoreOp::kStore,
                                      .clear_color = { .f32 = { 0.0F, 1.0F, 0.0F, 0.0F } } }
    };
    a.depth.view = depth.view;
    a.depth.depth_load = cd::rhi::LoadOp::kClear;
    a.depth.depth_store = cd::rhi::StoreOp::kStore;
    a.depth.clear.depth = 1.0F;
    return a;
}

inline HdrSceneFrame begin_hdr_scene_pass(cd::rhi::ICommandBuffer& cmd,
                                          std::uint32_t frame_idx,
                                          const cd::framegraph::ColorTarget& hdr_target,
                                          const cd::framegraph::ColorTarget& gbuf_normal,
                                          const cd::framegraph::ColorTarget& gbuf_albedo,
                                          const cd::framegraph::ColorTarget& gbuf_mr,
                                          const cd::framegraph::DepthTarget& depth,
                                          cd::rhi::Extent2D extent,
                                          const cd::camera::Camera& cam,
                                          float taa_amount)
{
    hdr_pass_barriers(cmd, frame_idx, hdr_target, gbuf_normal, gbuf_albedo, gbuf_mr);
    const HdrPassAttach a = hdr_pass_attach(hdr_target, gbuf_normal, gbuf_albedo,
                                            gbuf_mr, depth);
    cd::rhi::RenderPassBeginInfo rp {};
    rp.render_area = cd::rhi::Rect2D { { 0, 0 }, extent };
    rp.color_attachments = a.color;
    rp.depth_stencil = &a.depth;
    cmd.begin_render_pass(rp);
    cmd.set_viewport(cd::rhi::Viewport { 0.0F, 0.0F,
                                         static_cast<float>(extent.width),
                                         static_cast<float>(extent.height),
                                         0.0F, 1.0F });
    cmd.set_scissor(cd::rhi::Rect2D { { 0, 0 }, extent });
    return make_hdr_frame(frame_idx, extent, cam, taa_amount);
}

/// phase1121 (X1-FU-F step 3): parallel-lane variant. Same barriers and
/// attachments, then the pass opens with secondary-contents semantics.
/// When the backend reports no lane support (null recorder) it falls back
/// to the serial open INTERNALLY so barriers are never emitted twice;
/// callers branch on the returned recorder.
inline std::pair<HdrSceneFrame, std::unique_ptr<cd::rhi::IParallelPassRecorder>>
begin_hdr_scene_pass_lanes(cd::rhi::ICommandBuffer& cmd,
                           std::uint32_t frame_idx,
                           const cd::framegraph::ColorTarget& hdr_target,
                           const cd::framegraph::ColorTarget& gbuf_normal,
                           const cd::framegraph::ColorTarget& gbuf_albedo,
                           const cd::framegraph::ColorTarget& gbuf_mr,
                           const cd::framegraph::DepthTarget& depth,
                           cd::rhi::Extent2D extent,
                           const cd::camera::Camera& cam,
                           float taa_amount,
                           std::uint32_t lane_count)
{
    hdr_pass_barriers(cmd, frame_idx, hdr_target, gbuf_normal, gbuf_albedo, gbuf_mr);
    const HdrPassAttach a = hdr_pass_attach(hdr_target, gbuf_normal, gbuf_albedo,
                                            gbuf_mr, depth);
    cd::rhi::RenderPassBeginInfo rp {};
    rp.render_area = cd::rhi::Rect2D { { 0, 0 }, extent };
    rp.color_attachments = a.color;
    rp.depth_stencil = &a.depth;
    auto rec = cmd.begin_parallel_render_pass(rp, lane_count);
    if (rec == nullptr)
    {
        cmd.begin_render_pass(rp);
        cmd.set_viewport(cd::rhi::Viewport { 0.0F, 0.0F,
                                             static_cast<float>(extent.width),
                                             static_cast<float>(extent.height),
                                             0.0F, 1.0F });
        cmd.set_scissor(cd::rhi::Rect2D { { 0, 0 }, extent });
    }
    return { make_hdr_frame(frame_idx, extent, cam, taa_amount), std::move(rec) };
}

/// phase1121: a lane's secondary inherits NO dynamic state from the
/// primary — every lane must (re)set viewport/scissor before its draws.
inline void prep_hdr_lane(cd::rhi::IDrawRecorder& lane, cd::rhi::Extent2D extent)
{
    lane.set_viewport(cd::rhi::Viewport { 0.0F, 0.0F,
                                          static_cast<float>(extent.width),
                                          static_cast<float>(extent.height),
                                          0.0F, 1.0F });
    lane.set_scissor(cd::rhi::Rect2D { { 0, 0 }, extent });
}

// ---- draw_floor_and_entities ----------------------------------------------
// Draws the floor quad (with FS sentinel tint.w=2.0 -> analytic XZ grid)
// and the ECS entity primitives row in one helper. Both use the same
// prim_material + fill_prim_push_shared for the sun/fx/cam fields.
//
// X1D parallel-prep / serial-draw split (phase 286) preserved: per-entity
// PrimPush is built via parallel_for into a pre-sized scratch vector, the
// bind + push + draw pass stays serial (Vulkan cmd recording not
// thread-safe per buffer, ADR-015 / Vulkan spec 5.1).
//
// kFloorY constant is owned by the caller because planar shadows need it
// for the projection matrix; we accept it as an argument so the constant
// stays a single source of truth.
template <typename MeshFor, typename PrimRangesFor>
inline void draw_floor_and_entities(cd::rhi::IDrawRecorder& cmd,
                                    const GpuMesh& floor_mesh,
                                    float floor_y,
                                    const cd::math::Mat4f& vp,
                                    const cd_studio::StudioRenderFx& fx,
                                    const SunLight& sun,
                                    const cd::camera::Camera& cam,
                                    std::vector<SceneEntity>& entities,
                                    const cd::scene::Scene& scene,
                                    bool has_gltf_texture,
                                    cd::material::Material& prim_material,
                                    cd::core::CounterTable& counters,
                                    const MeshFor& mesh_for,
                                    PrimRangesFor prim_ranges_for,
                                    cd::material::MaterialInstance& prim_inst,
                                    cd::rhi::SamplerHandle albedo_sampler,
                                    cd::rhi::IDevice& device,
                                    cd::rhi::TextureViewHandle global_albedo_view,
                                    // phase456: global procedural normal + MR views used to
                                    // restore the shared prim_inst's bindings 8 + 9 after the
                                    // Sponza per-prim draw loop, so subsequent entity draws
                                    // (CesiumMan, PBR spheres) sample the right material maps.
                                    cd::rhi::TextureViewHandle global_normal_view = {},
                                    cd::rhi::TextureViewHandle global_mr_view     = {},
                                    // phase435-vis8: skip editor grid when a scene with its
                                    // own floor (Sponza) is active. Grid bleeds through
                                    // alpha-discarded vegetation pixels. Default true
                                    // keeps legacy call sites unchanged.
                                    bool show_editor_floor = true)
{
    // ---- Floor (large flat quad) ----
    // Faz 1.5 - real geometry on which the planar-shadow pass can
    // project caster silhouettes. Floor sits at y = floor_y so the
    // front-row primitives (which extend ??0.5 m around y=0) just
    // touch it.
    // phase435-vis8: skip entirely when a scene with its own floor is active.
    auto emit_floor_draw = [&](float sentinel_w)
    {
        cmd.bind_vertex_buffer(0, floor_mesh.vb, 0);
        cmd.bind_index_buffer(floor_mesh.ib, 0, floor_mesh.index_type);
        cd::math::Mat4f floor_model = cd::math::Mat4f::identity();
        floor_model[3][1] = floor_y;  // translate quad to y = floor_y
        const auto floor_mvp = vp * floor_model;
        PrimPush fp {};
        fp.mvp = floor_mvp;
        fp.model = floor_model;
        fp.tint[0] = 0.15F;
        fp.tint[1] = 0.16F;
        fp.tint[2] = 0.18F;
        fp.tint[3] = sentinel_w;
        fill_prim_push_shared(fp, fx, sun, cam);
        fp.fx_params[1] = 0.0F;
        fp.fx_params[2] = 0.0F;
        fp.fx_params4[0] = fp.fx_params4[1] = fp.fx_params4[2] = 0.0F;
        fp.fx_params4[3] = static_cast<float>(fx.view_mode);
        cmd.push_constants(
            prim_material.pipeline_layout(),
            cd::rhi::ShaderStage::kVertex | cd::rhi::ShaderStage::kFragment,
            0,
            sizeof(fp),
            &fp
        );
        cmd.draw_indexed(floor_mesh.index_count, 1, 0, 0, 0);
        counters.increment("draws_prim");
    };

    if (show_editor_floor && cd::studio_rt::golden::enabled())
    {
        emit_floor_draw(2.0F);
    }

    // X1D (phase 286): parallel ECS PrimPush prep. The push-
    // constant struct + valid flag is built per entity into a
    // pre-sized scratch vector via cd::concurrency::parallel_for
    // (write-by-index, no push_back), then the serial draw pass
    // binds mesh + records push_constants + draw_indexed.
    // Vulkan cmd buffer recording isn't thread-safe per buffer
    // (ADR-015 / Vulkan spec 5.1) so submission stays serial; the
    // win is the prep phase parallelizes and the scaling story
    // unlocks when entity count grows past the worker count.
    static thread_local std::vector<PrimPush> ent_push_scratch;
    static thread_local std::vector<std::uint8_t> ent_push_valid;
    ent_push_scratch.resize(entities.size());
    ent_push_valid.assign(entities.size(), 0u);
    auto prep_entity_push = [&](std::size_t i)
    {
        const auto& ent = entities[i];
        if (!ent.visible)
            return;
        if (ent.name.find("Ground_Floor") != std::string::npos &&
            !ent.use_texture && ent.custom_texture_id == 0)
            return;
        const auto& mesh = mesh_for(ent.kind);
        if (!mesh.vb.is_valid())
            return;
        auto* lt = scene.local(ent.handle);
        if (lt == nullptr)
            return;
        const auto model = cd::math::to_mat4(lt->value);
        const auto mvp = vp * model;
        PrimPush& pp = ent_push_scratch[i];
        pp.mvp = mvp;
        pp.model = model;
        pp.tint[0] = ent.tint.x;
        pp.tint[1] = ent.tint.y;
        pp.tint[2] = ent.tint.z;
        const bool has_custom_override_tex =
            (ent.kind == PrimitiveKind::kGltf && ent.use_texture && ent.custom_texture_id >= 2);
        const bool is_gltf_prim = (ent.kind == PrimitiveKind::kSponza ||
                                   (ent.kind == PrimitiveKind::kGltf && !has_custom_override_tex));
        pp.tint[3] = 1.0F;
        if (is_gltf_prim)
            pp.tint[3] = 4.0F;
        else if (ent.is_pbr)
            pp.tint[3] = 3.0F;
        fill_prim_push_shared(pp, fx, sun, cam);
        const bool gltf_tex_path = ((ent.kind == PrimitiveKind::kGltf) && !has_custom_override_tex && has_gltf_texture);
        const bool pbr_tex_path  = (!is_gltf_prim && ent.is_pbr && ent.use_texture);
        const float tex_slot_val = static_cast<float>(std::max(1, ent.custom_texture_id));
        pp.fx_params[1] = pbr_tex_path ? tex_slot_val : (gltf_tex_path ? 1.0F : 0.0F);
        if (ent.is_pbr && !is_gltf_prim)
        {
            pp.fx_params4[0] = ent.metallic;
            pp.fx_params4[1] = ent.roughness;
            const float irid_int = std::floor(std::clamp(ent.iridescence_nm, 0.0F, 1000.0F));
            const float ms_frac  = std::clamp(ent.multiscatter_comp * 0.99F, 0.0F, 0.99F);
            pp.fx_params4[2]     = irid_int + ms_frac;
        }
        else if (is_gltf_prim)
        {
            float m = 0.0F;
            float r = 0.9F;
            float ns = 0.0F;
            const auto ranges = prim_ranges_for(ent.kind);
            if (!ranges.empty())
            {
                m  = ranges.front().metallic;
                r  = ranges.front().roughness;
                ns = ranges.front().normal_strength;
            }
            pp.fx_params4[0] = m;
            pp.fx_params4[1] = r;
            pp.fx_params4[2] = ns;
        }
        else
        {
            pp.fx_params4[0] = fx.clearcoat_strength;
            pp.fx_params4[1] = fx.sheen_strength;
            pp.fx_params4[2] = fx.sss_strength;
        }
        pp.fx_params4[3] = static_cast<float>(fx.view_mode);
        ent_push_valid[i] = 1u;
    };
    if (entities.size() >= 256u)
    {
        cd::concurrency::parallel_for(std::size_t { 0 }, entities.size(), prep_entity_push);
    }
    else
    {
        for (std::size_t i = 0; i < entities.size(); ++i)
            prep_entity_push(i);
    }
    for (std::size_t i = 0; i < entities.size(); ++i)
    {
        if (ent_push_valid[i] == 0u)
            continue;
        const auto& ent = entities[i];
        const auto& mesh = mesh_for(ent.kind);
        cmd.bind_vertex_buffer(0, mesh.vb, 0);
        cmd.bind_index_buffer(mesh.ib, 0, mesh.index_type);
        cmd.push_constants(
            prim_material.pipeline_layout(),
            cd::rhi::ShaderStage::kVertex | cd::rhi::ShaderStage::kFragment,
            0,
            sizeof(PrimPush),
            &ent_push_scratch[i]
        );

        // Per-primitive material dispatch.
        // glTF models use prim_ranges for per-material texture/parameter switching.
        const bool skip_prim_ranges =
            (ent.kind == PrimitiveKind::kGltf && ent.use_texture && ent.custom_texture_id >= 2);
        const auto ranges = skip_prim_ranges
            ? std::span<const cd_studio::GltfPrimRange> {}
            : prim_ranges_for(ent.kind);
        if (!ranges.empty())
        {
            bool dirty_shared_prim_inst = false;
            for (const auto& pr : ranges)
            {
                if (pr.index_count == 0)
                    continue;
                if (pr.has_texture && pr.albedo_view.is_valid())
                {
                    if (pr.prim_inst.is_valid())
                    {
                        // Per-prim descriptor set (pre-built at boot by boot_meshes).
                        pr.prim_inst.bind(cmd, 0);
                    }
                    else
                    {
                        // Fallback: update the shared descriptor (legacy path, may alias).
                        const std::array<cd::rhi::DescriptorWrite, 1> tw {
                            cd::rhi::DescriptorWrite { .binding = 4,
                                                       .array_element = 0,
                                                       .type = cd::rhi::DescriptorType::kCombinedImageSampler,
                                                       .view = pr.albedo_view,
                                                       .sampler = albedo_sampler }
                        };
                        (void)prim_inst.update(tw);
                        prim_inst.bind(cmd, 0);
                        dirty_shared_prim_inst = true;
                    }
                }
                PrimPush sub_pp = ent_push_scratch[i];
                sub_pp.fx_params[1]  = pr.has_texture ? 1.0F : 0.0F;
                sub_pp.fx_params[3]  = pr.alpha_cutoff;
                sub_pp.fx_params4[0] = pr.metallic;
                sub_pp.fx_params4[1] = pr.roughness;
                sub_pp.fx_params4[2] = pr.normal_strength;
                cmd.push_constants(
                    prim_material.pipeline_layout(),
                    cd::rhi::ShaderStage::kVertex | cd::rhi::ShaderStage::kFragment,
                    0,
                    sizeof(PrimPush),
                    &sub_pp
                );
                cmd.draw_indexed(pr.index_count, 1, pr.index_offset, 0, 0);
                counters.increment("draws_prim");
            }
            if (dirty_shared_prim_inst && global_albedo_view.is_valid())
            {
                std::vector<cd::rhi::DescriptorWrite> rw;
                rw.reserve(3);
                rw.push_back(cd::rhi::DescriptorWrite {
                    .binding = 4,
                    .array_element = 0,
                    .type = cd::rhi::DescriptorType::kCombinedImageSampler,
                    .view = global_albedo_view,
                    .sampler = albedo_sampler });
                if (global_normal_view.is_valid())
                {
                    rw.push_back(cd::rhi::DescriptorWrite {
                        .binding = 8,
                        .array_element = 0,
                        .type = cd::rhi::DescriptorType::kCombinedImageSampler,
                        .view = global_normal_view,
                        .sampler = albedo_sampler });
                }
                if (global_mr_view.is_valid())
                {
                    rw.push_back(cd::rhi::DescriptorWrite {
                        .binding = 9,
                        .array_element = 0,
                        .type = cd::rhi::DescriptorType::kCombinedImageSampler,
                        .view = global_mr_view,
                        .sampler = albedo_sampler });
                }
                (void)prim_inst.update(rw);
            }
            prim_inst.bind(cmd, 0);
            (void)device;
        }
        else
        {
            cmd.draw_indexed(mesh.index_count, 1, 0, 0, 0);
            counters.increment("draws_prim");
        }
    }

    if (show_editor_floor && !cd::studio_rt::golden::enabled())
    {
        emit_floor_draw(2.25F);
    }
}

}  // namespace

// ============================================================================
// M2C/M2D/M2E: StudioApp full implementation.
//
// EngineState is defined here (not in StudioApp.hpp) so it can
// reference the anonymous-namespace types declared above
// (SceneEntity, GizmoState, SelKind, LightRow, GpuTexture2D, etc.).
// ============================================================================

// ---- EngineState definition ------------------------------------------------
struct StudioApp::EngineState
{
    // Window / device / renderer / imgui
    std::unique_ptr<cd::platform::IWindow>   window;
    std::unique_ptr<cd::rhi::IDevice>        device_owner;
    cd::render::Renderer                     renderer;
    std::unique_ptr<cd::imgui::Context>      imgui_ctx;

    // Shader compiler
    std::unique_ptr<cd::shader::ICompiler>   compiler;

    // Render targets
    cd_studio::RenderTargets                 rts;
    bool                                     depth_initialised_on_gpu  { false };
    bool                                     shadow_initialised_on_gpu { false };

    // Materials
    cd_studio::MaterialBundle                materials;
    cd_studio::StudioShaderWatch              shader_watch;

    // Shadow-map resources
    cd::framegraph::DepthTarget              shadow_target;
    cd::rhi::SamplerHandle                   shadow_sampler  {};
    cd::rhi::BufferHandle                    shadow_ubo      {};

    // Multi-light UBO + inst-mat SSBO
    cd::rhi::BufferHandle                    lights_ubo      {};
    cd::rhi::BufferHandle                    inst_mat_ssbo   {};

    // IBL
    cd_studio::IblBakeCpu                    ibl_cpu;
    cd_studio::IblBakeGpu                    ibl_gpu;

    // Procedural textures
    GpuTexture2D                             albedo_tex;
    GpuTexture2D                             normal_tex;
    GpuTexture2D                             mr_tex;
    cd::rhi::SamplerHandle                   albedo_sampler  {};
    bool                                     has_gltf_texture { false };
    // phase435-vis8: suppress the editor floor grid when a scene that
    // provides its own floor (Sponza) is loaded. Grid shines through
    // Sponza vegetation alpha-discards and overlaps column bases.
    bool                                     show_editor_floor { true };

    // Material instances
    cd::material::MaterialInstance           prim_inst;
    // phase864-bindless-dedicated-set: one descriptor set allocated
    // from materials.prim_bindless_layout, populated at boot with
    // Sponza per-prim albedos + fallback for unused slots. Bound as
    // set index 1 alongside prim_inst (set 0) when the chrome
    // reflection branch needs to sample the bindless array.
    cd::rhi::DescriptorSetHandle             prim_bindless_set {};
    // phase1031-debug-line-gpu: CPU batch + host-visible VB for the
    // cd::debug_line kLineList pipeline. The batch is rebuilt every
    // frame by whichever debug overlays are toggled on; the VB is
    // created lazily at first use and grown (destroy + recreate at
    // 2x) when a frame's vertex count exceeds the capacity.
    cd::debug_line::LineBatch                debug_lines;
    // phase1058: GPU side ported to cd::debug_draw::Renderer — the
    // pipeline + lazily-grown VB + frames-in-flight-safe growth all
    // live in the library now (phase 1031/1034 prototype retired).
    cd::debug_draw::Renderer                 debug_draw;
    // phase1053: VT probe page table (moved out of the panel static
    // so the 3D atlas overlay can enumerate residents()).
    cd::virtual_textures::PageTable          vt_table { 4, 4 };
    std::uint32_t                            vt_applied_serial { 0 };
    std::array<cd::material::MaterialInstance, 2> composite_insts;
    cd::material::MaterialInstance           bloom_prefilter_inst;
    std::array<cd::material::MaterialInstance, 3> bloom_down_insts;
    std::array<cd::material::MaterialInstance, 3> bloom_up_insts;

    // Bloom mip chain
    cd::post::bloom::BloomMipChain            bloom_chain;

    // Meshes + BLAS / TLAS
    cd_studio::StudioMeshRegistry                   meshes;
    cd::rhi::AccelStructureHandle            current_tlas    {};
    std::deque<cd_studio::DeferredTlas>      tlas_destroy_queue;
    // phase1146 — DDGI (Dynamic Diffuse GI) GPU pipeline. Booted lazily
    // after the first TLAS build; the per-frame dispatch is gated on
    // fx.ddgi_on (default OFF, so the golden fixture path is untouched).
    cd_studio::StudioDdgiPipeline                     ddgi;
    // phase465-perprim: per-Sponza-prim representative reflection colour.
    // One entry per gltf_prim_range[i]; matches the multi-geometry BLAS
    // geometry ordering 1:1.  Used by rebuild_tlas_and_transition_depth's
    // geom_albedos_for callback to populate the per-(instance, geom)
    // SSBO so vegetation reflects green, fabric red, stone grey instead
    // of every Sponza hit sharing the single sandstone tint.
    std::vector<cd::math::Vec3f>             sponza_geom_albedos;
    // phase843-W8-BE-rt-bindless-texture-sampling: parallel array to
    // sponza_geom_albedos with the binding-13 slot index + binding-12
    // index_offset for each Sponza prim. Populated once at boot
    // (build path: after s.meshes loads) and stays static for the
    // process lifetime.
    std::vector<cd_studio::W8BEGeomMeta>     sponza_w8be_meta;
    std::vector<cd::math::Vec3f>             cesium_geom_albedos;
    // phase890-cesium-bindless-textures: per-geom W8BEGeomMeta for
    // CesiumMan prims (mesh_id=1) and the bindless-array slot index
    // where CesiumMan's albedos start. The shader uses
    // (albedo_tex_slot, mesh_id) to pick (slot, VB/IB pair).
    std::vector<cd_studio::W8BEGeomMeta>     cesium_w8be_meta;
    std::uint32_t                            cesium_bindless_slot_base { 0u };
    // phase1000-pbr-sphere-rt-texture: 1-element span returned by
    // w8be_metadata_for() for PBR sphere entities that have
    // ent.use_texture == true. mesh_id == 2 selects the shader's
    // analytical spherical-UV bindless-sample path; albedo_tex_slot
    // points at a known earth_albedo fallback slot in the bindless
    // array. Filled at bindless-slot build time below (right after
    // the fallback slot loop fills 255 with earth_albedo).
    std::vector<cd_studio::W8BEGeomMeta>     pbr_sphere_textured_meta;
    std::vector<GpuTexture2D>                custom_disk_textures;
    std::unordered_map<std::string, int>     custom_disk_tex_slots;
    GpuMesh                                  custom_gltf_mesh {};
    cd::rhi::AccelStructureHandle            blas_custom_gltf {};

    // World / scene / history
    cd::ecs::World                           ecs_world;
    cd::scene::Scene                         scene           { ecs_world };
    cd::editor::EditHistory                  history;
    std::deque<std::string>                  log;
    cd::world_container::World               cd_world;
    std::vector<SceneEntity>                 entities;
    // phase508-generic-gltf-path: 3rd load path. One entry per .gltf
    // discovered under assets/samples/Generic/ that was successfully run
    // through cd::asset::gltf::load_scene + cd::render::scene::
    // ingest_gltf_scene. Each GenericIngest owns the IngestResult so
    // on_shutdown can free every GPU handle deterministically.
    std::vector<cd_studio::GenericIngest>    generic_ingests;
    int                                      selected        { 0 };
    SelKind                                  selected_kind   { SelKind::kEntity };

    // Selection outline + gizmo
    cd::editor::SelectionOutline             outline;
    cd::editor::AxisGizmo                    gizmo;
    GizmoState                               gizmo_state;

    // Camera
    cd::camera::Camera                       cam;
    cd::scene::SceneCameraController         scene_cam;
    cd_studio::SampleAppState                app_state;

    // Audio
    cd_studio::AudioState                    audio_state;

    // Net sim
    cd::net::Throttle                        net_throttle    { 4.0F, 30.0F };
    cd::net::SnapshotBuffer<float>           net_snapbuf;
    cd::net::LatencyStats                    net_rtt;
    bool                                     net_enabled     { true };
    std::uint32_t                            net_sent        { 0 };
    std::uint32_t                            net_recv        { 0 };
    std::uint32_t                            net_drop        { 0 };
    std::uint64_t                            net_raw_bytes   { 0 };
    std::uint64_t                            net_wire_bytes  { 0 };
    cd::math::Random                         net_rng         { 0xC0FFEE42u };
    float                                    net_baseline    { 0.0F };
    double                                   net_t           { 0.0 };
    double                                   next_net_tick   { 0.0 };

    // Lights
    std::vector<LightRow>                    lights;
    cd::light::ClusterGrid                   cluster_grid;
    cd::light::ClusterGridDesc               cluster_desc;

    // AsyncStreamer (lambda captures streamer_completed/failed by reference;
    // EngineState is always heap-allocated so references are stable).
    std::atomic<std::uint32_t>               streamer_completed { 0 };
    std::atomic<std::uint32_t>               streamer_failed    { 0 };
    cd::asset::AsyncStreamer                 streamer;
    std::vector<cd::asset::AssetId>          streamer_tracked;
    std::uint64_t                            streamer_next_id   { 1 };

    // Random viz
    cd::math::Random                         rand_rng           { 0xA1B2C3D4u };
    Histogram                                hist_uniform;
    Histogram                                hist_normal;
    std::uint64_t                            next_random_refresh{ 0 };

    // Counter table / command palette / FX
    cd::core::CounterTable                   counters;
    cd::editor::CommandPalette               palette;
    bool                                     palette_visible    { false };
    std::string                              palette_query;
    cd_studio::StudioRenderFx                 fx;

    // phase1174 — behavior_designer fold (ADR-20260614). A cd::ui::*
    // overlay stack that renders the library BehaviorDesigner panel on top
    // of the ImGui dev UI. The Submitter (Route B inline GLSL) co-exists
    // with ImGui in the same composite/swapchain BGRA8 pass (depth OFF +
    // alpha blend ON).
    //
    // Held via unique_ptr (not a by-value member): cd::ui::renderer_rhi::
    // Submitter is PIMPL with an inline `= default` default ctor whose
    // unique_ptr<Impl> member needs Impl complete at the point EngineState's
    // own ctor is compiled — which it is not (Impl lives in the .cpp). The
    // ADR scope guard forbids touching the library header, so we side-step
    // the limitation consumer-side: unique_ptr<Submitter> only needs the
    // out-of-line ~Submitter() (declared), never sizeof(Impl). Heap-stored
    // after device_owner so it destructs before the device on teardown.
    std::unique_ptr<cd::ui::renderer_rhi::Submitter> ui_submitter;
    cd::ui::renderer::DrawBatcher            ui_batcher;
    cd::ui::widgets::Theme                   ui_widget_theme;
    cd::ui::font::Font                       ui_font;
    cd::editor::panel::behavior_designer::BehaviorDesigner bd_panel;
    // ZORUNLU golden invariant (ADR §5.1): the overlay record is gated by
    // (!kHideEditorUiForGolden && show_behavior_designer) so it NEVER
    // emits draws on a golden capture frame. Default OFF.
    bool                                     show_behavior_designer { false };

    // phase1177 — studio_ui fold (ADR-20260614 §3.4, Job C). The deleted
    // samples/ui/studio_ui Button/Slider/Label/Flex demo, re-homed as a
    // second consumer of the SAME ui_submitter/ui_batcher/ui_widget_theme/
    // ui_font overlay stack (no second Submitter — one overlay feeds both
    // panels). Same golden invariant: the record is gated by
    // (!kHideEditorUiForGolden && show_ui_widgets) so it NEVER emits draws
    // on a golden capture frame. Default OFF.
    bool                                     show_ui_widgets { false };
    cd::ui::layout::FlexTree                 uiw_tree;
    cd::ui::layout::NodeId                   uiw_root   { cd::ui::layout::kInvalidNode };
    cd::ui::layout::NodeId                   uiw_label  { cd::ui::layout::kInvalidNode };
    cd::ui::layout::NodeId                   uiw_button { cd::ui::layout::kInvalidNode };
    cd::ui::layout::NodeId                   uiw_slider { cd::ui::layout::kInvalidNode };
    cd::ui::widgets::Button                  uiw_btn { "Click me" };
    cd::ui::widgets::Slider                  uiw_sld { 0.5F };
    int                                      uiw_click_count { 0 };
    float                                    uiw_slider_val  { 0.5F };
    bool                                     uiw_tree_built  { false };

    // Post-fx settings (wired; dispatch queued)
    cd::post::gtao::Settings                  fx_gtao;
    cd::post::bloom::Settings                 fx_bloom;
    cd::post::ssr::Settings                   fx_ssr;
    cd::post::dof::CameraSettings             fx_dof;
    cd::post::motion_blur::Settings           fx_mblur;
    cd::post::taa::Settings                   fx_taa;
    cd::post::smaa::Settings                  fx_smaa;

    // Phase 511 — auto-exposure (Reinhard log-avg + EMA + EV→linear mul).
    // Created in on_init when the HDR target extent is known. The helper
    // stays default-constructed (no GpuReduction) when create() fails or
    // the integrator hasn't wired the per-frame descriptor write yet; in
    // that case current_ev() returns 0 and the bloom prefilter behaves as
    // before (exp2(0) = 1.0 = no EV scaling). See
    // engine/render/post/exposure/include/cd/post/exposure/Setup.hpp.
    cd::post::exposure::Setup                 auto_exposure;

    // GI wire-in (dispatch queued)
    cd::restir_di::Reservoir                 fx_restir_di_reservoir;
    cd::restir_gi::Reservoir                 fx_restir_gi_reservoir;
    cd::ddgi::GridConfig                     fx_ddgi_grid;

    // Camera feedback / TAA state
    PrevCamBasis                             prev_cam_basis;
    cd::math::Mat4f                          prev_vp_unjittered  { cd::math::Mat4f::identity() };
    bool                                     prev_vp_valid       { false };
    std::array<cd::rhi::ResourceState, 2>    history_states
        { cd::rhi::ResourceState::kUndefined, cd::rhi::ResourceState::kUndefined };

    // Queued libs (silenced via (void))
    cd::atmosphere::Parameters               fx_atmosphere;
    cd::light_shafts::Settings               fx_lshafts;
    cd::volumetric::clouds::Settings          fx_clouds;
    cd::volumetric::fog::GridConfig           fx_vfog;

    // Frame-loop misc
    bool                                     dock_initialised   { false };
    bool                                     mod_ctrl           { false };
    bool                                     mod_shift          { false };
    bool                                     needs_rebuild      { false };
    bool                                     velocity_initialised_on_gpu { false };
    bool                                     bloom_initialised_on_gpu    { false };
    std::uint64_t                            last_tlas_scene_hash        { 0 };
    std::uint32_t                            frame_idx          { 0 };
    bool                                     pie_playing        { false };
    bool                                     anim_paused        { false };
    float                                    anim_speed         { 1.0F };

    // phase1178 — cd::ui InputState wiring (pointer flatten for overlay panels).
    // Updated each frame in the event loop from platform OSEvents so the
    // overlay record block (gated by !kHideEditorUiForGolden) can build a
    // real InputState for uiw_btn/uiw_sld tick(). Golden-neutral: these
    // fields are only USED inside the golden gate; mutation in the event loop
    // is always safe (values don't affect rendered pixels when the gate is off).
    float                                    ui_mouse_x         { 0.0F };
    float                                    ui_mouse_y         { 0.0F };
    bool                                     ui_lmb_down        { false };
    bool                                     ui_lmb_prev_down   { false };

    explicit EngineState()
        : streamer(
            [this](cd::asset::AssetId id) -> bool
            {
                const auto v = id.value();
                std::this_thread::sleep_for(
                    std::chrono::milliseconds(
                        120 + static_cast<std::int64_t>(v % 5) * 80));
                const bool ok = (v % 17 != 0);
                if (ok) streamer_completed.fetch_add(1, std::memory_order_relaxed);
                else    streamer_failed.fetch_add(1, std::memory_order_relaxed);
                return ok;
            })
    {}

    EngineState(const EngineState&)            = delete;
    EngineState& operator=(const EngineState&) = delete;
    EngineState(EngineState&&)                 = delete;
    EngineState& operator=(EngineState&&)      = delete;
};

// ---- Ctor / Dtor -----------------------------------------------------------
StudioApp::StudioApp() noexcept
    : cd::app::App(cd::app::AppConfig {
          .title             = "CHROMODYNAMIC 3D ENGINE STUDIO",
          .window_width      = 1600,
          .window_height     = 900,
          .start_maximized   = false,
          .enable_validation = (cd::core::compat::read_env_var("CD_VK_VALIDATION") == "1"),
          .max_frames        = 0u,
      })
{
}

StudioApp::~StudioApp() = default;

// ---- M2C: on_boot -----------------------------------------------------------
cd::core::Result<void> StudioApp::on_boot()
{
    state_ = std::make_unique<EngineState>();
    auto& s = *state_;

    // Window + Vulkan device + Renderer + ImGui
    cd::platform::WindowDesc wd {};
    wd.title     = "CHROMODYNAMIC 3D ENGINE STUDIO";
    wd.width     = 1600;
    wd.height    = 900;
    wd.frameless = true;
    auto window_r = cd::platform::create_window(wd);
    if (!window_r.has_value()) return std::unexpected(window_r.error());
    s.window = std::move(*window_r);
    auto& window = *s.window;

    cd::rhi::vulkan::VulkanCreateInfo vci {};
    vci.enable_validation = (cd::core::compat::read_env_var("CD_VK_VALIDATION") == "1");
    auto dev_r = cd::rhi::vulkan::create_vulkan_device(vci);
    if (!dev_r.has_value()) return std::unexpected(dev_r.error());
    s.device_owner = std::move(*dev_r);
    auto& device = *s.device_owner;

    cd::render::RendererDesc rd {};
    rd.device                    = &device;
    rd.swapchain.window_handle   = window.native_window_handle();
    rd.swapchain.display_handle  = window.native_display_handle();
    rd.swapchain.extent          = { window.width(), window.height() };
    rd.swapchain.format          = cd::rhi::Format::kBGRA8Unorm;
    rd.swapchain.image_count     = 3;
    rd.swapchain.vsync           = (cd::core::compat::read_env_var("CD_VSYNC") == "1");
    rd.frames_in_flight          = 1;
    auto renderer_r = cd::render::Renderer::create(rd);
    if (!renderer_r.has_value()) return std::unexpected(renderer_r.error());
    s.renderer = std::move(*renderer_r);

    cd::imgui::InitDesc imgui_id {};
    imgui_id.window           = &window;
    imgui_id.device           = &device;
    imgui_id.color_format     = cd::rhi::Format::kBGRA8Unorm;
    imgui_id.frames_in_flight = 1;
    auto ctx_r = cd::imgui::Context::create(imgui_id);
    if (!ctx_r.has_value()) return std::unexpected(ctx_r.error());
    s.imgui_ctx = std::move(*ctx_r);

    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    // phase1174 — behavior_designer fold (ADR-20260614, Job A boot wiring).
    //
    // Route B (create_with_inline_shader, Phase 554): an INLINE-compiled
    // GLSL pipeline that records DrawBatcher quads straight into the
    // composite/swapchain pass. color_format MUST match the swapchain
    // (kBGRA8Unorm) — see ADR Kanıt 5 + §5.1 format contract. The
    // create() never runs on the golden path's hot loop; a failure here is
    // logged + skipped (overlay stays disabled) so the golden capture is
    // unaffected (ADR §5.5).
    {
        const auto ttf = find_system_font_for_bd();
        if (!ttf.empty() &&
            s.ui_font.load_ttf_in_memory(
                std::span<const std::uint8_t>(ttf.data(), ttf.size())))
        {
            static constexpr float         kPixelSize = 15.0F;
            static constexpr std::uint32_t kAtlasDim  = 1024U;
            (void)s.ui_font.rasterize_range(0x0020U, 0x00FFU, kPixelSize, kAtlasDim);
        }
        s.ui_widget_theme = build_dark_widget_theme();

        // phase1177 — studio_ui fold: build the Button/Slider/Label demo
        // tree once. Device-independent (pure CPU flex tree); rebuilt never.
        uiw_build_tree(s.uiw_tree, s.uiw_root, s.uiw_label, s.uiw_button, s.uiw_slider);
        s.uiw_tree_built = true;
        s.uiw_btn.set_on_click([&s] { ++s.uiw_click_count; });
        s.uiw_sld.set_on_change([&s](float v) { s.uiw_slider_val = v; });

        cd::ui::renderer_rhi::SubmitterCreateInfo sci {};
        sci.color_format = cd::rhi::Format::kBGRA8Unorm;
        sci.max_vertices = 16384U;
        sci.max_indices  = 65536U;
        auto sub_r =
            cd::ui::renderer_rhi::Submitter::create_with_inline_shader(device, sci);
        if (sub_r.has_value())
        {
            s.ui_submitter = std::make_unique<cd::ui::renderer_rhi::Submitter>(
                std::move(*sub_r));
            // Seed a non-trivial BT panel state (mirrors the deleted
            // studio_behavior_designer demo): demo BT root + child select +
            // pan. Renders the Sprint-1 demo-node graph when toggled ON.
            s.bd_panel.set_tree_root(1U);      // demo BT root
            s.bd_panel.set_selected(2U);       // select Child A (accent border)
            s.bd_panel.set_pan_offset(20.0F, 10.0F);
            std::printf("chromodynamic_studio: BehaviorDesigner overlay ready "
                        "(cd::ui Submitter Route B; toggle in R-Showcase).\n");
        }
        else
        {
            std::printf("chromodynamic_studio: BehaviorDesigner overlay disabled "
                        "(Submitter create failed: %.*s).\n",
                        static_cast<int>(sub_r.error().message.size()),
                        sub_r.error().message.data());
        }
    }

    // Shader compiler
    s.compiler = cd::shader::make_glslang_compiler();
    if (!s.compiler)
        return std::unexpected(cd::core::ErrorCode { 0, 5, "shader compiler" });

    // Render targets
    using cd_studio::kDepthFormat;
    if (int rc = cd_studio::create_render_targets(
            device, { window.width(), window.height() }, s.rts); rc != 0)
        return std::unexpected(
            cd::core::ErrorCode { 0, static_cast<std::uint32_t>(rc), "render targets" });

    // Materials
    auto mat_r = cd_studio::spawn_materials(device, s.compiler.get());
    if (!mat_r.has_value())
        return std::unexpected(
            cd::core::ErrorCode { 0,
                static_cast<std::uint32_t>(mat_r.error().exit_code), "materials" });
    s.materials = std::move(*mat_r);

    // phase1058: debug-line GPU renderer via cd::debug_draw. The MRT
    // fragment shader (4 attachments with neutral G-buffer writes)
    // comes from LineShader.hpp; on-disk copies win for hot-reload.
    {
        constexpr std::array<cd::rhi::Format, 4> kLineColorFmts {
            cd::rhi::Format::kRGBA16Float,
            cd::rhi::Format::kRGBA16Float,
            cd::rhi::Format::kRGBA8Unorm,
            cd::rhi::Format::kRG8Unorm
        };
        cd::debug_draw::RendererDesc dd {};
        dd.color_attachment_formats = kLineColorFmts;
        dd.depth_attachment_format  = cd::rhi::Format::kD32Float;
        dd.vertex_glsl   = cd::studio_rt::kLineVS;
        dd.fragment_glsl = cd::studio_rt::kLineFS;
#if STUDIO_USE_ON_DISK_SHADERS
        // X5 step 2: source-tree resolution (same as the watch entry
        // below). Locals must outlive Renderer::create (glsl_path is a
        // view into them).
        const std::string line_vert_path =
            cd_studio::resolve_shader_path(cd_studio::kLineVertGlslPath);
        const std::string line_frag_path =
            cd_studio::resolve_shader_path(cd_studio::kLineFragGlslPath);
        dd.vertex_glsl_path   = line_vert_path;
        dd.fragment_glsl_path = line_frag_path;
#endif
        dd.name = "chromodynamic_studio/debug_line";
        auto dd_r = cd::debug_draw::Renderer::create(
            device, s.compiler.get(), dd);
        if (!dd_r.has_value())
            return std::unexpected(cd::core::ErrorCode { 0, 11U, "debug_line" });
        s.debug_draw = std::move(*dd_r);
#if STUDIO_USE_ON_DISK_SHADERS
        // phase1060: line shaders join the X5 hot-reload watch. The
        // closure rebuilds the SAME desc (statics — the boot-scope
        // arrays would dangle) and swaps only the pipeline; VB +
        // parked queue carry no shader state and stay put. A broken
        // edit keeps the old pipeline (recreate_pipeline contract).
        s.shader_watch.add_entry(
            nullptr,
            std::vector<std::string> {
                cd_studio::resolve_shader_path(cd_studio::kLineVertGlslPath),
                cd_studio::resolve_shader_path(cd_studio::kLineFragGlslPath) },
            [this](cd::rhi::IDevice& d, cd::shader::ICompiler* c) -> bool
            {
                static constexpr std::array<cd::rhi::Format, 4> kFmts {
                    cd::rhi::Format::kRGBA16Float,
                    cd::rhi::Format::kRGBA16Float,
                    cd::rhi::Format::kRGBA8Unorm,
                    cd::rhi::Format::kRG8Unorm
                };
                cd::debug_draw::RendererDesc rd {};
                rd.color_attachment_formats = kFmts;
                rd.depth_attachment_format  = cd::rhi::Format::kD32Float;
                rd.vertex_glsl   = cd::studio_rt::kLineVS;
                rd.fragment_glsl = cd::studio_rt::kLineFS;
                // X5 step 2: same source-tree resolution as the watch
                // paths; locals must outlive recreate_pipeline (view).
                const std::string rl_vert_path =
                    cd_studio::resolve_shader_path(cd_studio::kLineVertGlslPath);
                const std::string rl_frag_path =
                    cd_studio::resolve_shader_path(cd_studio::kLineFragGlslPath);
                rd.vertex_glsl_path   = rl_vert_path;
                rd.fragment_glsl_path = rl_frag_path;
                rd.name = "chromodynamic_studio/debug_line";
                return state_->debug_draw.recreate_pipeline(d, c, rd);
            },
            "debug_line");
#endif
    }

    // Shader hot-reload entries. X5 step 2: watch paths go through the
    // SAME resolve_shader_path the *_recreate compile paths use, so the
    // watched file and the compiled file can never diverge (dev builds
    // watch the SOURCE tree; shipped builds fall back to exe-side).
    s.shader_watch.add_entry(
        &s.materials.prim,
        std::vector<std::string> {
            cd_studio::resolve_shader_path(cd_studio::kPrimVertGlslPath),
            cd_studio::resolve_shader_path(cd_studio::kPrimFragGlslPath) },
        [this](cd::rhi::IDevice& d, cd::shader::ICompiler* c) -> bool
        {
            // Boot/reload parity: spawn_materials passes prim_bindless_layout
            // so the prim pipeline carries the dedicated set=1 bindless
            // sampler-array layout (phase864). The reload closure MUST pass it
            // too — omitting it recreates a pipeline WITHOUT set 1 and the
            // bindless path breaks silently on the first prim hot-reload.
            return cd_studio::prim_recreate(d, c, &state_->materials.prim,
                                            state_->materials.prim_bindless_layout);
        },
        "prim");
    s.shader_watch.add_entry(
        &s.materials.shadow,
        std::vector<std::string> {
            cd_studio::resolve_shader_path(cd_studio::kShadowVertGlslPath),
            cd_studio::resolve_shader_path(cd_studio::kShadowFragGlslPath) },
        [this](cd::rhi::IDevice& d, cd::shader::ICompiler* c) -> bool
        { return cd_studio::shadow_recreate(d, c, &state_->materials.shadow); },
        "shadow");

    // phase1129 (on_boot extraction batch 5): shadow/light/SSBO/IBL boot
    // resources live in StudioBootGpu.hpp; the byte-size constants are
    // shared from there (single definition for boot + descriptors).
    if (auto gb = cd_studio::setup_boot_gpu_buffers(s, device); !gb.has_value())
        return std::unexpected(gb.error());
    constexpr std::uint32_t kLightUboBytes = cd_studio::kBootLightUboBytes;
    using cd::studio_rt::kInstMatBytes;

    // phase1140 (on_boot extraction batch 6): procedural textures +
    // albedo sampler live in StudioBootGpu.hpp.
    if (auto pt = cd_studio::setup_boot_procedural_textures(s, device);
        !pt.has_value())
        return std::unexpected(pt.error());

    // phase1126 (on_boot extraction batch 4): prim instance + 10-binding
    // boot descriptor write live in StudioBootBindless.hpp.
    if (auto pr_r = cd_studio::init_prim_inst_descriptors(
            s, device, kLightUboBytes, cd::studio_rt::kInstMatBytes);
        !pr_r.has_value())
        return std::unexpected(pr_r.error());

    // phase1125 (on_boot extraction batch 3): composite/bloom/exposure
    // instances + the bloom/composite descriptor binders live in
    // StudioBootPostFx.hpp (binders shared with the on_frame resize path,
    // which previously carried duplicated lambdas).
    if (auto pf = cd_studio::setup_boot_postfx_instances(
            s, device, { window.width(), window.height() });
        !pf.has_value())
        return std::unexpected(pf.error());
    cd_studio::bind_bloom_descriptors(s);
    cd_studio::bind_composite_hdr_descriptors(s);

    // Meshes + glTF + BLAS
    auto upload_alb_fn = [&device_ref = device](
        const std::uint8_t* rgba, std::uint32_t w, std::uint32_t h)
        -> std::pair<cd::rhi::TextureHandle, cd::rhi::TextureViewHandle>
    {
        auto t = create_texture_rgba8(device_ref, rgba, w, h);
        return { t.image, t.view };
    };
    cd_studio::AlbedoSlot alb_slot {
        .image_io = &s.albedo_tex.image,
        .view_io  = &s.albedo_tex.view,
        .sampler  = s.albedo_sampler,
        .upload   = upload_alb_fn,
    };
    const bool eager_golden_boot = cd::studio_rt::golden::enabled();
    s.meshes = cd_studio::boot_meshes(device, alb_slot, s.prim_inst, s.materials.prim,
                                      s.normal_tex.view, s.mr_tex.view,
                                      eager_golden_boot);
    if (s.meshes.build_exit_code != 0)
        return std::unexpected(
            cd::core::ErrorCode { 0, static_cast<std::uint32_t>(s.meshes.build_exit_code), "meshes" });
    if (s.meshes.has_gltf_texture || s.meshes.has_cesium_texture)
        s.has_gltf_texture = true;

    // phase1124 (on_boot extraction batch 2): W8-BE bindless descriptor
    // wiring + per-prim albedo/meta tables live in StudioBootBindless.hpp
    // (chrome-probe golden net pins the behaviour).
    cd_studio::wire_w8be_bindless_and_meta(
        s, device, kLightUboBytes, cd::studio_rt::kInstMatBytes);

    // phase1146 (on_boot extraction batch 7): world container + .cdproject
    // adoption + CD_HDR_LANES env read live in StudioBootProject.hpp
    cd_studio::setup_boot_world_and_project(s);
    if (eager_golden_boot)
    {
        spawn_primitive_seeds(s.scene, s.entities);
        const bool chrome_probe_mode =
            cd::studio_rt::golden::enabled() &&
            cd::studio_rt::golden::options().fixture_index == 5;
        spawn_sponza_and_cesiumman_entities(
            s.scene,
            s.meshes.gltf,          s.meshes.gltf_loaded_name,
            chrome_probe_mode ? cd::render::GpuMesh {} : s.meshes.gltf_cesium,
            chrome_probe_mode ? std::string_view {}    : s.meshes.gltf_cesium_loaded_name,
            s.entities);
        spawn_pbr_grid_entities(s.scene, s.entities);
        if (chrome_probe_mode)
        {
            spawn_chrome_probe_entity(s.scene, s.entities);
        }
        auto early_log = [&s](std::string msg)
        {
            s.log.emplace_back(std::move(msg));
            while (s.log.size() > 64) s.log.pop_front();
        };
        s.generic_ingests = cd_studio::load_generic_gltf_scenes(
            *s.device_owner, s.ecs_world, s.scene, early_log);
        if (!s.meshes.gltf_loaded_name.empty() &&
            s.meshes.gltf_loaded_name.find("Sponza") != std::string::npos)
        {
            s.show_editor_floor = false;
        }
    }

    auto log_push_fn = [&s](std::string msg)
    {
        s.log.emplace_back(std::move(msg));
        while (s.log.size() > 64) s.log.pop_front();
    };
    log_push_fn(
        std::string { "[boot] " } + std::to_string(s.entities.size()) +
        " ECS entities spawned (Empty_Stage — showcases load on demand)");

    // Selection + gizmo
    s.outline.style = cd::editor::OutlineStyle::kWireframe;

    // phase1123 (on_boot extraction batch 1): camera defaults + the
    // phase543/797 golden-fixture pin live in StudioBootScene.hpp.
    cd_studio::setup_boot_camera(s, log_push_fn);

    // Audio
    cd_studio::init_audio(s.audio_state, square_wave, burst_noise);

    // phase1123 (on_boot extraction batch 1): W8-BB light rig (Sponza
    // indoor preset vs open-air defaults, phase455 calibration notes) +
    // cluster grid config live in StudioBootScene.hpp.
    cd_studio::setup_boot_lights_and_clusters(s);

    // phase1146 (on_boot extraction batch 7): AsyncStreamer start + the
    // random-viz histogram first fill live in StudioBootProject.hpp.
    cd_studio::start_boot_streamer_and_random_viz(s);

    // phase1146 (on_boot extraction batch 7): the fx/engineering palette
    // buckets + edit/select/transform/camera/audio/net/random/help commands
    // live in StudioBootProject.hpp. The Scene: Save/Load + Gizmo + Streamer
    // commands below stay here — they close over the file-local SceneEntity
    // / LightRow / PrimitiveKind types (StudioEngineCommands.hpp scope rule)
    // and their registration order vs the duplicate FX ids must hold.
    cd_studio::register_boot_palette_commands(s, log_push_fn);

    constexpr const char* kSavePath = "studio_scene.cdscene.json";
    s.palette.register_command(80, "Scene: Save",
        [&s, log_push_fn]() mutable
        {
            (void)cd::studio::save_full_studio_scene(s, kSavePath, log_push_fn);
        });
    s.palette.register_command(95, "Gizmo: Toggle Visibility",
        [&s, log_push_fn]() mutable
        {
            s.gizmo_state.visible = !s.gizmo_state.visible;
            log_push_fn("[gizmo] visible=" + std::string(s.gizmo_state.visible ? "true" : "false"));
        });
    s.palette.register_command(90, "Streamer: Enqueue 8 burst",
        [&s, log_push_fn]() mutable
        {
            for (int i = 0; i < 8; ++i)
            {
                cd::asset::AssetId id { s.streamer_next_id++ };
                cd::asset::StreamRequest req; req.id = id; req.priority = i * 10;
                s.streamer.enqueue(req); s.streamer_tracked.push_back(id);
                if (s.streamer_tracked.size() > 32)
                    s.streamer_tracked.erase(s.streamer_tracked.begin(),
                                             s.streamer_tracked.begin() + 8);
            }
            log_push_fn("[palette] Streamer +8 burst");
        });
    s.palette.register_command(91, "Streamer: Enqueue 32 burst",
        [&s, log_push_fn]() mutable
        {
            for (int i = 0; i < 32; ++i)
            {
                cd::asset::AssetId id { s.streamer_next_id++ };
                cd::asset::StreamRequest req; req.id = id; req.priority = i % 4;
                s.streamer.enqueue(req); s.streamer_tracked.push_back(id);
                if (s.streamer_tracked.size() > 32)
                    s.streamer_tracked.erase(s.streamer_tracked.begin(),
                                             s.streamer_tracked.begin() + 8);
            }
            log_push_fn("[palette] Streamer +32 burst");
        });
    s.palette.register_command(81, "Scene: Load (replace world)",
        [&s, log_push_fn]() mutable
        {
            if (cd::studio::load_full_studio_scene(s, kSavePath, log_push_fn))
            {
                s.history.clear();
            }
        });

    return {};
}

// ---- M2D: on_frame ----------------------------------------------------------
void StudioApp::on_frame(const cd::app::FrameContext& /*fc*/)
{
    auto& s      = *state_;
    auto& window = *s.window;
    auto& device = *s.device_owner;
    auto& ctx    = *s.imgui_ctx;

    constexpr cd::rhi::Extent2D kShadowMapSize { 2048, 2048 };
    constexpr float kFloorY    = -0.55F;
    constexpr float kShadowLift = 0.01F;
    using cd_studio::kDepthFormat;
    using cd_studio::kAudioRingFrames;
    using cd::studio_rt::kInstMatBytes;

    auto log_push_fn = [&s](std::string msg)
    {
        s.log.emplace_back(std::move(msg));
        while (s.log.size() > 64) s.log.pop_front();
    };

    auto mesh_for = [&s](PrimitiveKind k) -> const GpuMesh&
    {
        switch (k)
        {
            case PrimitiveKind::kSphere:    return s.meshes.sphere;
            case PrimitiveKind::kCone:      return s.meshes.cone;
            case PrimitiveKind::kCylinder:  return s.meshes.cyl;
            case PrimitiveKind::kTorus:     return s.meshes.torus;
            case PrimitiveKind::kTorusKnot: return s.meshes.knot;
            case PrimitiveKind::kCustomGltf:
                return s.custom_gltf_mesh.vb.is_valid() ? s.custom_gltf_mesh : s.meshes.knot;
            case PrimitiveKind::kSponza:
                return s.meshes.gltf.vb.is_valid() ? s.meshes.gltf : s.meshes.knot;
            case PrimitiveKind::kGltf:
                return s.meshes.gltf_cesium.vb.is_valid() ? s.meshes.gltf_cesium : s.meshes.knot;
            default:                        return s.meshes.cube;
        }
    };
    auto blas_for_kind = [&s](PrimitiveKind k) -> cd::rhi::AccelStructureHandle
    {
        switch (k)
        {
            case PrimitiveKind::kSphere:     return s.meshes.blas_sphere;
            case PrimitiveKind::kCone:       return s.meshes.blas_cone;
            case PrimitiveKind::kCylinder:   return s.meshes.blas_cyl;
            case PrimitiveKind::kTorus:      return s.meshes.blas_torus;
            case PrimitiveKind::kTorusKnot:  return s.meshes.blas_knot;
            case PrimitiveKind::kCustomGltf:
                return s.blas_custom_gltf.is_valid() ? s.blas_custom_gltf : s.meshes.blas_knot;
            case PrimitiveKind::kSponza:     return s.meshes.blas_gltf;
            case PrimitiveKind::kGltf:       return s.meshes.blas_cesium;
            default:                         return s.meshes.blas_cube;
        }
    };

    // phase1125: bloom/composite descriptor binders moved to
    // StudioBootPostFx.hpp (shared with on_boot — were duplicated here).
    using clock = std::chrono::steady_clock;
    const auto frame_loop_start = clock::now();
    auto last_tick = frame_loop_start;
    std::vector<cd::platform::OSEvent> events;
    events.reserve(64);
    if (g_studio_demo_all_phases)
    {
        cd::studio::load_studio_interactive_template(s, 1, log_push_fn);
        cd::studio::studio_world().environment_modifiers.add_or_replace(
            std::make_unique<cd::studio::FogModifier>());
        (void)cd::studio::export_standalone_game_package(
            s, "build/export/ChromodynamicGame", log_push_fn);
    }

    while (true)
    {
        events.clear();
        if (!window.pump_events(events)) break;

        if ((s.frame_idx & 63u) == 0u)
        {
            (void)s.shader_watch.poll_and_reload(device, s.compiler.get());
        }

        for (const auto& ev : events)
        {
            ctx.handle_event(ev);
            if (ev.kind == cd::platform::OSEventKind::kKeyDown &&
                ev.key == cd::platform::KeyCode::kEscape)
            {
                if (s.pie_playing)
                { cd::studio::exit_pie_play_mode(s, log_push_fn); }
                else if (s.gizmo.is_dragging())
                { (void)s.gizmo.end_drag(); log_push_fn("[esc] gizmo drag cancelled"); }
                else if (s.palette_visible)
                { s.palette_visible = false; s.palette_query.clear();
                  log_push_fn("[esc] palette closed"); }
                else if (s.selected >= 0)
                { s.selected = -1; log_push_fn("[esc] selection cleared"); }
            }
            else if (ev.kind == cd::platform::OSEventKind::kResize)
            { s.needs_rebuild = true; }
            else if (ev.kind == cd::platform::OSEventKind::kKeyDown &&
                     ev.key == cd::platform::KeyCode::kF1)
            {
                s.palette_visible = !s.palette_visible;
                if (s.palette_visible) s.palette_query.clear();
            }
            else if (ev.kind == cd::platform::OSEventKind::kKeyDown)
            {
                if (ev.key == cd::platform::KeyCode::kLCtrl ||
                    ev.key == cd::platform::KeyCode::kRCtrl)  s.mod_ctrl  = true;
                if (ev.key == cd::platform::KeyCode::kLShift ||
                    ev.key == cd::platform::KeyCode::kRShift) s.mod_shift = true;
                if (ev.key == cd::platform::KeyCode::kP && s.mod_ctrl && s.mod_shift)
                { s.palette_visible = !s.palette_visible;
                  if (s.palette_visible) s.palette_query.clear(); }
            }
            else if (ev.kind == cd::platform::OSEventKind::kKeyUp)
            {
                if (ev.key == cd::platform::KeyCode::kLCtrl ||
                    ev.key == cd::platform::KeyCode::kRCtrl)  s.mod_ctrl  = false;
                if (ev.key == cd::platform::KeyCode::kLShift ||
                    ev.key == cd::platform::KeyCode::kRShift) s.mod_shift = false;
            }

            const bool key_dn = (ev.kind == cd::platform::OSEventKind::kKeyDown);
            const bool key_up = (ev.kind == cd::platform::OSEventKind::kKeyUp);
            const bool ui_captures_kb = ImGui::GetIO().WantCaptureKeyboard;
            auto& fl = s.app_state.free_look;
            if (ui_captures_kb || s.pie_playing)
            {
                fl.key_w = fl.key_a = fl.key_s = fl.key_d = fl.key_q = fl.key_e = false;
            }
            else if (key_dn || key_up)
            {
                const bool v = key_dn;
                if (ev.key == cd::platform::KeyCode::kW)  fl.key_w = v;
                if (ev.key == cd::platform::KeyCode::kA)  fl.key_a = v;
                if (ev.key == cd::platform::KeyCode::kS)  fl.key_s = v;
                if (ev.key == cd::platform::KeyCode::kD)  fl.key_d = v;
                if (ev.key == cd::platform::KeyCode::kQ)  fl.key_q = v;
                if (ev.key == cd::platform::KeyCode::kE)  fl.key_e = v;
                if (ev.key == cd::platform::KeyCode::kLShift ||
                    ev.key == cd::platform::KeyCode::kRShift) fl.key_shift = v;
                if (ev.key == cd::platform::KeyCode::kLCtrl ||
                    ev.key == cd::platform::KeyCode::kRCtrl)  fl.key_ctrl  = v;
                if (key_dn && (ev.key == cd::platform::KeyCode::kW ||
                               ev.key == cd::platform::KeyCode::kA ||
                               ev.key == cd::platform::KeyCode::kS ||
                               ev.key == cd::platform::KeyCode::kD ||
                               ev.key == cd::platform::KeyCode::kQ ||
                               ev.key == cd::platform::KeyCode::kE))
                { fl.manual_mode = true; s.scene_cam.set_auto_spin(false); }
            }
            if (key_dn && ev.key == cd::platform::KeyCode::kF &&
                !ui_captures_kb && !s.pie_playing &&
                s.selected >= 0 && std::cmp_less(s.selected, s.entities.size()))
            {
                if (auto* lt = s.scene.local(
                        s.entities[static_cast<std::size_t>(s.selected)].handle))
                {
                    s.cam.target = { lt->value.position.x, lt->value.position.y,
                                     lt->value.position.z };
                    log_push_fn("[cam] focus " +
                        s.entities[static_cast<std::size_t>(s.selected)].name);
                }
            }
            if (key_dn && ev.key == cd::platform::KeyCode::kSpace &&
                !ui_captures_kb && !s.pie_playing)
            {
                s.gizmo_state.mode = static_cast<GizmoMode>(
                    (static_cast<std::uint8_t>(s.gizmo_state.mode) + 1u) % 3u);
                const char* ms = "SCALE";
                if (s.gizmo_state.mode == GizmoMode::kTranslate)
                    ms = "TRANSLATE";
                else if (s.gizmo_state.mode == GizmoMode::kRotate)
                    ms = "ROTATE";
                log_push_fn(std::string { "[gizmo] mode: " } + ms);
            }
            if (key_dn && ev.key == cd::platform::KeyCode::kDelete &&
                !ui_captures_kb && !s.pie_playing && s.selected >= 0)
            {
                cd::studio::delete_selected(s, log_push_fn);
            }
            if (ev.kind == cd::platform::OSEventKind::kMouseButtonDown)
            {
                if (ev.mouse_button == cd::platform::MouseButton::kRight)
                {
                    fl.right_drag = true; fl.manual_mode = true;
                    fl.has_last_mouse = false;
                    s.scene_cam.set_auto_spin(false);
                    const float dx = s.cam.target.x - s.cam.eye.x;
                    const float dy = s.cam.target.y - s.cam.eye.y;
                    const float dz = s.cam.target.z - s.cam.eye.z;
                    const float dist = std::sqrt(dx*dx + dy*dy + dz*dz);
                    if (dist > 1e-3F)
                    {
                        fl.dist  = dist;
                        fl.pitch = std::asin(std::clamp(dy / dist, -1.0F, 1.0F));
                        fl.yaw   = std::atan2(dx, -dz);
                    }
                }
                else if (ev.mouse_button == cd::platform::MouseButton::kMiddle &&
                         !ImGui::GetIO().WantCaptureMouse)
                {
                    fl.middle_drag = true; fl.manual_mode = true;
                    fl.has_last_mouse = false;
                    s.scene_cam.set_auto_spin(false);
                    const float dx = s.cam.target.x - s.cam.eye.x;
                    const float dy = s.cam.target.y - s.cam.eye.y;
                    const float dz = s.cam.target.z - s.cam.eye.z;
                    const float dist = std::sqrt(dx*dx + dy*dy + dz*dz);
                    if (dist > 1e-3F)
                    {
                        fl.dist  = dist;
                        fl.pitch = std::asin(std::clamp(dy / dist, -1.0F, 1.0F));
                        fl.yaw   = std::atan2(dx, -dz);
                    }
                }
                else if (ev.mouse_button == cd::platform::MouseButton::kLeft &&
                         !ImGui::GetIO().WantCaptureMouse)
                {
                    s.app_state.pick.pending = true;
                    s.app_state.pick.x = ev.mouse_x;
                    s.app_state.pick.y = ev.mouse_y;
                }
                // phase1178: track LMB down for cd::ui InputState flatten.
                if (ev.mouse_button == cd::platform::MouseButton::kLeft)
                    s.ui_lmb_down = true;
            }
            if (ev.kind == cd::platform::OSEventKind::kMouseButtonUp &&
                ev.mouse_button == cd::platform::MouseButton::kRight)
                fl.right_drag = false;
            if (ev.kind == cd::platform::OSEventKind::kMouseButtonUp &&
                ev.mouse_button == cd::platform::MouseButton::kMiddle)
                fl.middle_drag = false;
            // phase1178: track LMB up for cd::ui InputState flatten.
            if (ev.kind == cd::platform::OSEventKind::kMouseButtonUp &&
                ev.mouse_button == cd::platform::MouseButton::kLeft)
                s.ui_lmb_down = false;
            if (ev.kind == cd::platform::OSEventKind::kMouseMove)
            {
                if (fl.right_drag && fl.has_last_mouse)
                {
                    const float ddx = ev.mouse_x - fl.last_mouse_x;
                    const float ddy = ev.mouse_y - fl.last_mouse_y;
                    fl.yaw   += ddx * cd_studio::kCamLookSpeed;
                    fl.pitch -= ddy * cd_studio::kCamLookSpeed;
                    constexpr float kHalfPi = 1.5707963F;
                    fl.pitch = std::min(fl.pitch, kHalfPi - 0.05F);
                    fl.pitch = std::max(fl.pitch, -kHalfPi + 0.05F);
                }
                if (fl.middle_drag && fl.has_last_mouse && !s.pie_playing)
                {
                    const float ddx = ev.mouse_x - fl.last_mouse_x;
                    const float ddy = ev.mouse_y - fl.last_mouse_y;
                    cd::math::Vec3f fwd { s.cam.target.x - s.cam.eye.x,
                                          s.cam.target.y - s.cam.eye.y,
                                          s.cam.target.z - s.cam.eye.z };
                    const float flen = std::sqrt(fwd.x * fwd.x + fwd.y * fwd.y + fwd.z * fwd.z);
                    if (flen > 1e-5F)
                    {
                        fwd.x /= flen;
                        fwd.y /= flen;
                        fwd.z /= flen;
                        const cd::math::Vec3f wup { 0.0F, 1.0F, 0.0F };
                        cd::math::Vec3f rgt { fwd.y * wup.z - fwd.z * wup.y,
                                              fwd.z * wup.x - fwd.x * wup.z,
                                              fwd.x * wup.y - fwd.y * wup.x };
                        const float rlen = std::sqrt(rgt.x * rgt.x + rgt.y * rgt.y + rgt.z * rgt.z);
                        if (rlen > 1e-5F)
                        {
                            rgt.x /= rlen;
                            rgt.y /= rlen;
                            rgt.z /= rlen;
                        }
                        else
                        {
                            rgt = { 1.0F, 0.0F, 0.0F };
                        }
                        const cd::math::Vec3f up_v { rgt.y * fwd.z - rgt.z * fwd.y,
                                                     rgt.z * fwd.x - rgt.x * fwd.z,
                                                     rgt.x * fwd.y - rgt.y * fwd.x };
                        const float pan_speed = std::max(0.8F, fl.dist) * 0.0018F;
                        const cd::math::Vec3f pan_delta {
                            (-rgt.x * ddx + up_v.x * ddy) * pan_speed,
                            (-rgt.y * ddx + up_v.y * ddy) * pan_speed,
                            (-rgt.z * ddx + up_v.z * ddy) * pan_speed };
                        s.cam.eye.x    += pan_delta.x;
                        s.cam.eye.y    += pan_delta.y;
                        s.cam.eye.z    += pan_delta.z;
                        s.cam.target.x += pan_delta.x;
                        s.cam.target.y += pan_delta.y;
                        s.cam.target.z += pan_delta.z;
                    }
                }
                fl.last_mouse_x = ev.mouse_x; fl.last_mouse_y = ev.mouse_y;
                fl.has_last_mouse = true;
                // phase1178: mirror cursor to ui_mouse for InputState flatten.
                s.ui_mouse_x = ev.mouse_x;
                s.ui_mouse_y = ev.mouse_y;
            }
            if (ev.kind == cd::platform::OSEventKind::kMouseWheel &&
                !ImGui::GetIO().WantCaptureMouse && !s.pie_playing)
            {
                fl.dist *= (ev.wheel > 0.0F) ? 0.9F : 1.1F;
                fl.dist = std::max(fl.dist, 1.0F);
                fl.dist = std::min(fl.dist, 100.0F);
                const float dx = s.cam.eye.x - s.cam.target.x;
                const float dy = s.cam.eye.y - s.cam.target.y;
                const float dz = s.cam.eye.z - s.cam.target.z;
                const float cur_d = std::sqrt(dx * dx + dy * dy + dz * dz);
                if (cur_d > 1e-4F)
                {
                    const float scale = fl.dist / cur_d;
                    s.cam.eye.x = s.cam.target.x + dx * scale;
                    s.cam.eye.y = s.cam.target.y + dy * scale;
                    s.cam.eye.z = s.cam.target.z + dz * scale;
                }
            }
        }  // end event loop

        if (s.needs_rebuild)
        {
            if (window.width() == 0 || window.height() == 0) continue;
            device.wait_idle();
            if (!s.renderer.recreate_swapchain({ window.width(), window.height() }).has_value())
                continue;
            s.rts.destroy(device);
            if (cd_studio::create_render_targets(
                    device, { window.width(), window.height() }, s.rts) != 0)
                continue;
            s.history_states[0] = cd::rhi::ResourceState::kUndefined;
            s.history_states[1] = cd::rhi::ResourceState::kUndefined;
            if (!create_bloom_chain(device, { window.width(), window.height() }, s.bloom_chain))
                continue;
            cd_studio::bind_bloom_descriptors(s);
            cd_studio::bind_composite_hdr_descriptors(s);
            // phase1146: the HDR / depth / normal views the DDGI sample pass
            // points at were just destroyed + recreated — force a rebind on
            // the next frame against the fresh views.
            s.ddgi.bound = false;
            s.depth_initialised_on_gpu = false;
            s.velocity_initialised_on_gpu = false;
            s.bloom_initialised_on_gpu = false;
            s.last_tlas_scene_hash = 0;
            s.needs_rebuild = false;
        }

        const auto now = clock::now();
        const float dt = std::chrono::duration<float>(now - last_tick).count();
        last_tick = now;

        // Tick audio (only when 3D audio waveform overlay is visible or on initial seed frame)
        auto& as = s.audio_state;
        if (!as.muted && (s.frame_idx == 0 || s.fx.audio_show_wave_3d))
        {
            float peak = 0.0F;
            float comp_db_min = 0.0F;
            float lim_gain_min = 1.0F;
            for (std::size_t si = 0; si < kAudioBufferLen; ++si, ++as.sample_t)
            {
                as.bus.mix(0, square_wave(as.sample_t, 440.0F));
                as.bus.mix(1, burst_noise(as.sample_t));
                float x = as.bus.pull();
                x = as.comp.process(x);
                comp_db_min = std::min(as.comp.gain_db(), comp_db_min);
                const float wet = as.reverb.process(x);
                x = 0.75F * x + 0.20F * wet;
                x = as.lowpass.process(x);
                x = as.limiter.process(x);
                lim_gain_min = std::min(as.limiter.current_gain(), lim_gain_min);
                peak = std::max(std::fabs(x), peak);
                x = std::min(x, 1.0F);
                x = std::max(x, -1.0F);
                as.ring[as.ring_write] = static_cast<std::int16_t>(x * 32760.0F);
                if (++as.ring_write >= kAudioRingFrames) as.ring_write = 0;
                ++as.total_written;
            }
            as.peak_window = peak; as.comp_db_window = comp_db_min;
            as.limiter_gain_min = lim_gain_min;
            as.meter_history.push_back(peak);
            while (as.meter_history.size() > 120) as.meter_history.pop_front();
            s.counters.increment("audio_ticks");
        }

        // Tick net sim
        if (s.net_enabled)
        {
            s.net_t += static_cast<double>(dt);
            s.net_throttle.update(dt);
            while (s.net_t >= s.next_net_tick)
            {
                s.next_net_tick += 1.0 / 60.0;
                if (s.net_throttle.try_consume(1.0F))
                {
                    const float v = std::sin(static_cast<float>(s.net_t) * 1.2F);
                    const std::byte cb[4] = {
                        std::byte((std::uint32_t(v * 1e6F) >> 0)  & 0xFFu),
                        std::byte((std::uint32_t(v * 1e6F) >> 8)  & 0xFFu),
                        std::byte((std::uint32_t(v * 1e6F) >> 16) & 0xFFu),
                        std::byte((std::uint32_t(v * 1e6F) >> 24) & 0xFFu) };
                    const std::byte bb[4] = {
                        std::byte((std::uint32_t(s.net_baseline * 1e6F) >> 0)  & 0xFFu),
                        std::byte((std::uint32_t(s.net_baseline * 1e6F) >> 8)  & 0xFFu),
                        std::byte((std::uint32_t(s.net_baseline * 1e6F) >> 16) & 0xFFu),
                        std::byte((std::uint32_t(s.net_baseline * 1e6F) >> 24) & 0xFFu) };
                    const auto delta = cd::net::write_delta(
                        std::span<const std::byte>(bb),
                        std::span<const std::byte>(cb));
                    s.net_raw_bytes += 4; s.net_wire_bytes += delta.size();
                    s.net_baseline = v; ++s.net_sent;
                    if (s.net_rng.next_float() < 0.10F)
                    { ++s.net_drop; }
                    else
                    {
                        const double lat = 0.03 + 0.06 * static_cast<double>(
                            s.net_rng.next_float());
                        s.net_rtt.record(static_cast<std::uint32_t>(lat * 2.0 * 1e6));
                        s.net_snapbuf.push(s.net_t + lat, v);
                        ++s.net_recv;
                    }
                }
            }
            (void)s.net_snapbuf.sample(s.net_t - 0.10);
            s.net_snapbuf.drop_older_than(s.net_t - 0.5);
            s.counters.increment("net_ticks");
        }

        // Random viz refresh (only when 3D RNG overlay is visible or on initial seed frame)
        if ((s.frame_idx == 0 || s.fx.rng_show_hist_3d) && s.frame_idx >= s.next_random_refresh)
        {
            std::vector<float> u; u.reserve(8192);
            for (int i = 0; i < 8192; ++i) u.push_back(s.rand_rng.next_float());
            s.hist_uniform.rebuild(u, 0.0F, 1.0F, 24);
            std::vector<float> n; n.reserve(8192);
            bool cv_ok = false; float cv = 0.0F;
            for (int i = 0; i < 8192; ++i)
            {
                if (cv_ok) { cv_ok = false; n.push_back(cv); continue; }
                float u1 = s.rand_rng.next_float(); u1 = std::max(u1, 1e-7F);
                float u2 = s.rand_rng.next_float();
                float rr = std::sqrt(-2.0F * std::log(u1));
                float t  = 2.0F * std::numbers::pi_v<float> * u2;
                cv = rr * std::sin(t); cv_ok = true;
                n.push_back(rr * std::cos(t));
            }
            s.hist_normal.rebuild(n, -3.0F, 3.0F, 24);
            s.next_random_refresh = s.frame_idx + 120;
        }

        if (s.frame_idx == 0u)
        {
            const bool had_cli_template     = (g_studio_initial_template >= 0);
            const bool had_cli_scene        = !g_studio_initial_scene.empty();
            const bool had_cli_context_demo = g_studio_load_context_demo;
            if (g_studio_autopilot_e2e)
            {
                cd::studio::init_clean_studio_workspace(s, log_push_fn);
                cd::studio::build_and_save_custom_game_level(s, log_push_fn);
                if (g_studio_capture_gameplay_dir.empty())
                {
                    cd::studio::enter_pie_play_mode(s, log_push_fn);
                    cd::studio::studio_world().autopilot_play_active = true;
                }
            }
            else if (had_cli_template)
            {
                cd::studio::init_clean_studio_workspace(s, log_push_fn);
                cd::studio::load_studio_interactive_template(s, g_studio_initial_template, log_push_fn);
                g_studio_initial_template = -1;
            }
            if (had_cli_scene)
            {
                cd::studio::init_clean_studio_workspace(s, log_push_fn);
                (void)cd::studio::load_full_studio_scene(s, g_studio_initial_scene.c_str(), log_push_fn);
                g_studio_initial_scene.clear();
            }
            if (g_studio_load_context_demo)
            {
                cd::studio::seed_demo_context_and_scene(s, log_push_fn);
                g_studio_load_context_demo = false;
            }
            else if (!g_studio_autopilot_e2e && !had_cli_template && !had_cli_scene && !cd::studio_rt::golden::enabled())
            {
                cd::studio::init_clean_studio_workspace(s, log_push_fn);
            }
            if (g_studio_start_pie && !s.pie_playing)
            {
                cd::studio::init_clean_studio_workspace(s, log_push_fn);
                cd::studio::enter_pie_play_mode(s, log_push_fn);
            }
            if (g_studio_has_cli_cam_eye)
                s.cam.eye = g_studio_cli_cam_eye;
            if (g_studio_has_cli_cam_target)
                s.cam.target = g_studio_cli_cam_target;
            if (g_studio_has_cli_cam_eye || g_studio_has_cli_cam_target)
                cd::studio::sync_camera_and_invalidate_frame_history(s);
            if (g_studio_force_project_manager ||
                (!g_studio_autopilot_e2e && !had_cli_template && !had_cli_scene &&
                 !had_cli_context_demo && !g_studio_start_pie &&
                 g_studio_capture_png.empty() && !cd::studio_rt::golden::enabled()))
            {
                cd::studio::studio_world().show_project_manager = true;
            }
        }
        if (g_studio_autopilot_e2e && !g_studio_capture_gameplay_dir.empty() && s.frame_idx == 13u)
        {
            cd::studio::enter_pie_play_mode(s, log_push_fn);
            cd::studio::studio_world().autopilot_play_active = true;
        }

        // Tick Object-Oriented Modifiers + 3D RigidBody & Narrowphase Collider Physics +
        // 3rd-Person Spring Camera BEFORE building TLAS and recording the HDR 3D scene pass!
        if (!cd::studio_rt::golden::enabled())
        {
            cd::studio::tick_studio_modifiers_and_physics(s, dt, log_push_fn);
        }

        if (!s.pie_playing)
        {
            cd_studio::update_free_look_camera(s.app_state.free_look, s.cam, s.scene_cam, dt);
        }

        if (s.app_state.pick.pending && s.gizmo_state.was_hovered)
            s.app_state.pick.pending = false;
        if (s.app_state.pick.pending)
        {
            s.app_state.pick.pending = false;
            const auto vw = static_cast<float>(window.width());
            const auto vh = static_cast<float>(window.height());
            if (vw > 0 && vh > 0)
            {
                const bool wasd_active =
                    s.app_state.free_look.key_w || s.app_state.free_look.key_a ||
                    s.app_state.free_look.key_s || s.app_state.free_look.key_d ||
                    s.app_state.free_look.key_q || s.app_state.free_look.key_e;
                std::vector<cd_studio::EntityHit> ehits;
                ehits.reserve(s.entities.size());
                for (const auto& ent : s.entities)
                {
                    const bool skip_pick =
                        !ent.visible || ent.locked ||
                        ent.name.find("Ground_Floor") != std::string::npos;
                    ehits.push_back({ skip_pick ? cd::ecs::Entity {} : ent.name == "Ocean_Surface_Plane" ? cd::ecs::Entity {} : ent.handle, ent.name });
                }
                std::vector<cd_studio::LightHit> lhits;
                lhits.reserve(s.lights.size());
                for (const auto& l : s.lights)
                    lhits.push_back({ l.light.position, l.light.type, l.name });
                cd_studio::PickInputs pin {
                    .ndc_x          = (2.0F * s.app_state.pick.x / vw) - 1.0F,
                    .ndc_y          = 1.0F - (2.0F * s.app_state.pick.y / vh),
                    .aspect         = vw / vh,
                    .cam            = s.cam,
                    .cam_pitch      = s.app_state.free_look.pitch,
                    .cam_yaw        = s.app_state.free_look.yaw,
                    .wasd_active    = wasd_active,
                    .cam_right_drag = s.app_state.free_look.right_drag,
                    .gizmo_hovered  = false,
                };
                const auto pr = cd_studio::pick_entity_or_light(pin, ehits, lhits, s.scene);
                if (pr.kind == cd_studio::PickKind::kLight)
                { s.selected = pr.index; s.selected_kind = SelKind::kLight; log_push_fn(pr.log); }
                else if (pr.kind == cd_studio::PickKind::kEntity)
                { s.selected = pr.index; s.selected_kind = SelKind::kEntity; log_push_fn(pr.log); }
                else if (s.selected >= 0)
                { log_push_fn("[pick] cleared selection"); s.selected = -1; }
            }
        }

        auto frame_r = s.renderer.begin_frame();
        if (!frame_r.has_value())
        {
            if (frame_r.error().code ==
                static_cast<std::uint32_t>(
                    cd::render::render_errors::Code::kSwapchainOutOfDate))
            { s.needs_rebuild = true; continue; }
            break;
        }
        auto& frame = *frame_r;
        auto& cmd   = *frame.command_buffer;

        // Build ImGui/ChromaUI panels, gizmo manipulation, level switching, and
        // Content Browser mesh/texture assignment BEFORE TLAS/Shadow/HDR pass
        // recording so every scene or camera mutation takes effect atomically
        // across all passes in the same frame with zero mid-frame desync!
        ctx.new_frame();
        const bool kHideEditorUiForGolden = cd::studio_rt::golden::enabled();

        if (!kHideEditorUiForGolden &&
            (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_P) ||
             ImGui::IsKeyChordPressed(ImGuiKey_F2) ||
             ImGui::IsKeyChordPressed(ImGuiKey_GraveAccent)))
        {
            s.palette_visible = !s.palette_visible;
            if (s.palette_visible) s.palette_query.clear();
        }

        if (!kHideEditorUiForGolden)
        {
            cd::studio::draw_studio_main_menu_bar(s, dt, log_push_fn);

            if (!s.pie_playing && !cd::studio::studio_world().show_project_manager)
            {
                const ImGuiViewport* mv = ImGui::GetMainViewport();
                ImGui::SetNextWindowPos(mv->WorkPos);
                ImGui::SetNextWindowSize(mv->WorkSize);
                ImGui::SetNextWindowViewport(mv->ID);
                ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0F);
                ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0F);
                ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2 { 0, 0 });
                ImGui::Begin("##cd_dockhost", nullptr,
                    ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar |
                    ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
                    ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus |
                    ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_NoBackground);
                ImGui::PopStyleVar(3);
                const ImGuiID dock_id = ImGui::GetID("cd_engine_dock");
                if (!s.dock_initialised && ImGui::DockBuilderGetNode(dock_id) == nullptr)
                {
                    ImGui::DockBuilderRemoveNode(dock_id);
                    const int flags =
                        static_cast<int>(ImGuiDockNodeFlags_DockSpace) |
                        static_cast<int>(ImGuiDockNodeFlags_PassthruCentralNode);
                    ImGui::DockBuilderAddNode(dock_id, static_cast<ImGuiDockNodeFlags>(flags));
                    ImGui::DockBuilderSetNodeSize(dock_id, mv->WorkSize);
                    ImGuiID m = dock_id;
                    ImGuiID dl = ImGui::DockBuilderSplitNode(m, ImGuiDir_Left,  0.215F, nullptr, &m);
                    ImGuiID dr = ImGui::DockBuilderSplitNode(m, ImGuiDir_Right, 0.24F,  nullptr, &m);
                    ImGuiID db = ImGui::DockBuilderSplitNode(m, ImGuiDir_Down,  0.22F,  nullptr, &m);
                    ImGui::DockBuilderDockWindow("Studio Workbench", dl);
                    ImGui::DockBuilderDockWindow("Inspector",        dr);
                    ImGui::DockBuilderDockWindow("History",          db);
                    ImGui::DockBuilderFinish(dock_id);
                    s.dock_initialised = true;
                }
                if (cd::ui::chroma::state().workspace_mode == 0)
                {
                    ImGui::DockSpace(dock_id, ImVec2 { 0, 0 },
                                     ImGuiDockNodeFlags_PassthruCentralNode);
                }
                ImGui::End();
            }

            const HdrSceneFrame ui_hdr_frame =
                make_hdr_frame(s.frame_idx, frame.extent, s.cam, s.fx.taa_amount);
            cd::studio::draw_clean_studio_workspace_panels(
                s, dt, ui_hdr_frame.vp, frame.extent.width, frame.extent.height, log_push_fn);
            const HdrSceneFrame post_panel_hdr_frame =
                make_hdr_frame(s.frame_idx, frame.extent, s.cam, s.fx.taa_amount);
            if (!s.pie_playing && !cd::studio::studio_world().show_project_manager &&
                cd::ui::chroma::state().workspace_mode == 0)
            {
                draw_selection_outline_overlay(s.entities, s.selected, s.selected_kind,
                                               s.outline, s.scene, post_panel_hdr_frame.vp, frame.extent);
                draw_light_markers_overlay(s.lights, s.selected, s.selected_kind, post_panel_hdr_frame.vp, frame.extent);
                update_and_draw_gizmo(s.gizmo, s.gizmo_state,
                                      s.app_state.pick.pending, s.selected, s.selected_kind,
                                      s.entities, s.lights, s.scene, s.history, log_push_fn,
                                      post_panel_hdr_frame.vp, s.cam, window, frame.extent);
            }
            draw_command_palette_popup(s.palette, s.palette_visible, s.palette_query,
                                       frame.extent);
        }

        // CPU-LBS/DQS skinning for CesiumMan (animated character — kGltf entity).
        bool has_gltf_entity   = false;
        bool has_sponza_entity = false;
        for (const auto& ent : s.entities)
        {
            if (ent.kind == PrimitiveKind::kGltf)   has_gltf_entity   = true;
            if (ent.kind == PrimitiveKind::kSponza) has_sponza_entity = true;
        }
        if ((has_sponza_entity && !s.meshes.gltf.vb.is_valid()) ||
            (has_gltf_entity   && !s.meshes.gltf_cesium.vb.is_valid()))
        {
            auto upload_alb_lazy = [&device_ref = device](
                const std::uint8_t* rgba, std::uint32_t w, std::uint32_t h)
                -> std::pair<cd::rhi::TextureHandle, cd::rhi::TextureViewHandle>
            {
                auto t = create_texture_rgba8(device_ref, rgba, w, h);
                return { t.image, t.view };
            };
            cd_studio::AlbedoSlot lazy_alb_slot {
                .image_io = &s.albedo_tex.image,
                .view_io  = &s.albedo_tex.view,
                .sampler  = s.albedo_sampler,
                .upload   = upload_alb_lazy,
            };
            bool loaded_any = false;
            if (has_sponza_entity && !s.meshes.gltf.vb.is_valid())
            {
                loaded_any |= cd_studio::ensure_sponza_loaded_in_registry(
                    s.meshes, device, lazy_alb_slot, s.prim_inst, s.materials.prim,
                    s.normal_tex.view, s.mr_tex.view);
            }
            if (has_gltf_entity && !s.meshes.gltf_cesium.vb.is_valid())
            {
                loaded_any |= cd_studio::ensure_cesium_loaded_in_registry(
                    s.meshes, device, lazy_alb_slot, s.prim_inst, s.materials.prim,
                    s.normal_tex.view, s.mr_tex.view);
            }
            if (loaded_any)
            {
                if (s.meshes.has_gltf_texture || s.meshes.has_cesium_texture)
                    s.has_gltf_texture = true;
                cd_studio::wire_w8be_bindless_and_meta(
                    s, device, cd_studio::kBootLightUboBytes, cd::studio_rt::kInstMatBytes);
                s.last_tlas_scene_hash = 0;
            }
        }
        const float pie_speed_scale = s.pie_playing
            ? std::clamp(cd::studio::studio_world().player_horiz_speed / 3.2F, 0.14F, 1.55F)
            : 1.0F;
        const float anim_dt = s.anim_paused
            ? 0.0F
            : (std::max(dt, 1.0F / 120.0F) * s.anim_speed * pie_speed_scale);
        if (has_gltf_entity &&
            !cd_studio::update_skinned_animation(s.meshes.cesium_skinned, device,
                                                  s.meshes.gltf_cesium.vb, anim_dt))
        {
            static float cesium_yaw_t = 0.0F;
            cesium_yaw_t += dt * 0.5F;
            for (auto& ent : s.entities)
            {
                if (ent.kind != PrimitiveKind::kGltf) continue;
                auto* lt = s.scene.local(ent.handle);
                if (!lt) continue;
                const float half = cesium_yaw_t * 0.5F;
                const float sy = std::sin(half);
                const float cy = std::cos(half);
                const float qx1 = 0.0F;
                const float qy1 = sy;
                const float qz1 = 0.0F;
                const float qw1 = cy;
                const float qx2 = -0.7071068F;
                const float qy2 = 0.0F;
                const float qz2 = 0.0F;
                const float qw2 = 0.7071068F;
                lt->value.rotation = {
                    qw1*qx2+qx1*qw2+qy1*qz2-qz1*qy2,
                    qw1*qy2-qx1*qz2+qy1*qw2+qz1*qx2,
                    qw1*qz2+qx1*qy2-qy1*qx2+qz1*qw2,
                    qw1*qw2-qx1*qx2-qy1*qy2-qz1*qz2 };
                break;
            }
        }

        // Compute lightweight FNV-1a scene hash over entity transforms/materials
        // so static frames skip TLAS recreation, 512 KB SSBO heap allocation,
        // and 100+ vkUpdateDescriptorSets calls.
        std::uint64_t tlas_scene_hash = 14695981039346656037ULL;
        auto hash_mix = [&](std::uint64_t v) noexcept
        {
            tlas_scene_hash ^= v;
            tlas_scene_hash *= 1099511628211ULL;
        };
        auto hash_f32 = [&](float f) noexcept
        {
            hash_mix(static_cast<std::uint64_t>(std::bit_cast<std::uint32_t>(f)));
        };
        const float effective_floor_y =
            (cd::studio_rt::golden::enabled() || !s.show_editor_floor) ? kFloorY : -0.002F;
        const SunLight sun = resolve_sun_light(s.lights);
        hash_mix(s.entities.size());
        hash_mix(s.show_editor_floor ? 1u : 0u);
        hash_f32(effective_floor_y);
        hash_mix(sun.has_sun ? 1u : 0u);
        hash_f32(sun.dir.x); hash_f32(sun.dir.y); hash_f32(sun.dir.z);
        if (has_gltf_entity && !s.anim_paused)
        {
            hash_mix(static_cast<std::uint64_t>(s.frame_idx));
        }
        for (const auto& ent : s.entities)
        {
            hash_mix(static_cast<std::uint64_t>(ent.handle.id));
            hash_mix(static_cast<std::uint64_t>(ent.kind));
            hash_mix(ent.visible ? 1u : 0u);
            hash_mix(ent.is_pbr ? 1u : 0u);
            hash_mix(ent.use_texture ? 1u : 0u);
            hash_mix(static_cast<std::uint64_t>(ent.custom_texture_id));
            hash_f32(ent.tint.x); hash_f32(ent.tint.y); hash_f32(ent.tint.z);
            hash_f32(ent.metallic); hash_f32(ent.roughness);
            hash_f32(ent.multiscatter_comp); hash_f32(ent.iridescence_nm);
            if (const auto* lt = s.scene.local(ent.handle))
            {
                hash_f32(lt->value.position.x); hash_f32(lt->value.position.y); hash_f32(lt->value.position.z);
                hash_f32(lt->value.rotation.x); hash_f32(lt->value.rotation.y); hash_f32(lt->value.rotation.z); hash_f32(lt->value.rotation.w);
                hash_f32(lt->value.scale.x);    hash_f32(lt->value.scale.y);    hash_f32(lt->value.scale.z);
            }
        }
        const bool tlas_dirty =
            !s.current_tlas.is_valid() || (tlas_scene_hash != s.last_tlas_scene_hash);
        if (tlas_dirty)
        {
            s.last_tlas_scene_hash = tlas_scene_hash;
        }

        cd_studio::rebuild_tlas_and_transition_depth(
            device, cmd, s.frame_idx, s.tlas_destroy_queue, s.current_tlas,
            s.prim_inst, std::span<const SceneEntity>(s.entities),
            blas_for_kind,
            [](const SceneEntity& e) -> cd::math::Vec3f {
                if (e.kind == PrimitiveKind::kSponza)
                    return { 0.72F, 0.60F, 0.48F };
                return e.tint;
            },
            [](const SceneEntity& e) -> PrimitiveKind    { return e.kind; },
            [&s](const SceneEntity& e) -> std::optional<cd::math::Mat4f>
            {
                if (!e.visible || e.name.find("Ground_Floor") != std::string::npos)
                    return std::nullopt;
                auto* lt = s.scene.local(e.handle);
                if (!lt) return std::nullopt;
                return cd::math::to_mat4(lt->value);
            },
            [&s](const SceneEntity& e) -> std::span<const cd::math::Vec3f>
            {
                if (e.kind == PrimitiveKind::kSponza)
                    return { s.sponza_geom_albedos };
                if (e.kind == PrimitiveKind::kGltf)
                    return { s.cesium_geom_albedos };
                return {};
            },
            [&s](const SceneEntity& e) -> std::span<const cd_studio::W8BEGeomMeta>
            {
                if (e.kind == PrimitiveKind::kSponza)
                    return { s.sponza_w8be_meta };
                if (e.kind == PrimitiveKind::kGltf)
                    return { s.cesium_w8be_meta };
                if (e.kind == PrimitiveKind::kSphere && e.is_pbr && e.use_texture)
                    return { s.pbr_sphere_textured_meta };
                return {};
            },
            (cd::studio_rt::golden::enabled() && s.show_editor_floor) ? s.meshes.blas_floor : cd::rhi::AccelStructureHandle {}, s.meshes.blas_cesium,
            has_gltf_entity && s.meshes.cesium_skinned.valid, s.inst_mat_ssbo,
            s.rts.depth.image, s.depth_initialised_on_gpu,
            [](const SceneEntity& e) -> bool {
                return e.kind == PrimitiveKind::kSphere;
            },
            tlas_dirty,
            effective_floor_y);

        // phase-descriptor-sync: propagate per-frame TLAS + inst_mat SSBO
        // writes to active per-prim descriptor sets (binding 2 + 10) when dirty.
        if (tlas_dirty && s.current_tlas.is_valid())
        {
            if (has_sponza_entity)
            {
                cd_studio::sync_perprim_tlas_and_ssbo(
                    s.meshes.gltf_prim_ranges,
                    s.current_tlas, s.inst_mat_ssbo,
                    cd::studio_rt::kInstMatBytes);
            }
            if (has_gltf_entity)
            {
                cd_studio::sync_perprim_tlas_and_ssbo(
                    s.meshes.gltf_cesium_prim_ranges,
                    s.current_tlas, s.inst_mat_ssbo,
                    cd::studio_rt::kInstMatBytes);
            }
        }

        const bool ddgi_wanted = (s.fx.ddgi_on || cd_studio::g_ddgi_force_on)
                                 && s.current_tlas.is_valid()
                                 && device.features().ray_query;
        if (ddgi_wanted)
        {
            if (!s.ddgi.inited)
                (void)s.ddgi.boot(device);
            if (s.ddgi.inited &&
                (!s.ddgi.bound ||
                 s.ddgi.vp_w != frame.extent.width ||
                 s.ddgi.vp_h != frame.extent.height))
            {
                (void)s.ddgi.bind(device, s.rts.hdr.view, s.rts.depth.view,
                                  s.rts.gbuf_normal.view,
                                  frame.extent.width, frame.extent.height);
            }
        }

        draw_shadow_map_pass(cmd, sun, device, s.shadow_ubo, s.shadow_target,
                             s.shadow_initialised_on_gpu, s.materials.shadow,
                             kShadowMapSize, s.entities, s.scene, mesh_for,
                             tlas_dirty);

        constexpr std::uint32_t kHdrLaneCount = 3;
        std::unique_ptr<cd::rhi::IParallelPassRecorder> hdr_rec;
        HdrSceneFrame hdr_frame {};
        if (s.fx.hdr_parallel_lanes)
        {
            auto opened = begin_hdr_scene_pass_lanes(
                cmd, s.frame_idx, s.rts.hdr, s.rts.gbuf_normal, s.rts.gbuf_albedo,
                s.rts.gbuf_mr, s.rts.depth, frame.extent, s.cam, s.fx.taa_amount,
                kHdrLaneCount);
            hdr_frame = opened.first;
            hdr_rec   = std::move(opened.second);
            static bool lanes_logged = false;
            if (!lanes_logged)
            {
                lanes_logged = true;
                std::puts(hdr_rec
                              ? "[lanes] HDR pass: parallel lanes ENGAGED (3 lanes)"
                              : "[lanes] HDR pass: backend fallback to SERIAL");
            }
        }
        else
        {
            hdr_frame = begin_hdr_scene_pass(
                cmd, s.frame_idx, s.rts.hdr, s.rts.gbuf_normal, s.rts.gbuf_albedo,
                s.rts.gbuf_mr, s.rts.depth, frame.extent, s.cam, s.fx.taa_amount);
        }
        const float aspect           = hdr_frame.aspect;
        const cd::math::Mat4f vp_unj = hdr_frame.vp_unjittered;
        const cd::math::Mat4f vp     = hdr_frame.vp;

        auto& hdr_lane0 = hdr_rec ? hdr_rec->lane(0)
                                  : static_cast<cd::rhi::IDrawRecorder&>(cmd);
        auto& hdr_lane1 = hdr_rec ? hdr_rec->lane(1)
                                  : static_cast<cd::rhi::IDrawRecorder&>(cmd);
        auto& hdr_lane2 = hdr_rec ? hdr_rec->lane(2)
                                  : static_cast<cd::rhi::IDrawRecorder&>(cmd);
        if (hdr_rec)
            prep_hdr_lane(hdr_lane0, frame.extent);

        draw_sky_pass(hdr_lane0, s.cam, aspect, sun, s.materials.sky);
        upload_multi_light_ubo(device, s.lights_ubo, s.lights, s.counters);
        s.materials.prim.apply(hdr_lane0);
        s.prim_inst.bind(hdr_lane0, 0);
        if (s.prim_bindless_set.is_valid())
            hdr_lane0.bind_descriptor_set(1, s.prim_bindless_set);

        draw_floor_and_entities(hdr_lane0, s.meshes.floor, effective_floor_y, vp, s.fx, sun, s.cam,
                                s.entities, s.scene, s.has_gltf_texture,
                                s.materials.prim, s.counters, mesh_for,
                                [&s](PrimitiveKind k) -> std::span<const cd_studio::GltfPrimRange> {
                                    if (k == PrimitiveKind::kSponza) return s.meshes.gltf_prim_ranges;
                                    if (k == PrimitiveKind::kGltf) return s.meshes.gltf_cesium_prim_ranges;
                                    return {};
                                }, s.prim_inst,
                                s.albedo_sampler, device, s.albedo_tex.view,
                                s.normal_tex.view, s.mr_tex.view,  // phase456: per-prim restore
                                s.show_editor_floor);  // phase435-vis8
        if (cd::studio_rt::golden::enabled() && s.show_editor_floor && s.entities.size() > 1U)
        {
            draw_planar_shadows(hdr_lane0, sun, effective_floor_y, kShadowLift, s.entities,
                                s.scene, vp, s.materials.prim, s.counters, mesh_for);
        }
        // phase1121: lane1 owns the 3D overlays — re-establish dynamic
        // state + the prim pipeline/sets the overlays assume (a lane's
        // secondary inherits nothing from lane0).
        if (hdr_rec)
        {
            prep_hdr_lane(hdr_lane1, frame.extent);
            s.materials.prim.apply(hdr_lane1);
            s.prim_inst.bind(hdr_lane1, 0);
            if (s.prim_bindless_set.is_valid())
                hdr_lane1.bind_descriptor_set(1, s.prim_bindless_set);
        }
        cd_studio::draw_ddgi_probe_overlay_3d(s, hdr_lane1, vp, sun, fill_prim_push_shared);
        cd_studio::draw_frustum_cull_overlay_3d(s, hdr_lane1, vp, sun, fill_prim_push_shared, aspect);
        cd_studio::draw_light_gizmo_overlay_3d(s, hdr_lane1, vp, sun, fill_prim_push_shared);
        cd_studio::draw_decal_obb_overlay_3d(s, hdr_lane1, vp, sun, fill_prim_push_shared);
        cd_studio::draw_csm_cascade_overlay_3d(s, hdr_lane1, vp, sun, fill_prim_push_shared);
        cd_studio::draw_cluster_heatmap_overlay_3d(s, hdr_lane1, vp, sun, fill_prim_push_shared);
        cd_studio::draw_bezier_curve_overlay_3d(s, hdr_lane1, vp, sun, fill_prim_push_shared);
        cd_studio::draw_gpu_particles_overlay_3d(s, hdr_lane1, vp, sun, fill_prim_push_shared);
        cd_studio::draw_quat_slerp_overlay_3d(s, hdr_lane1, vp, sun, fill_prim_push_shared);
        cd_studio::draw_motion_vector_overlay_3d(s, hdr_lane1, vp, sun, fill_prim_push_shared);
        cd_studio::draw_cubemap_globe_overlay_3d(s, hdr_lane1, vp, sun, fill_prim_push_shared);
        cd_studio::draw_vg_lod_overlay_3d(s, hdr_lane1, vp, sun, fill_prim_push_shared);
        cd_studio::draw_noise_heightfield_overlay_3d(s, hdr_lane1, vp, sun, fill_prim_push_shared);
        cd_studio::draw_restir_reservoir_overlay_3d(s, hdr_lane1, vp, sun, fill_prim_push_shared);
        cd_studio::draw_earth_globe_overlay_3d(s, hdr_lane1, vp, sun, fill_prim_push_shared);
        cd_studio::draw_camera_basis_overlay_3d(s, hdr_lane1, vp, sun, fill_prim_push_shared);
        cd_studio::draw_cct_sweep_overlay_3d(s, hdr_lane1, vp, sun, fill_prim_push_shared);
        cd_studio::draw_attenuation_rail_overlay_3d(s, hdr_lane1, vp, sun, fill_prim_push_shared);
        cd_studio::append_pure_line_overlays(s.fx, s.debug_lines);
        {
            cd::virtual_textures::PageId q {};
            q.x   = static_cast<std::uint16_t>(s.fx.vt_req[0]);
            q.y   = static_cast<std::uint16_t>(s.fx.vt_req[1]);
            q.mip = static_cast<std::uint8_t>(s.fx.vt_req[2]);
            if (s.fx.vt_req_serial != s.vt_applied_serial)
            {
                (void)s.vt_table.allocate(q);
                s.vt_applied_serial = s.fx.vt_req_serial;
            }
            s.fx.vt_resident_count =
                static_cast<std::uint32_t>(s.vt_table.resident_count());
            const auto* slot = s.vt_table.lookup(q);
            s.fx.vt_lookup_found  = (slot != nullptr);
            s.fx.vt_lookup_slot_x = (slot != nullptr) ? slot->slot_x : 0U;
            s.fx.vt_lookup_slot_y = (slot != nullptr) ? slot->slot_y : 0U;
            if (s.fx.vt_show_atlas_3d)
                cd_studio::append_vt_atlas_overlay(
                    s.fx, s.vt_table, s.debug_lines);
        }
        if (s.fx.ms_show_meshlets_3d)
            cd_studio::append_meshlet_overlay(s.fx, s.debug_lines);
        if (!s.debug_lines.empty())
            s.counters.increment("draws_debug_lines");
        if (hdr_rec)
            prep_hdr_lane(hdr_lane2, frame.extent);
        s.debug_draw.flush(device, hdr_lane2, s.debug_lines, vp, s.frame_idx);
        s.debug_lines.clear();

        // phase1121: lanes join IN LANE ORDER (== serial order) and the
        // pass closes inside finish(); the serial path closes as before.
        if (hdr_rec)
        {
            hdr_rec->finish();
            hdr_rec.reset();
        }
        else
        {
            cmd.end_render_pass();
        }
        {
            std::array<cd::rhi::TextureBarrier, 4> hb {
                cd::rhi::TextureBarrier { .texture = s.rts.hdr.image,
                    .from = cd::rhi::ResourceState::kColorAttachment,
                    .to   = cd::rhi::ResourceState::kShaderResource,
                    .range = { 0, 1, 0, 1 } },
                cd::rhi::TextureBarrier { .texture = s.rts.gbuf_normal.image,
                    .from = cd::rhi::ResourceState::kColorAttachment,
                    .to   = cd::rhi::ResourceState::kShaderResource,
                    .range = { 0, 1, 0, 1 } },
                cd::rhi::TextureBarrier { .texture = s.rts.gbuf_albedo.image,
                    .from = cd::rhi::ResourceState::kColorAttachment,
                    .to   = cd::rhi::ResourceState::kShaderResource,
                    .range = { 0, 1, 0, 1 } },
                cd::rhi::TextureBarrier { .texture = s.rts.gbuf_mr.image,
                    .from = cd::rhi::ResourceState::kColorAttachment,
                    .to   = cd::rhi::ResourceState::kShaderResource,
                    .range = { 0, 1, 0, 1 } }
            };
            cmd.barrier({}, hb);
        }

        // phase1146 (P4): DDGI indirect bounce. Runs AFTER the HDR scene pass
        // closed (hdr + gbuf_normal now kShaderResource, depth kDepthWrite)
        // and BEFORE the composite reads HDR, so the diffuse GI ADDS into the
        // HDR buffer. Gated on (fx.ddgi_on || force) && a valid TLAS — the
        // golden fixture path leaves ddgi_on false so the chrome golden is
        // untouched. Depth is bounced kDepthWrite -> kShaderResource for the
        // dispatch (world-pos reconstruct) then restored to kDepthWrite so
        // the velocity pass's kDepthWrite -> kDepthRead transition stays valid.
        if (ddgi_wanted && s.ddgi.inited && s.ddgi.bound)
        {
            {
                std::array<cd::rhi::TextureBarrier, 1> dsr {
                    cd::rhi::TextureBarrier { .texture = s.rts.depth.image,
                        .from = cd::rhi::ResourceState::kDepthWrite,
                        .to   = cd::rhi::ResourceState::kShaderResource,
                        .range = { 0, 1, 0, 1 } }
                };
                cmd.barrier({}, dsr);
            }

            // SunLight::dir is the direction light TRAVELS; the DDGI trace
            // shader wants the direction TOWARD the sun (for N·L), so negate.
            // Scale colour by the sun strength so the indirect bounce tracks
            // the scene's direct-light intensity.
            const cd::math::Vec3f ddgi_sun_dir { -sun.dir.x, -sun.dir.y, -sun.dir.z };
            const float ddgi_sun_k = (sun.strength > 0.0F) ? sun.strength : 1.0F;
            const cd::math::Vec3f ddgi_sun_col {
                sun.col.x * ddgi_sun_k, sun.col.y * ddgi_sun_k, sun.col.z * ddgi_sun_k };
            s.ddgi.execute_frame(
                device, cmd, s.rts.hdr.image, s.rts.gbuf_normal.image,
                s.current_tlas, vp, ddgi_sun_dir, ddgi_sun_col, s.frame_idx);

            {
                std::array<cd::rhi::TextureBarrier, 1> dre {
                    cd::rhi::TextureBarrier { .texture = s.rts.depth.image,
                        .from = cd::rhi::ResourceState::kShaderResource,
                        .to   = cd::rhi::ResourceState::kDepthWrite,
                        .range = { 0, 1, 0, 1 } }
                };
                cmd.barrier({}, dre);
            }
        }

        if (!s.velocity_initialised_on_gpu || s.fx.taa_amount > 0.001F || s.fx.motion_blur > 0.001F)
        {
            velocity_gbuffer_pass(cmd, s.frame_idx, s.rts.gbuf_velocity, s.rts.depth,
                                  frame.extent, s.materials.velocity,
                                  s.entities, s.scene, s.prev_vp_unjittered, vp_unj, mesh_for,
                                  !s.velocity_initialised_on_gpu);
            s.velocity_initialised_on_gpu = true;
        }
        else
        {
            std::array<cd::rhi::TextureBarrier, 1> db_to_sr {
                cd::rhi::TextureBarrier { .texture = s.rts.depth.image,
                                         .from = cd::rhi::ResourceState::kDepthWrite,
                                         .to = cd::rhi::ResourceState::kShaderResource,
                                         .range = { 0, 1, 0, 1 } }
            };
            cmd.barrier({}, db_to_sr);
        }

        const float scene_ev_stops = s.auto_exposure.current_ev();

        if (!s.bloom_initialised_on_gpu || s.fx.bloom_post > 0.001F)
        {
            run_bloom_chain(cmd, s.frame_idx, s.bloom_chain,
                            s.materials.bloom_prefilter, s.bloom_prefilter_inst,
                            s.materials.bloom_downsample, s.bloom_down_insts,
                            s.materials.bloom_upsample, s.bloom_up_insts,
                            scene_ev_stops, !s.bloom_initialised_on_gpu);
            s.bloom_initialised_on_gpu = true;
        }

        begin_composite_pass(cmd, s.frame_idx, frame.swapchain_image_view,
                             frame.extent, s.rts.history, s.history_states,
                             s.materials.composite, s.composite_insts,
                             s.fx, s.lights, s.cam, frame_loop_start,
                             s.prev_cam_basis, s.prev_vp_unjittered, s.prev_vp_valid,
                             vp_unj);

        // phase1174 — behavior_designer fold (ADR-20260614, Job A overlay
        // record + Job B panel bind). The cd::ui Submitter records the
        // BehaviorDesigner node-graph into the OPEN composite/swapchain pass,
        // BEFORE ctx.render(cmd) so ImGui stays on top (ADR §3.3 draw order:
        // scene -> composite -> submitter -> ImGui). It co-exists with ImGui
        // (same BGRA8 pass; Submitter pipeline is depth-OFF + alpha-blend-ON).
        //
        // ZORUNLU golden invariant (ADR §5.1): the upload/record pair is
        // gated by (!kHideEditorUiForGolden && (panel toggles)). On a
        // --golden-fixture capture golden::enabled() is true, so this block
        // NEVER runs -> the composite output is byte-identical to baseline.
        //
        // phase1177 — studio_ui fold (ADR-20260614 §3.4, Job C). TWO panels
        // share this ONE Submitter overlay: the BehaviorDesigner node-graph
        // (Job A/B) and the folded studio_ui Button/Slider/Label/Flex demo.
        // Both draw into the SAME ui_batcher within a single begin_frame ->
        // upload -> record cycle; each panel is independently toggled but the
        // outer gate keeps EITHER from emitting on a golden frame.
        if (!kHideEditorUiForGolden &&
            (s.show_behavior_designer || s.show_ui_widgets) &&
            s.ui_submitter && s.ui_submitter->is_valid())
        {
            const cd::ui::widgets::Rect panel_rect {
                0.0F,
                0.0F,
                static_cast<float>(frame.extent.width),
                static_cast<float>(frame.extent.height)
            };
            s.ui_batcher.begin_frame();

            if (s.show_behavior_designer)
            {
                s.bd_panel.draw(s.ui_batcher, s.ui_widget_theme, panel_rect);
            }

            // phase1177 — UI Widgets Demo. Solve the column-flex tree for a
            // small panel anchored bottom-left, push the leaf rects onto the
            // widgets, tick them on the (empty) input snapshot, and draw the
            // header label / button / slider into the shared batcher.
            if (s.show_ui_widgets && s.uiw_tree_built)
            {
                constexpr float kPanelW = 320.0F;
                constexpr float kPanelH = 160.0F;
                const float panel_x = 24.0F;
                const float panel_y =
                    static_cast<float>(frame.extent.height) - kPanelH - 24.0F;

                s.uiw_tree.solve(s.uiw_root, kPanelW, kPanelH);

                // Translate the solver's panel-local rects into the bottom-
                // left anchor and feed the widgets.
                auto shift = [&](cd::ui::layout::NodeId n) {
                    cd::ui::layout::Rect r = s.uiw_tree.layout(n);
                    r.x += panel_x;
                    r.y += panel_y;
                    return r;
                };
                const cd::ui::layout::Rect lr = shift(s.uiw_label);
                s.uiw_btn.set_rect(uiw_to_widget_rect(shift(s.uiw_button)));
                s.uiw_sld.set_rect(uiw_to_widget_rect(shift(s.uiw_slider)));

                // phase1178 — flatten current frame's pointer state into
                // InputState so Button/Slider tick() can respond to hover
                // + click. Coordinates are absolute screen-space pixels;
                // widget rects were already shifted to screen space above.
                // This block is already inside !kHideEditorUiForGolden
                // (outer gate) so golden captures are byte-identical.
                cd::ui::widgets::InputState ui_input;
                ui_input.pointer.mouse_x      = s.ui_mouse_x;
                ui_input.pointer.mouse_y      = s.ui_mouse_y;
                ui_input.pointer.left_down    = s.ui_lmb_down;
                ui_input.pointer.left_pressed = s.ui_lmb_down && !s.ui_lmb_prev_down;
                ui_input.pointer.left_released= !s.ui_lmb_down && s.ui_lmb_prev_down;
                ui_input.focused              = false;
                (void)s.uiw_btn.tick(ui_input);
                (void)s.uiw_sld.tick(ui_input);

                // Panel background + header label surface quad.
                s.ui_batcher.quad(panel_x, panel_y, kPanelW, kPanelH,
                                  cd::ui::renderer::Color {
                                      s.ui_widget_theme.background.r,
                                      s.ui_widget_theme.background.g,
                                      s.ui_widget_theme.background.b,
                                      s.ui_widget_theme.background.a });
                s.ui_batcher.quad(lr.x, lr.y, lr.width, lr.height,
                                  cd::ui::renderer::Color {
                                      s.ui_widget_theme.surface.r,
                                      s.ui_widget_theme.surface.g,
                                      s.ui_widget_theme.surface.b,
                                      s.ui_widget_theme.surface.a });
                cd::ui::font::Font* fp =
                    s.ui_font.is_loaded() ? &s.ui_font : nullptr;
                s.uiw_btn.draw(s.ui_batcher, fp, s.ui_widget_theme);
                s.uiw_sld.draw(s.ui_batcher, fp, s.ui_widget_theme);
            }

            if (s.ui_submitter->upload(s.ui_batcher))
            {
                s.ui_submitter->record(cmd, frame.extent);
            }
        }

        ctx.render(cmd);
        cmd.end_render_pass();

        // ----------------------------------------------------------------
        // T1.7 phase543 -- Sponza golden-image readback.
        //
        // When --golden-fixture was provided AND we have reached the
        // configured "golden frame", schedule a swapchain -> host-buffer
        // copy here (after end_render_pass, before end_frame). The post-
        // end_frame block below downloads + writes the PNG + breaks.
        // ----------------------------------------------------------------
        const bool golden_active = cd::studio_rt::golden::enabled()
            && !cd::studio_rt::golden::options().out_png_path.empty();
        const bool studio_capture_active = !g_studio_capture_png.empty();
        std::string gameplay_seq_path {};
        bool        gameplay_seq_final = false;
        if (!g_studio_capture_gameplay_dir.empty())
        {
            std::error_code ec {};
            std::filesystem::create_directories(g_studio_capture_gameplay_dir, ec);
            if (s.frame_idx == 12u)
                gameplay_seq_path = g_studio_capture_gameplay_dir + "/01_editor_built_and_loaded_scene.png";
            else if (s.frame_idx == 32u)
                gameplay_seq_path = g_studio_capture_gameplay_dir + "/02_pie_combat_and_jumppad.png";
            else if (s.frame_idx == 58u)
                gameplay_seq_path = g_studio_capture_gameplay_dir + "/03_pie_moving_platforms_and_cores.png";
            else if (s.frame_idx == 95u)
            {
                gameplay_seq_path  = g_studio_capture_gameplay_dir + "/04_pie_victory_goal_complete.png";
                gameplay_seq_final = true;
            }
        }
        const bool capture_this_frame =
            (golden_active && s.frame_idx == cd::studio_rt::golden::options().capture_at_frame)
            || (studio_capture_active && s.frame_idx >= g_studio_capture_frame)
            || !gameplay_seq_path.empty();
        cd::rhi::BufferHandle golden_staging {};
        std::uint32_t golden_w = 0;
        std::uint32_t golden_h = 0;
        if (capture_this_frame)
        {
            cd::rhi::BufferDesc bd {};
            bd.size   = static_cast<std::uint64_t>(frame.extent.width)
                      * frame.extent.height * 4U;
            bd.usage  = cd::rhi::BufferUsage::kTransferDst;
            bd.memory = cd::rhi::MemoryUsage::kGpuToCpu;
            auto buf_r = device.create_buffer(bd);
            if (buf_r.has_value())
            {
                const std::array<cd::rhi::TextureBarrier, 1> to_src {
                    cd::rhi::TextureBarrier {
                        .texture = frame.swapchain_image,
                        .from = cd::rhi::ResourceState::kColorAttachment,
                        .to   = cd::rhi::ResourceState::kTransferSrc,
                        .range = { .base_mip = 0, .mip_count = 1,
                                   .base_layer = 0, .layer_count = 1 },
                    }
                };
                cmd.barrier({}, to_src);
                const std::array<cd::rhi::BufferImageCopyRegion, 1> regions {
                    cd::rhi::BufferImageCopyRegion {
                        .buffer_offset = 0,
                        .mip_level = 0,
                        .base_layer = 0,
                        .layer_count = 1,
                        .image_offset = { 0, 0, 0 },
                        .image_extent = { frame.extent.width,
                                          frame.extent.height, 1 },
                    }
                };
                cmd.copy_image_to_buffer(frame.swapchain_image, *buf_r, regions);
                const std::array<cd::rhi::TextureBarrier, 1> back {
                    cd::rhi::TextureBarrier {
                        .texture = frame.swapchain_image,
                        .from = cd::rhi::ResourceState::kTransferSrc,
                        .to   = cd::rhi::ResourceState::kColorAttachment,
                        .range = { .base_mip = 0, .mip_count = 1,
                                   .base_layer = 0, .layer_count = 1 },
                    }
                };
                cmd.barrier({}, back);
                golden_staging = *buf_r;
                golden_w = frame.extent.width;
                golden_h = frame.extent.height;
            }
            else
            {
                std::fprintf(stderr,
                    "[golden] staging buffer create failed; capture aborted\n");
            }
        }

        auto end_r = s.renderer.end_frame();
        if (!end_r.has_value())
        {
            if (end_r.error().code ==
                static_cast<std::uint32_t>(
                    cd::render::render_errors::Code::kSwapchainOutOfDate))
            { s.needs_rebuild = true; continue; }
            break;
        }

        // After end_frame: GPU has finished writing the staging buffer.
        // Download bytes, BGRA->RGBA swap, write PNG, request shutdown (if single capture or final stage).
        if (capture_this_frame && golden_staging.is_valid())
        {
            s.renderer.wait_idle();  // belt-and-braces; end_frame already waited
            const std::uint64_t bytes =
                static_cast<std::uint64_t>(golden_w) * golden_h * 4U;
            std::vector<std::byte> raw(static_cast<std::size_t>(bytes));
            auto dl = device.download_buffer(
                golden_staging, 0, std::span<std::byte> { raw });
            device.destroy_buffer(golden_staging);
            if (!dl.has_value())
            {
                std::fprintf(stderr,
                    "[golden] download_buffer failed (code=%u)\n",
                    static_cast<unsigned>(dl.error().code));
                request_shutdown();
                break;
            }
            // Swapchain format is BGRA8Unorm; PNG expects RGBA.
            std::vector<std::uint8_t> rgba(static_cast<std::size_t>(bytes));
            for (std::size_t i = 0; i < rgba.size(); i += 4)
            {
                rgba[i + 0] = static_cast<std::uint8_t>(raw[i + 2]);
                rgba[i + 1] = static_cast<std::uint8_t>(raw[i + 1]);
                rgba[i + 2] = static_cast<std::uint8_t>(raw[i + 0]);
                rgba[i + 3] = static_cast<std::uint8_t>(raw[i + 3]);
            }
            const auto& out_path = !gameplay_seq_path.empty()
                ? gameplay_seq_path
                : (studio_capture_active
                    ? g_studio_capture_png
                    : cd::studio_rt::golden::options().out_png_path);
            auto wr = cd::asset::image::write_png_rgba(
                out_path, rgba.data(), golden_w, golden_h);
            if (!wr.has_value())
            {
                std::fprintf(stderr,
                    "[golden] PNG write failed: %s\n", out_path.c_str());
                request_shutdown();
                break;
            }
            const auto& w = cd::studio::studio_world();
            std::fprintf(stdout,
                "[studio] captured swapchain PNG -> %s (%ux%u | frame=%u | PIE=%d | Score=%d | Cores=%d/%d | Bots=%d | Victory=%d)\n",
                out_path.c_str(), golden_w, golden_h, s.frame_idx,
                s.pie_playing ? 1 : 0, w.game_score, w.collected_cores,
                w.total_cores_in_level, w.defeated_enemies, w.level_goal_reached ? 1 : 0);
            if (gameplay_seq_path.empty() || gameplay_seq_final)
            {
                request_shutdown();
                break;
            }
        }

        // phase1178 — advance LMB edge-detection state for next frame's
        // InputState (left_pressed / left_released edge signals).
        s.ui_lmb_prev_down = s.ui_lmb_down;
        ++s.frame_idx;
        s.counters.set("frame", s.frame_idx);
        if (g_studio_max_frames > 0U && s.frame_idx >= g_studio_max_frames)
        {
            request_shutdown();
            break;
        }
    }  // while(true)

    request_shutdown();
}

// ---- M2E: on_shutdown -------------------------------------------------------
void StudioApp::on_shutdown() noexcept
{
    if (!state_) return;
    auto& s      = *state_;
    auto& device = *s.device_owner;

    s.renderer.wait_idle();
    s.streamer.stop();

    // phase508-generic-gltf-path: free every GPU resource (vertex / index /
    // texture handles) owned by the generic ingest list. Per
    // destroy_ingest_result's contract the ECS entities are NOT destroyed
    // here -- the surrounding World destruction handles that moments later.
    cd_studio::destroy_generic_ingests(device, s.generic_ingests);
    for (auto& dt : s.custom_disk_textures)
    {
        if (dt.view.is_valid())  device.destroy_texture_view(dt.view);
        if (dt.image.is_valid()) device.destroy_texture(dt.image);
    }
    s.custom_disk_textures.clear();
    s.custom_disk_tex_slots.clear();
    if (s.blas_custom_gltf.is_valid())
    {
        device.destroy_acceleration_structure(s.blas_custom_gltf);
        s.blas_custom_gltf = {};
    }
    if (s.custom_gltf_mesh.vb.is_valid())
    {
        destroy_mesh(device, s.custom_gltf_mesh);
        s.custom_gltf_mesh = {};
    }

    destroy_mesh(device, s.meshes.floor);
    s.rts.destroy(device);
    s.auto_exposure.destroy();  // phase 511 — release GpuReduction (idempotent)
    s.bloom_chain.destroy(device);
    s.shadow_target.destroy(device);
    device.destroy_sampler(s.shadow_sampler);
    device.destroy_buffer(s.shadow_ubo);
    device.destroy_buffer(s.lights_ubo);
    device.destroy_buffer(s.inst_mat_ssbo);
    s.debug_draw.destroy(device);  // phase1058 — VB + parked + pipeline

    if (s.albedo_tex.view.is_valid())  device.destroy_texture_view(s.albedo_tex.view);
    if (s.albedo_tex.image.is_valid()) device.destroy_texture(s.albedo_tex.image);
    device.destroy_sampler(s.albedo_sampler);
    device.destroy_sampler(s.ibl_gpu.ibl_sampler);

    if (s.ibl_gpu.gpu_spec_cube.view.is_valid())
        device.destroy_texture_view(s.ibl_gpu.gpu_spec_cube.view);
    if (s.ibl_gpu.gpu_spec_cube.image.is_valid())
        device.destroy_texture(s.ibl_gpu.gpu_spec_cube.image);
    if (s.ibl_gpu.gpu_diff_cube.view.is_valid())
        device.destroy_texture_view(s.ibl_gpu.gpu_diff_cube.view);
    if (s.ibl_gpu.gpu_diff_cube.image.is_valid())
        device.destroy_texture(s.ibl_gpu.gpu_diff_cube.image);
    if (s.ibl_gpu.gpu_brdf_lut.view.is_valid())
        device.destroy_texture_view(s.ibl_gpu.gpu_brdf_lut.view);
    if (s.ibl_gpu.gpu_brdf_lut.image.is_valid())
        device.destroy_texture(s.ibl_gpu.gpu_brdf_lut.image);

    if (s.normal_tex.view.is_valid())  device.destroy_texture_view(s.normal_tex.view);
    if (s.normal_tex.image.is_valid()) device.destroy_texture(s.normal_tex.image);
    if (s.mr_tex.view.is_valid())      device.destroy_texture_view(s.mr_tex.view);
    if (s.mr_tex.image.is_valid())     device.destroy_texture(s.mr_tex.image);

    device.wait_idle();
    // phase1146: free the DDGI GPU pipeline (pipelines / atlases / UBOs /
    // sampler) before the TLAS + device teardown.
    s.ddgi.shutdown(device);
    if (s.current_tlas.is_valid())
        device.destroy_acceleration_structure(s.current_tlas);
    while (!s.tlas_destroy_queue.empty())
    {
        device.destroy_acceleration_structure(s.tlas_destroy_queue.front().h);
        s.tlas_destroy_queue.pop_front();
    }
    cd_studio::destroy_meshes(device, s.meshes);

    // phase1174 — behavior_designer fold (ADR-20260614). Tear down the
    // cd::ui Submitter explicitly while the device is still alive so the
    // GPU resources are released deterministically (the dtor would also do
    // it, but ordering it here matches the rest of on_shutdown).
    if (s.ui_submitter) { s.ui_submitter->destroy(); }
    s.ui_submitter.reset();
    if (s.imgui_ctx) { s.imgui_ctx->destroy(); }
    s.imgui_ctx.reset();

    std::printf("chromodynamic_studio: clean exit (%u frames).\n", s.frame_idx);
    state_.reset();
}

// ============================================================================
// Studio entry point.
// ============================================================================
int main(int argc, char** argv)
{
    (void)cd::studio_rt::golden::parse(argc, argv);
    bool headless_smoke = false;
    for (int i = 1; i < argc; ++i)
    {
        if (argv[i] == nullptr) continue;
        const std::string_view arg { argv[i] };
        if (arg == "--ddgi-force-on")
        {
            cd_studio::g_ddgi_force_on = true;
        }
        else if (arg == "--pie-play")
        {
            g_studio_start_pie = true;
        }
        else if (arg == "--autopilot" || arg == "--auto-play" || arg == "--play-game")
        {
            g_studio_autopilot_e2e = true;
        }
        else if (arg == "--capture-gameplay-dir" && i + 1 < argc && argv[i + 1] != nullptr)
        {
            g_studio_autopilot_e2e        = true;
            g_studio_capture_gameplay_dir = argv[++i];
            if (g_studio_max_frames == 0U) g_studio_max_frames = 110U;
        }
        else if (arg == "--demo-all-phases")
        {
            g_studio_demo_all_phases = true;
        }
        else if (arg == "--load-context-demo")
        {
            g_studio_load_context_demo = true;
        }
        else if (arg == "--project-manager")
        {
            g_studio_force_project_manager = true;
        }
        else if ((arg == "--capture-window-png" || arg == "--studio-capture-png" ||
                  arg == "--capture-png" || arg == "--screenshot") &&
                 i + 1 < argc && argv[i + 1] != nullptr)
        {
            g_studio_capture_png = argv[++i];
        }
        else if ((arg == "--frames" || arg == "--max-frames" || arg == "--frame-count" ||
                  arg == "--studio-capture-frame" || arg == "--capture-frame") &&
                 i + 1 < argc && argv[i + 1] != nullptr)
        {
            const int f = std::atoi(argv[++i]);
            if (f > 0)
            {
                g_studio_capture_frame = static_cast<std::uint32_t>(f);
                g_studio_max_frames    = static_cast<std::uint32_t>(f) + 4U;
            }
        }
        else if (arg == "--ui-mode" && i + 1 < argc && argv[i + 1] != nullptr)
        {
            const int m = std::atoi(argv[++i]);
            cd::ui::chroma::state().workspace_mode = std::clamp(m, 0, 2);
        }
        else if (arg == "--theme" && i + 1 < argc && argv[i + 1] != nullptr)
        {
            const int t = std::atoi(argv[++i]);
            ImGui::ApplyTheme(t);
        }
        else if ((arg == "--template" || arg == "--studio-template" ||
                  arg == "--level" || arg == "--studio-level") &&
                 i + 1 < argc && argv[i + 1] != nullptr)
        {
            g_studio_initial_template = std::clamp(std::atoi(argv[++i]), 0, 20);
        }
        else if ((arg == "--scene" || arg == "--load-scene") &&
                 i + 1 < argc && argv[i + 1] != nullptr)
        {
            g_studio_initial_scene = argv[++i];
        }
        else if (arg == "--cam-eye" && i + 3 < argc &&
                 argv[i + 1] != nullptr && argv[i + 2] != nullptr && argv[i + 3] != nullptr)
        {
            g_studio_has_cli_cam_eye = true;
            g_studio_cli_cam_eye.x   = static_cast<float>(std::atof(argv[++i]));
            g_studio_cli_cam_eye.y   = static_cast<float>(std::atof(argv[++i]));
            g_studio_cli_cam_eye.z   = static_cast<float>(std::atof(argv[++i]));
        }
        else if (arg == "--cam-target" && i + 3 < argc &&
                 argv[i + 1] != nullptr && argv[i + 2] != nullptr && argv[i + 3] != nullptr)
        {
            g_studio_has_cli_cam_target = true;
            g_studio_cli_cam_target.x   = static_cast<float>(std::atof(argv[++i]));
            g_studio_cli_cam_target.y   = static_cast<float>(std::atof(argv[++i]));
            g_studio_cli_cam_target.z   = static_cast<float>(std::atof(argv[++i]));
        }
        else if (arg == "--headless" || arg == "--null")
        {
            headless_smoke = true;
        }
    }
    if (headless_smoke && g_studio_capture_png.empty() &&
        g_studio_capture_gameplay_dir.empty() && !cd::studio_rt::golden::enabled())
    {
        std::printf("chromodynamic_studio: headless smoke pass (PIE=%d)\n",
                    g_studio_start_pie ? 1 : 0);
        return 0;
    }
    return cd::app::run<StudioApp>(argc, argv);
}
// ============================================================================
