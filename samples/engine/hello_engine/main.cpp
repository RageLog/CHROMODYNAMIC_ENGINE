// =============================================================================
// CHROMODYNAMIC - samples/hello_engine
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
#include <cd/asset/AsyncStreamer.hpp>
#include <cd/asset/Primitives.hpp>
#include <cd/asset/StreamRequest.hpp>
#include <cd/asset_gltf/GltfLoader.hpp>
#include <cd/asset_gltf/SkinnedMeshBridge.hpp>
#include <cd/asset_json/Json.hpp>
#include <cd/atmosphere/Atmosphere.hpp>
#include <cd/audio/Compressor.hpp>
#include <cd/audio/IAudioBackend.hpp>
#include <cd/audio/Limiter.hpp>
#include <cd/audio/LowPass.hpp>
#include <cd/audio/Mixer.hpp>
#include <cd/audio/SimpleReverb.hpp>
#include <cd/audio/WasapiBackend.hpp>
#include <cd/brdf_ltc/Ltc.hpp>
#include <cd/brdf_sheen_clearcoat/SheenClearcoat.hpp>
#include <cd/brdf_sss/Sss.hpp>
#include <cd/camera/Camera.hpp>
#include <cd/camera/Frustum.hpp>
#include <cd/concurrency/JobGraph.hpp>
#include <cd/concurrency/ParallelFor.hpp>
#include <cd/concurrency/WorkStealingThreadPool.hpp>
#include <cd/core/CounterTable.hpp>
#include <cd/ddgi/Ddgi.hpp>
#include <cd/decal/Decal.hpp>
#include <cd/ecs/Entity.hpp>
#include <cd/ecs/World.hpp>
#include <cd/editor/AxisGizmo.hpp>
#include <cd/editor/CommandPalette.hpp>
#include <cd/editor/EditHistory.hpp>
#include <cd/editor/SelectionOutline.hpp>
#include <cd/editor/TransformCommands.hpp>
#include <cd/frame_timing/FrameTimeRing.hpp>
#include <cd/gpu_particles/GpuParticles.hpp>
#include <cd/ibl/BrdfLut.hpp>
#include <cd/ibl/Cubemap.hpp>
#include <cd/ibl/IrradianceConvolution.hpp>
#include <cd/ibl/PrefilteredSpecular.hpp>
#include <cd/ibl_gpu/Upload.hpp>
#include <cd/imgui/Context.hpp>
#include <cd/light/Attenuation.hpp>
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

// W8-AR: <cd/material/StandardPbrMaterial.hpp> include REMOVED.
// hello_engine no longer uses the dedicated StandardPbr pipeline;
// the PBR demo spheres are unified into the kPrimFS path. The
// library header stays available for hello_pbr and external samples.
#include <cd/framegraph/Targets.hpp>
#include <cd/math/Matrix.hpp>
#include <cd/math/Quaternion.hpp>
#include <cd/math/Random.hpp>
#include <cd/math/Transform.hpp>
#include <cd/math/Vector.hpp>
#include <cd/net/DeltaWriter.hpp>
#include <cd/net/LatencyStats.hpp>
#include <cd/net/SnapshotBuffer.hpp>
#include <cd/net/Throttle.hpp>
#include <cd/platform/Window.hpp>
#include <cd/post_bloom/Bloom.hpp>
#include <cd/post_composite/Composite.hpp>
#include <cd/post_dof/Dof.hpp>
#include <cd/post_gtao/Gtao.hpp>
#include <cd/post_motion_blur/MotionBlur.hpp>
#include <cd/post_smaa/Smaa.hpp>
#include <cd/post_ssr/Ssr.hpp>
#include <cd/post_taa/Taa.hpp>
#include <cd/render/MeshUpload.hpp>
#include <cd/render/PlanarShadow.hpp>
#include <cd/render/Renderer.hpp>
#include <cd/rhi/Barriers.hpp>
#include <cd/rhi/ICommandBuffer.hpp>
#include <cd/rhi/IDevice.hpp>
#include <cd/rhi_vulkan/VulkanDevice.hpp>
#include <cd/scene/Scene.hpp>
#include <cd/scene/SceneCameraController.hpp>
#include <cd/scene/Serializer.hpp>
#include <cd/shader/Compiler.hpp>
#include <cd/velocity/Velocity.hpp>
#include <cd/volumetric_clouds/Clouds.hpp>
#include <cd/volumetric_fog/Fog.hpp>
#include <cd/world_container/World.hpp>
#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <fstream>
#include <ios>
#include <optional>
#include <span>
#include <string>
#include <thread>
#include <vector>

#include "PrimShader.hpp"
#include "HelloLighting.hpp"
#include "HelloRayQuery.hpp"
#include "HelloPbrGrid.hpp"


namespace
{

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

// ============================================================================
// Per-entity 3D draw data.
// ============================================================================

enum class PrimitiveKind : std::uint8_t
{
    kCube,
    kSphere,
    kCone,
    kCylinder,
    kTorus,
    kGltf,  ///< user-supplied glTF asset auto-loaded at boot
};

struct SceneEntity
{
    cd::ecs::Entity handle {};
    std::string name;
    cd::math::Vec3f tint { 1.0F, 1.0F, 1.0F };
    float metallic { 0.0F };
    float roughness { 0.5F };
    PrimitiveKind kind { PrimitiveKind::kCube };
    // W8-AR: ECS-driven PBR sphere flag. When true the entity renders
    // through kPrimFS's tint.w==3.0 PBR branch (Cook-Torrance + GGX +
    // multi-light + Karis IBL), reading metallic/roughness from the
    // SceneEntity fields above. When false the standard primitive
    // path runs (vertex-coloured or textured albedo, hemisphere +
    // Lambert + cd_lights). One render loop, one shader, one shadow
    // pass — PBR is just a per-entity attribute now.
    bool is_pbr { false };
    // R3 phase 226 - previous-frame model matrix, populated at the end
    // of each frame's main pass for the velocity pass to use next frame.
    // On the first frame this equals the curr model so the velocity
    // output is zero (no motion).
    cd::math::Mat4f prev_model { cd::math::Mat4f::identity() };
    bool prev_model_valid { false };
};

// Reserved for save/load round-trip - currently unused but documents
// the convention.
[[maybe_unused]] [[nodiscard]] PrimitiveKind kind_from_name(std::string_view n) noexcept
{
    if (n == "Sphere")
        return PrimitiveKind::kSphere;
    if (n == "Cone")
        return PrimitiveKind::kCone;
    if (n == "Cylinder")
        return PrimitiveKind::kCylinder;
    if (n == "Torus")
        return PrimitiveKind::kTorus;
    return PrimitiveKind::kCube;
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
using cd::render::upload_mesh;
using cd::render::destroy_mesh;

// W8-AR: PbrVertex / to_pbr_vertices / upload_pbr_mesh REMOVED.
// They existed to feed the dedicated StandardPbrMaterial pipeline
// with a pos+normal-only stream. After W8-AR the PBR demo spheres
// are ECS entities rendered through the same PrimitiveVertex
// (pos+normal+uv+color) stream every other primitive uses.

// =============================================================================
// Phase 289 / Marathon Run 7 sub-N1A: the embedded kPrimVS / kPrimFS / kShadowVS
// / kShadowFS GLSL strings (~990 lines total) live in PrimShader.hpp +
// PrimShader_kPrimFS.inl + PrimShader_kShadow.inl now. main.cpp brings
// them back into the anonymous namespace via using-decls so existing
// call sites stay identical.
// =============================================================================
using cd::hello_engine::kPrimVS;
using cd::hello_engine::kPrimFS;
using cd::hello_engine::kShadowVS;
using cd::hello_engine::kShadowFS;


// R3 - Composite pass - shader source + push struct extracted into
// cd::post_composite. hello_engine just references the namespace via
// the using-decls below.
using cd::post_composite::kCompositeFS;
using cd::post_composite::kCompositeVS;
using CompositePush = cd::post_composite::Push;

// R3 - Multi-mip bloom (Karis 2013) - shader source + push struct
// definitions are extracted into cd::post_bloom. hello_engine just
// references them via the namespace.
using cd::post_bloom::kDownsampleFS;
using cd::post_bloom::kPrefilterFS;
using cd::post_bloom::kUpsampleFS;
using BloomPrefilterPush = cd::post_bloom::PrefilterPush;
using BloomUpsamplePush = cd::post_bloom::UpsamplePush;

// =============================================================================
// Phase 290 / Marathon Run 7 sub-N1B: PrimPush + LightSlotGpu + LightUboGpu
// layouts + the pack_light_slot() helper live in HelloLighting.hpp. main.cpp
// re-imports the names so existing call sites stay identical.
// =============================================================================
using cd::hello_engine::PrimPush;
using cd::hello_engine::LightSlotGpu;
using cd::hello_engine::LightUboGpu;
using cd::hello_engine::pack_light_slot;

// Upload an RGBA8 image to a freshly-created GPU texture. Returns
// invalid handles on failure. Lifetime: caller owns the texture +
// view + sampler; destroy at exit. Used by the default-white
// fallback and the glTF baseColor path (#1/#13).
struct GpuTexture2D
{
    cd::rhi::TextureHandle image {};
    cd::rhi::TextureViewHandle view {};
};

[[nodiscard]] inline GpuTexture2D
create_texture_rgba8(cd::rhi::IDevice& dev, const std::uint8_t* rgba, std::uint32_t w, std::uint32_t h)
{
    GpuTexture2D out {};
    if (rgba == nullptr || w == 0 || h == 0)
        return out;
    cd::rhi::TextureDesc td {};
    td.type = cd::rhi::TextureType::k2D;
    td.format = cd::rhi::Format::kRGBA8Unorm;
    td.extent = { w, h, 1 };
    td.mip_levels = 1;
    td.array_layers = 1;
    td.usage = cd::rhi::TextureUsage::kSampled | cd::rhi::TextureUsage::kTransferDst;
    td.memory = cd::rhi::MemoryUsage::kGpuOnly;
    auto img = dev.create_texture(td);
    if (!img.has_value())
        return out;
    out.image = *img;
    // Staging buffer upload.
    const std::size_t bytes = static_cast<std::size_t>(w) * h * 4;
    cd::rhi::BufferDesc sd {};
    sd.size = bytes;
    sd.usage = cd::rhi::BufferUsage::kTransferSrc;
    sd.memory = cd::rhi::MemoryUsage::kCpuToGpu;
    auto staging_r = dev.create_buffer(sd);
    if (!staging_r.has_value())
        return out;
    const auto staging = *staging_r;
    (void)dev.upload_buffer(staging, 0, std::span<const std::byte>(reinterpret_cast<const std::byte*>(rgba), bytes));
    auto cmd = dev.create_command_buffer(cd::rhi::QueueType::kGraphics);
    if (cmd == nullptr)
    {
        dev.destroy_buffer(staging);
        return out;
    }
    cmd->begin();
    std::array<cd::rhi::TextureBarrier, 1> tb_dst {
        cd::rhi::TextureBarrier { .texture = out.image,
                                 .from = cd::rhi::ResourceState::kUndefined,
                                 .to = cd::rhi::ResourceState::kTransferDst,
                                 .range = { 0, 1, 0, 1 } }
    };
    cmd->barrier({}, tb_dst);
    std::array<cd::rhi::BufferImageCopyRegion, 1> regs {
        cd::rhi::BufferImageCopyRegion { .buffer_offset = 0,
                                        .mip_level = 0,
                                        .base_layer = 0,
                                        .layer_count = 1,
                                        .image_offset = { 0, 0, 0 },
                                        .image_extent = { w, h, 1 } }
    };
    cmd->copy_buffer_to_image(staging, out.image, regs);
    std::array<cd::rhi::TextureBarrier, 1> tb_read {
        cd::rhi::TextureBarrier { .texture = out.image,
                                 .from = cd::rhi::ResourceState::kTransferDst,
                                 .to = cd::rhi::ResourceState::kShaderResource,
                                 .range = { 0, 1, 0, 1 } }
    };
    cmd->barrier({}, tb_read);
    cmd->end();
    cd::rhi::SubmitDesc sub {};
    std::array<cd::rhi::ICommandBuffer*, 1> cbs { cmd.get() };
    sub.command_buffers = cbs;
    (void)dev.submit(sub);
    dev.wait_idle();
    dev.destroy_buffer(staging);
    cd::rhi::TextureViewDesc vd {};
    vd.texture = out.image;
    vd.type = cd::rhi::TextureType::k2D;
    vd.format = cd::rhi::Format::kRGBA8Unorm;
    vd.base_mip = 0;
    vd.mip_count = 1;
    vd.base_layer = 0;
    vd.layer_count = 1;
    auto v = dev.create_texture_view(vd);
    if (!v.has_value())
    {
        dev.destroy_texture(out.image);
        out.image = {};
        return out;
    }
    out.view = *v;
    return out;
}

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
using BloomMipChain = cd::post_bloom::BloomMipChain;
using cd::framegraph::create_color_target;
using cd::framegraph::create_depth_target;
using cd::post_bloom::create_bloom_chain;

// ============================================================================
// Mini histogram helper for the random viz panel.
// ============================================================================
struct Histogram
{
    std::vector<std::size_t> bins;
    float lo { 0 };
    float hi { 1 };

    void rebuild(std::span<const float> samples, float lo_, float hi_, int n_bins)
    {
        lo = lo_;
        hi = hi_;
        bins.assign(static_cast<std::size_t>(n_bins), 0);
        const float inv = static_cast<float>(n_bins) / (hi - lo);
        for (float s : samples)
        {
            if (s < lo || s >= hi)
                continue;
            int idx = static_cast<int>((s - lo) * inv);
            if (idx >= 0 && idx < n_bins)
                ++bins[static_cast<std::size_t>(idx)];
        }
    }
};

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

// ---- draw_counters_panel --------------------------------------------------
inline void draw_counters_panel(const cd::core::CounterTable& counters,
                                float dt,
                                std::uint32_t frame_idx)
{
    ImGui::Begin("Counters");
    const auto snap = counters.snapshot();
    // Phase 139 - FPS / dt readout up top.
    // W5-G: 120-frame ring + statistics extracted to cd::frame_timing
    // (header-only foundation lib). Single source of truth for the
    // mean / median / p99 math so future samples can reuse the same
    // widget without re-deriving the ring logic.
    static cd::frame_timing::FrameTimeRing<120> dt_ring;
    dt_ring.push(dt);
    const auto fts = dt_ring.stats();
    const double fps_inst = (dt > 0.0F) ? (1.0 / static_cast<double>(dt)) : 0.0;
    ImGui::Text("FPS avg: %5.1f  median: %5.1f  inst: %5.1f", fts.fps_mean(), fts.fps_median(), fps_inst);
    ImGui::Text(
        "dt: %.2f ms  p99: %.2f ms  frame: %u",
        fts.mean * 1000.0,
        static_cast<double>(fts.p99) * 1000.0,
        frame_idx
    );
    {
        // dt histogram so stutter spikes are visually obvious — feed
        // PlotHistogram the ring in oldest-first order so the X axis
        // reads left-to-right as time.
        static std::vector<float> dt_plot_buf;
        dt_ring.copy_in_order(dt_plot_buf);
        for (auto& v : dt_plot_buf)
            v *= 1000.0F;  // s -> ms
        ImGui::PlotHistogram(
            "##dt_hist",
            dt_plot_buf.empty() ? nullptr : dt_plot_buf.data(),
            static_cast<int>(dt_plot_buf.size()),
            0,
            "frame time (ms)",
            0.0F,
            std::max(40.0F, fts.p99 * 1000.0F * 1.2F),
            ImVec2(0, 40)
        );
    }
    ImGui::Separator();
    for (const auto& [name, value] : snap)
    {
        ImGui::Text("%-20s %lld", name.c_str(), static_cast<long long>(value));
    }
    ImGui::End();
}

// ---- draw_random_panel ----------------------------------------------------
inline void draw_random_panel(const Histogram& hist_uniform, const Histogram& hist_normal)
{
    ImGui::Begin("Random");
    ImGui::TextDisabled("PCG32 + Box-Muller (auto-refresh ~2s)");
    ImGui::SeparatorText("Uniform [0,1)");
    if (!hist_uniform.bins.empty())
    {
        std::vector<float> bars(hist_uniform.bins.size());
        std::size_t peak = 1;
        for (auto b : hist_uniform.bins)
            if (b > peak)
                peak = b;
        for (std::size_t i = 0; i < hist_uniform.bins.size(); ++i)
            bars[i] = static_cast<float>(hist_uniform.bins[i]) / static_cast<float>(peak);
        ImGui::PlotHistogram(
            "##uniform",
            bars.data(),
            static_cast<int>(bars.size()),
            0,
            nullptr,
            0.0F,
            1.0F,
            ImVec2(0, 60)
        );
    }
    ImGui::SeparatorText("N(0,1) Box-Muller");
    if (!hist_normal.bins.empty())
    {
        std::vector<float> bars(hist_normal.bins.size());
        std::size_t peak = 1;
        for (auto b : hist_normal.bins)
            if (b > peak)
                peak = b;
        for (std::size_t i = 0; i < hist_normal.bins.size(); ++i)
            bars[i] = static_cast<float>(hist_normal.bins[i]) / static_cast<float>(peak);
        ImGui::PlotHistogram(
            "##normal",
            bars.data(),
            static_cast<int>(bars.size()),
            0,
            nullptr,
            0.0F,
            1.0F,
            ImVec2(0, 60)
        );
    }
    ImGui::End();
}

// ---- draw_history_panel ---------------------------------------------------
inline void draw_history_panel(const cd::editor::EditHistory& history,
                               const std::deque<std::string>& log)
{
    ImGui::Begin("History");
    ImGui::Text(
        "undo depth %zu  redo depth %zu  (bytes %zu)",
        history.undo_depth(),
        history.redo_depth(),
        history.bytes_in_use()
    );
    ImGui::Separator();
    for (auto it = log.rbegin(); it != log.rend(); ++it)
        ImGui::TextUnformatted(it->c_str());
    ImGui::End();
}

// =============================================================================
// Phase 295 / Marathon Run 7 sub-N1G: scene-bootstrap helpers extracted
// from main(). These are sample-local (operate on the anon-namespace
// SceneEntity / PrimitiveKind) so they live in main.cpp rather than a
// library header. main() shrinks and the boot region reads as four
// named call sites instead of three nested initializer blocks.
// =============================================================================

// ---- setup_world_container ------------------------------------------------
// Build the passive editor outliner backing: one Project, one Main
// Level with stadium-sized bounds, two layers ("Lights", "UI"). The
// resulting World is purely descriptive metadata for the Outliner
// panel; entities still live in the ECS Scene.
inline void setup_world_container(cd::world_container::World& w)
{
    w.set_name("Sample World");
    auto proj = std::make_unique<cd::world_container::Project>("Sample Project");
    auto* lvl = proj->add_level("Main");
    lvl->bounds().min = { -40.0F, -2.0F, -40.0F };
    lvl->bounds().max = { 40.0F, 10.0F, 40.0F };
    lvl->add_layer("Lights");
    lvl->add_layer("UI");
    w.set_project(std::move(proj));
}

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
        const char* name;
        cd::math::Vec3f pos;
        cd::math::Vec3f tint;
        PrimitiveKind k;
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

// ---- spawn_gltf_or_earth_entity ------------------------------------------
// Spawn the "lead actor" entity at x=-4.5. When a real glTF mesh was
// loaded at boot, place the imported character with the Z-up -> Y-up
// (-90 deg about X) correction Cesium scenes need. Otherwise spawn
// the procedural Earth-like showcase sphere at the same off-row
// anchor so the procedural fallback does not clip into the Cone at
// origin. Kind stays kGltf either way - mesh_for(kGltf) selects the
// imported VB when valid, sphere VB otherwise.
inline void spawn_gltf_or_earth_entity(cd::scene::Scene& scene,
                                       const GpuMesh& gltf_mesh,
                                       std::string_view gltf_loaded_name,
                                       std::vector<SceneEntity>& entities)
{
    SceneEntity e;
    e.handle = scene.create_node();
    if (gltf_mesh.vb.is_valid())
    {
        e.name = std::string { "glTF (" } + std::string { gltf_loaded_name } + ")";
        scene.local(e.handle)->value.position = { -4.5F, -0.55F, 0.0F };
        scene.local(e.handle)->value.scale = { 2.2F, 2.2F, 2.2F };
        // X -90 deg rotation (Z-up -> Y-up).
        scene.local(e.handle)->value.rotation = { -0.7071068F, 0.0F, 0.0F, 0.7071068F };
    }
    else
    {
        e.name = "Earth (procedural showcase)";
        scene.local(e.handle)->value.position = { -4.5F, 0.7F, 0.0F };
        scene.local(e.handle)->value.scale = { 1.5F, 1.5F, 1.5F };
    }
    e.tint = { 1.0F, 1.0F, 1.0F };
    e.kind = PrimitiveKind::kGltf;
    entities.push_back(std::move(e));
}

// ---- spawn_pbr_grid_entities ---------------------------------------------
// Consume the 4x4 PbrGridSlot array from HelloPbrGrid.hpp and assemble
// one SceneEntity per slot. Sample-local SceneEntity / PrimitiveKind
// assembly stays here; pure data + math lives in the header.
inline void spawn_pbr_grid_entities(cd::scene::Scene& scene,
                                    std::vector<SceneEntity>& entities)
{
    for (const auto& slot : cd::hello_engine::build_pbr_demo_grid())
    {
        SceneEntity e;
        e.handle = scene.create_node();
        e.name = slot.name;
        e.kind = PrimitiveKind::kSphere;
        e.tint = slot.tint;
        e.is_pbr = true;
        e.metallic = slot.metallic;
        e.roughness = slot.roughness;
        scene.local(e.handle)->value.position = slot.position;
        scene.local(e.handle)->value.scale = { slot.scale, slot.scale, slot.scale };
        entities.push_back(std::move(e));
    }
}

}  // namespace

// ============================================================================
// Main.
// ============================================================================
int main()
{
    // ---- Window + Vulkan device + Renderer + ImGui ----
    cd::platform::WindowDesc wd {};
    wd.title = "CHROMODYNAMIC - hello_engine (mega-showcase)";
    wd.width = 1600;
    wd.height = 900;
    auto window_r = cd::platform::create_window(wd);
    if (!window_r.has_value())
        return 1;
    auto& window = **window_r;

    cd::rhi_vulkan::VulkanCreateInfo vci {};
    auto dev_r = cd::rhi_vulkan::create_vulkan_device(vci);
    if (!dev_r.has_value())
        return 2;
    auto& device = **dev_r;

    cd::render::RendererDesc rd {};
    rd.device = &device;
    rd.swapchain.window_handle = window.native_window_handle();
    rd.swapchain.display_handle = window.native_display_handle();
    rd.swapchain.extent = { window.width(), window.height() };
    rd.swapchain.format = cd::rhi::Format::kBGRA8Unorm;
    // B13: vsync off so the FPS counter reflects actual render cost.
    // Wave-1 sample was capped at the display refresh (60 Hz ? 60 FPS
    // even when GPU could push 760+). Mailbox present mode reduces
    // tearing without locking to refresh rate.
    rd.swapchain.vsync = false;
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

    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    bool dock_initialised = false;

    // ---- Shader compiler + materials ----
    auto compiler = cd::shader::make_glslang_compiler();
    if (compiler == nullptr)
        return 5;

    constexpr auto kDepthFormat = cd::rhi::Format::kD32Float;
    DepthTarget depth {};
    // kSampled - needed for the composite-pass GTAO inline AO that
    // samples the scene depth after the HDR pass ends.
    if (!create_depth_target(
            device,
            { window.width(), window.height() },
            kDepthFormat,
            depth,
            cd::rhi::TextureUsage::kSampled
        ))
        return 6;
    bool depth_initialised_on_gpu = false;

    // R3 - HDR offscreen color target. Scene + sky + UI overlay all
    // draw into this RGBA16F target; a separate composite pass blits
    // it to the swapchain with tonemap + saturation correction.
    constexpr auto kHdrFormat = cd::rhi::Format::kRGBA16Float;
    ColorTarget hdr_target {};
    if (!create_color_target(device, { window.width(), window.height() }, kHdrFormat, hdr_target))
        return 31;

    // R3 G-Buffer foundation - world-space surface normal target.
    // Every scene FS (prim, PBR, sky) MRT-writes its world-space
    // normal here so downstream post-fx (SSR, GTAO with normals,
    // future reflections) can sample it. RGBA16F encodes the
    // 3-component normal directly (xyz) + a flag in w (1 = surface,
    // 0 = sky / no surface). Recreated on swapchain rebuild.
    constexpr auto kNormalFormat = cd::rhi::Format::kRGBA16Float;
    ColorTarget gbuf_normal {};
    if (!create_color_target(device, { window.width(), window.height() }, kNormalFormat, gbuf_normal))
        return 47;

    // R3 G-Buffer phase 219 - Albedo + MR (metallic / roughness).
    // Unlocks proper deferred shading + SSR colour-tint by surface
    // properties + future GI integration. Pixel cost ??? 5 B per pixel.
    constexpr auto kAlbedoFormat = cd::rhi::Format::kRGBA8Unorm;
    constexpr auto kMrFormat = cd::rhi::Format::kRG8Unorm;
    ColorTarget gbuf_albedo {};
    if (!create_color_target(device, { window.width(), window.height() }, kAlbedoFormat, gbuf_albedo))
        return 49;
    ColorTarget gbuf_mr {};
    if (!create_color_target(device, { window.width(), window.height() }, kMrFormat, gbuf_mr))
        return 50;

    // R3 phase 226 - Velocity G-Buffer (RG16F = curr_uv - prev_uv).
    // Written by a separate velocity pass after the HDR scene using
    // cd::velocity::kVelocityVS + kVelocityFS so the main scene
    // shaders stay unchanged (no MRT velocity in prim/PBR). Composite
    // samples this for proper per-mesh motion blur + TAA reprojection.
    constexpr auto kVelocityFormat = cd::rhi::Format::kRG16Float;
    ColorTarget gbuf_velocity {};
    if (!create_color_target(device, { window.width(), window.height() }, kVelocityFormat, gbuf_velocity))
        return 51;

    // R3 TAA history - ping-pong color targets at swapchain format.
    // Each frame, composite reads history[frame & 1] (last frame's
    // post-tonemap blend) and writes to history[(frame & 1) ^ 1]
    // (this frame's blend, for next frame). MRT 2nd attachment in
    // the composite render pass.
    constexpr auto kHistoryFormat = cd::rhi::Format::kBGRA8Unorm;
    std::array<ColorTarget, 2> history_targets {};
    for (auto& h : history_targets)
    {
        if (!create_color_target(device, { window.width(), window.height() }, kHistoryFormat, h))
            return 48;
    }

    // R3 phase 219: scene materials MRT-write 4 targets:
    //   location 0: HDR colour (RGBA16F)
    //   location 1: world-space normal + surface-flag (RGBA16F)
    //   location 2: albedo + material-flag (RGBA8Unorm)
    //   location 3: metallic + roughness (RG8Unorm)
    // Every scene pipeline shares this layout so attachment layout
    // matches the HDR pass's begin_render_pass.
    constexpr std::array<cd::rhi::Format, 4> kColorFmts { cd::rhi::Format::kRGBA16Float,
                                                          cd::rhi::Format::kRGBA16Float,
                                                          cd::rhi::Format::kRGBA8Unorm,
                                                          cd::rhi::Format::kRG8Unorm };
    // (composite uses kCompositeFmts [swapchain + history] declared below;
    //  this single-slot kSwapchainFmts is preserved for symmetry / docs.)
    [[maybe_unused]] constexpr std::array<cd::rhi::Format, 1> kSwapchainFmts { cd::rhi::Format::kBGRA8Unorm };

    // Sky material - no vertex buffer, depth off.
    cd::material::MaterialDesc sky_md {};
    sky_md.vertex_glsl = cd::material::kAnalyticalSkyVS;
    sky_md.fragment_glsl = cd::material::kAnalyticalSkyFS;
    sky_md.color_attachment_formats = kColorFmts;
    constexpr std::array<cd::rhi::PushConstantRange, 1> kSkyPush {
        cd::rhi::PushConstantRange { .stages = cd::rhi::ShaderStage::kVertex | cd::rhi::ShaderStage::kFragment,
                                    .offset = 0,
                                    .size = static_cast<std::uint32_t>(sizeof(cd::material::AnalyticalSkyPush)) }
    };
    sky_md.push_constants = kSkyPush;
    sky_md.raster.cull = cd::rhi::CullMode::kNone;
    sky_md.depth_stencil.depth_test = false;
    sky_md.depth_stencil.depth_write = false;
    sky_md.name = "hello_engine/sky";
    auto sky_r = cd::material::Material::create(device, compiler.get(), sky_md);
    if (!sky_r.has_value())
        return 7;
    auto& sky_material = *sky_r;

    // R3 composite material - full-screen triangle, samples HDR target,
    // writes to swapchain. Carries the tonemap + saturation pass that
    // previously lived inline in prim/PBR FS.
    // Composite writes BOTH to the swapchain (final tonemapped LDR
    // for display) AND to a 2nd target = next-frame TAA history.
    constexpr std::array<cd::rhi::Format, 2> kCompositeFmts {
        cd::rhi::Format::kBGRA8Unorm,  // swapchain - visible output
        cd::rhi::Format::kBGRA8Unorm   // history target - for TAA next frame
    };
    cd::material::MaterialDesc comp_md {};
    comp_md.vertex_glsl = kCompositeVS;
    comp_md.fragment_glsl = kCompositeFS;
    comp_md.color_attachment_formats = kCompositeFmts;
    constexpr std::array<cd::rhi::PushConstantRange, 1> kCompositePush {
        cd::rhi::PushConstantRange { .stages = cd::rhi::ShaderStage::kFragment,
                                    .offset = 0,
                                    .size = sizeof(CompositePush) }
    };
    comp_md.push_constants = kCompositePush;
    constexpr std::array<cd::rhi::DescriptorSetLayoutBinding, 6> kCompositeBindings {
        cd::rhi::DescriptorSetLayoutBinding { .binding = 0,
                                             .type = cd::rhi::DescriptorType::kCombinedImageSampler,
                                             .count = 1,
                                             .stages = cd::rhi::ShaderStage::kFragment },
        cd::rhi::DescriptorSetLayoutBinding { .binding = 1,
                                             .type = cd::rhi::DescriptorType::kCombinedImageSampler,
                                             .count = 1,
                                             .stages = cd::rhi::ShaderStage::kFragment },
        cd::rhi::DescriptorSetLayoutBinding { .binding = 2,
                                             .type = cd::rhi::DescriptorType::kCombinedImageSampler,
                                             .count = 1,
                                             .stages = cd::rhi::ShaderStage::kFragment },
        cd::rhi::DescriptorSetLayoutBinding { .binding = 3,
                                             .type = cd::rhi::DescriptorType::kCombinedImageSampler,
                                             .count = 1,
                                             .stages = cd::rhi::ShaderStage::kFragment },
        cd::rhi::DescriptorSetLayoutBinding { .binding = 4,
                                             .type = cd::rhi::DescriptorType::kCombinedImageSampler,
                                             .count = 1,
                                             .stages = cd::rhi::ShaderStage::kFragment },
        cd::rhi::DescriptorSetLayoutBinding { .binding = 5, // gbuf_velocity (R3 phase 226)
                                              .type = cd::rhi::DescriptorType::kCombinedImageSampler,
                                             .count = 1,
                                             .stages = cd::rhi::ShaderStage::kFragment }
    };
    comp_md.descriptor_bindings = kCompositeBindings;
    comp_md.raster.cull = cd::rhi::CullMode::kNone;
    comp_md.depth_stencil.depth_test = false;
    comp_md.depth_stencil.depth_write = false;
    comp_md.name = "hello_engine/composite";
    auto comp_r = cd::material::Material::create(device, compiler.get(), comp_md);
    if (!comp_r.has_value())
        return 32;
    auto& composite_material = *comp_r;

    // R3 Multi-mip Bloom - Karis stable pipeline.
    // 3 fullscreen-triangle materials sharing the composite VS.
    // Color attachment format = RGBA16Float so HDR mip chain preserves
    // overshoot through prefilter -> downsample -> upsample.
    constexpr std::array<cd::rhi::Format, 1> kHdrFmts { kHdrFormat };
    constexpr std::array<cd::rhi::DescriptorSetLayoutBinding, 1> kBloomBindings {
        cd::rhi::DescriptorSetLayoutBinding { .binding = 0,
                                             .type = cd::rhi::DescriptorType::kCombinedImageSampler,
                                             .count = 1,
                                             .stages = cd::rhi::ShaderStage::kFragment }
    };
    constexpr std::array<cd::rhi::PushConstantRange, 1> kBloomPrefilterPushRange {
        cd::rhi::PushConstantRange { .stages = cd::rhi::ShaderStage::kFragment,
                                    .offset = 0,
                                    .size = sizeof(BloomPrefilterPush) }
    };
    constexpr std::array<cd::rhi::PushConstantRange, 1> kBloomUpsamplePushRange {
        cd::rhi::PushConstantRange { .stages = cd::rhi::ShaderStage::kFragment,
                                    .offset = 0,
                                    .size = sizeof(BloomUpsamplePush) }
    };

    cd::material::MaterialDesc bp_md {};
    bp_md.vertex_glsl = kCompositeVS;
    bp_md.fragment_glsl = std::string_view { kPrefilterFS };
    bp_md.color_attachment_formats = kHdrFmts;
    bp_md.push_constants = kBloomPrefilterPushRange;
    bp_md.descriptor_bindings = kBloomBindings;
    bp_md.raster.cull = cd::rhi::CullMode::kNone;
    bp_md.depth_stencil.depth_test = false;
    bp_md.depth_stencil.depth_write = false;
    bp_md.name = "hello_engine/bloom/prefilter";
    auto bp_r = cd::material::Material::create(device, compiler.get(), bp_md);
    if (!bp_r.has_value())
        return 40;
    auto& bloom_prefilter_material = *bp_r;

    cd::material::MaterialDesc bd_md {};
    bd_md.vertex_glsl = kCompositeVS;
    bd_md.fragment_glsl = std::string_view { kDownsampleFS };
    bd_md.color_attachment_formats = kHdrFmts;
    bd_md.descriptor_bindings = kBloomBindings;
    bd_md.raster.cull = cd::rhi::CullMode::kNone;
    bd_md.depth_stencil.depth_test = false;
    bd_md.depth_stencil.depth_write = false;
    bd_md.name = "hello_engine/bloom/downsample";
    auto bd_r = cd::material::Material::create(device, compiler.get(), bd_md);
    if (!bd_r.has_value())
        return 41;
    auto& bloom_downsample_material = *bd_r;

    cd::material::MaterialDesc bu_md {};
    bu_md.vertex_glsl = kCompositeVS;
    bu_md.fragment_glsl = std::string_view { kUpsampleFS };
    bu_md.color_attachment_formats = kHdrFmts;
    bu_md.push_constants = kBloomUpsamplePushRange;
    bu_md.descriptor_bindings = kBloomBindings;
    bu_md.raster.cull = cd::rhi::CullMode::kNone;
    // Additive blend so up-chain sum accumulates onto the previous mip.
    constexpr std::array<cd::rhi::BlendAttachmentState, 1> kBloomUpsampleBlend {
        cd::rhi::BlendAttachmentState { .blend_enable = true,
                                       .src_color = cd::rhi::BlendFactor::kOne,
                                       .dst_color = cd::rhi::BlendFactor::kOne,
                                       .color_op = cd::rhi::BlendOp::kAdd,
                                       .src_alpha = cd::rhi::BlendFactor::kOne,
                                       .dst_alpha = cd::rhi::BlendFactor::kOne,
                                       .alpha_op = cd::rhi::BlendOp::kAdd }
    };
    bu_md.blend_attachments = kBloomUpsampleBlend;
    bu_md.depth_stencil.depth_test = false;
    bu_md.depth_stencil.depth_write = false;
    bu_md.name = "hello_engine/bloom/upsample";
    auto bu_r = cd::material::Material::create(device, compiler.get(), bu_md);
    if (!bu_r.has_value())
        return 42;
    auto& bloom_upsample_material = *bu_r;

    // W8-AR: pbr_material (StandardPbrMaterial pipeline) REMOVED.
    // The 5x5 sphere sweep is now ECS-driven and renders through the
    // unified prim_material pipeline below (kPrimFS tint.w==3.0 branch).

    // Primitive shader (PrimitiveVertex layout, simple Lambert + tint).
    constexpr std::array<cd::rhi::VertexBinding, 1> kPrimBindings {
        cd::rhi::VertexBinding { 0, sizeof(cd::asset::PrimitiveVertex), false }
    };
    constexpr std::array<cd::rhi::VertexAttribute, 4> kPrimAttrs {
        cd::rhi::VertexAttribute { 0, 0, cd::rhi::Format::kRGB32Float, offsetof(cd::asset::PrimitiveVertex, pos)    },
        cd::rhi::VertexAttribute { 1, 0, cd::rhi::Format::kRGB32Float, offsetof(cd::asset::PrimitiveVertex, normal) },
        cd::rhi::VertexAttribute { 2, 0, cd::rhi::Format::kRG32Float,  offsetof(cd::asset::PrimitiveVertex, uv)     },
        cd::rhi::VertexAttribute { 3, 0, cd::rhi::Format::kRGB32Float, offsetof(cd::asset::PrimitiveVertex, color)  }
    };
    constexpr std::array<cd::rhi::PushConstantRange, 1> kPrimPushRange {
        cd::rhi::PushConstantRange { .stages = cd::rhi::ShaderStage::kVertex | cd::rhi::ShaderStage::kFragment,
                                    .offset = 0,
                                    .size = static_cast<std::uint32_t>(sizeof(PrimPush)) }
    };
    // Faz 1.6 CSM + Faz 1.7 inline RT - three descriptor bindings on
    // the prim pipeline:
    //   0: UBO  with the sun's light_vp matrix (vertex + fragment).
    //   1: sampler2D over the shadow depth map (fragment only).
    //   2: scene TLAS (acceleration structure) for ray queries
    //      against the punctual / spot / area lights' shadow tests.
    // Faz 1.7 requires ray_query device support - gated below before
    // we attempt prim_material creation. Without it the shader's
    // `#extension GL_EXT_ray_query : require` would fail to compile.
    if (!device.features().ray_query)
    {
        std::fprintf(
            stderr,
            "hello_engine: device lacks VK_KHR_ray_query; "
            "Faz 1.7 inline RT shadows require it. "
            "Re-run on RT-capable hardware or git-checkout f04b588 "
            "(pre-1.7 CSM-only ship).\n"
        );
        return 9;
    }
    constexpr std::array<cd::rhi::DescriptorSetLayoutBinding, 11> kPrimDescBindings {
        cd::rhi::DescriptorSetLayoutBinding { .binding = 0,
                                             .type = cd::rhi::DescriptorType::kUniformBuffer,
                                             .count = 1,
                                             .stages =
                                                  cd::rhi::ShaderStage::kVertex | cd::rhi::ShaderStage::kFragment },
        cd::rhi::DescriptorSetLayoutBinding { .binding = 1,
                                             .type = cd::rhi::DescriptorType::kCombinedImageSampler,
                                             .count = 1,
                                             .stages = cd::rhi::ShaderStage::kFragment                            },
        cd::rhi::DescriptorSetLayoutBinding { .binding = 2,
                                             .type = cd::rhi::DescriptorType::kAccelerationStructure,
                                             .count = 1,
                                             .stages = cd::rhi::ShaderStage::kFragment                            },
        cd::rhi::DescriptorSetLayoutBinding { .binding = 3,
                                             .type = cd::rhi::DescriptorType::kUniformBuffer,
                                             .count = 1,
                                             .stages = cd::rhi::ShaderStage::kFragment                            },
        // gap #1/#13 - baseColor texture slot for glTF entities.
        cd::rhi::DescriptorSetLayoutBinding { .binding = 4,
                                             .type = cd::rhi::DescriptorType::kCombinedImageSampler,
                                             .count = 1,
                                             .stages = cd::rhi::ShaderStage::kFragment                            },
        // R2: IBL on the prim pipeline so textured kGltf entities
        // (CesiumMan, procedural Earth, torus knot) get reflections.
        cd::rhi::DescriptorSetLayoutBinding { .binding = 5,
                                             .type = cd::rhi::DescriptorType::kCombinedImageSampler,
                                             .count = 1,
                                             .stages = cd::rhi::ShaderStage::kFragment                            },
        cd::rhi::DescriptorSetLayoutBinding { .binding = 6,
                                             .type = cd::rhi::DescriptorType::kCombinedImageSampler,
                                             .count = 1,
                                             .stages = cd::rhi::ShaderStage::kFragment                            },
        cd::rhi::DescriptorSetLayoutBinding { .binding = 7,
                                             .type = cd::rhi::DescriptorType::kCombinedImageSampler,
                                             .count = 1,
                                             .stages = cd::rhi::ShaderStage::kFragment                            },
        // R2: procedural normal map (tangent-space bump).
        cd::rhi::DescriptorSetLayoutBinding { .binding = 8,
                                             .type = cd::rhi::DescriptorType::kCombinedImageSampler,
                                             .count = 1,
                                             .stages = cd::rhi::ShaderStage::kFragment                            },
        // R2: metallic-roughness-AO map (glTF 2.0 packing).
        cd::rhi::DescriptorSetLayoutBinding { .binding = 9,
                                             .type = cd::rhi::DescriptorType::kCombinedImageSampler,
                                             .count = 1,
                                             .stages = cd::rhi::ShaderStage::kFragment                            },
        // W8-BC per-frame TLAS instance materials SSBO. Index matches
        // the push_inst order so the shader can look up the hit
        // instance's albedo via rayQueryGetIntersectionInstanceIdEXT.
        cd::rhi::DescriptorSetLayoutBinding { .binding = 10,
                                             .type = cd::rhi::DescriptorType::kStorageBuffer,
                                             .count = 1,
                                             .stages = cd::rhi::ShaderStage::kFragment                            }
    };
    cd::material::MaterialDesc prim_md {};
    prim_md.vertex_glsl = kPrimVS;
    prim_md.fragment_glsl = kPrimFS;
    prim_md.color_attachment_formats = kColorFmts;
    prim_md.depth_attachment_format = kDepthFormat;
    prim_md.vertex_bindings = kPrimBindings;
    prim_md.vertex_attributes = kPrimAttrs;
    prim_md.push_constants = kPrimPushRange;
    prim_md.descriptor_bindings = kPrimDescBindings;
    prim_md.raster.cull = cd::rhi::CullMode::kNone;
    prim_md.depth_stencil.depth_test = true;
    prim_md.depth_stencil.depth_write = true;
    prim_md.depth_stencil.depth_compare = cd::rhi::CompareOp::kLess;
    prim_md.name = "hello_engine/prim";
    auto prim_r = cd::material::Material::create(device, compiler.get(), prim_md);
    if (!prim_r.has_value())
    {
        std::fprintf(
            stderr,
            "hello_engine: prim_material create failed: %.*s\n",
            static_cast<int>(prim_r.error().message.size()),
            prim_r.error().message.data()
        );
        return 9;
    }
    auto& prim_material = *prim_r;

    // R3 phase 226 - Velocity-pass material. Re-draws each entity's
    // mesh with cd::velocity::kVelocityVS+kVelocityFS so the FS
    // writes (curr_uv - prev_uv) to gbuf_velocity. Push = 128 B
    // (prev_vp_model + curr_vp_model). Vertex layout matches kPrimBindings
    // (FS reads only the position attribute - colour/normal/UV are
    // ignored by the velocity shader).
    constexpr std::array<cd::rhi::Format, 1> kVelocityColorFmts { cd::rhi::Format::kRG16Float };
    constexpr std::array<cd::rhi::PushConstantRange, 1> kVelocityPushRange {
        cd::rhi::PushConstantRange { .stages = cd::rhi::ShaderStage::kVertex, .offset = 0, .size = 128U }
    };
    cd::material::MaterialDesc vel_md {};
    vel_md.vertex_glsl = std::string_view { cd::velocity::kVelocityVS };
    vel_md.fragment_glsl = std::string_view { cd::velocity::kVelocityFS };
    vel_md.color_attachment_formats = kVelocityColorFmts;
    vel_md.depth_attachment_format = kDepthFormat;
    vel_md.vertex_bindings = kPrimBindings;
    vel_md.vertex_attributes = kPrimAttrs;
    vel_md.push_constants = kVelocityPushRange;
    vel_md.raster.cull = cd::rhi::CullMode::kNone;
    vel_md.depth_stencil.depth_test = true;
    vel_md.depth_stencil.depth_write = false;  // read-only depth
    vel_md.depth_stencil.depth_compare = cd::rhi::CompareOp::kLessEqual;
    vel_md.name = "hello_engine/velocity";
    auto vel_r = cd::material::Material::create(device, compiler.get(), vel_md);
    if (!vel_r.has_value())
    {
        std::fprintf(
            stderr,
            "hello_engine: velocity_material create failed: %.*s\n",
            static_cast<int>(vel_r.error().message.size()),
            vel_r.error().message.data()
        );
        return 52;
    }
    [[maybe_unused]] auto& velocity_material = *vel_r;

    // Shadow material (Faz 1.6 CSM) - depth-only pipeline (no color
    // attachment) with a trivial mat4 push constant. Used in the
    // shadow pass to rasterize every caster from the sun's POV.
    constexpr std::array<cd::rhi::PushConstantRange, 1> kShadowPushRange {
        cd::rhi::PushConstantRange { .stages = cd::rhi::ShaderStage::kVertex,
                                    .offset = 0,
                                    .size = static_cast<std::uint32_t>(sizeof(cd::math::Mat4f)) }
    };
    cd::material::MaterialDesc shadow_md {};
    shadow_md.vertex_glsl = kShadowVS;
    shadow_md.fragment_glsl = kShadowFS;
    shadow_md.color_attachment_formats = {};  // depth-only
    shadow_md.depth_attachment_format = kDepthFormat;
    shadow_md.vertex_bindings = kPrimBindings;
    shadow_md.vertex_attributes = kPrimAttrs;
    shadow_md.push_constants = kShadowPushRange;
    // Back-face culling for casters reduces shadow acne on the back
    // side of each mesh by ~50%. depth_bias_enable + slope pushes
    // shadow depth slightly away from the caster surface (Persson's
    // shadow-acne mitigation pattern).
    shadow_md.raster.cull = cd::rhi::CullMode::kBack;
    shadow_md.raster.depth_bias_enable = true;
    shadow_md.raster.depth_bias_constant = 1.25F;
    shadow_md.raster.depth_bias_slope = 1.75F;
    shadow_md.depth_stencil.depth_test = true;
    shadow_md.depth_stencil.depth_write = true;
    shadow_md.depth_stencil.depth_compare = cd::rhi::CompareOp::kLess;
    shadow_md.name = "hello_engine/shadow";
    auto shadow_r = cd::material::Material::create(device, compiler.get(), shadow_md);
    if (!shadow_r.has_value())
        return 10;
    auto& shadow_material = *shadow_r;

    // ---- Shadow-map resources (Faz 1.6 CSM) ----
    // 2K depth texture + sampler + UBO holding light_vp. The
    // MaterialInstance below points the prim pipeline at all three.
    constexpr cd::rhi::Extent2D kShadowMapSize { 2048, 2048 };
    DepthTarget shadow_target {};
    if (!create_depth_target(device, kShadowMapSize, kDepthFormat, shadow_target, cd::rhi::TextureUsage::kSampled))
        return 11;
    bool shadow_initialised_on_gpu = false;

    cd::rhi::SamplerDesc shadow_sd {};
    shadow_sd.mag_filter = cd::rhi::SamplerFilter::kLinear;
    shadow_sd.min_filter = cd::rhi::SamplerFilter::kLinear;
    shadow_sd.mipmap_mode = cd::rhi::SamplerMipmapMode::kNearest;
    shadow_sd.address_u = cd::rhi::SamplerAddressMode::kClampToBorder;
    shadow_sd.address_v = cd::rhi::SamplerAddressMode::kClampToBorder;
    shadow_sd.address_w = cd::rhi::SamplerAddressMode::kClampToBorder;
    shadow_sd.border_color = cd::rhi::BorderColor::kFloatOpaqueWhite;  // 1.0 depth = no shadow
    shadow_sd.max_lod = 1.0F;
    auto shadow_samp_r = device.create_sampler(shadow_sd);
    if (!shadow_samp_r.has_value())
        return 12;
    const auto shadow_sampler = *shadow_samp_r;

    cd::rhi::BufferDesc shadow_ubo_desc {};
    shadow_ubo_desc.size = sizeof(cd::math::Mat4f);  // 64 bytes
    shadow_ubo_desc.usage = cd::rhi::BufferUsage::kUniform;
    shadow_ubo_desc.memory = cd::rhi::MemoryUsage::kCpuToGpu;
    auto shadow_ubo_r = device.create_buffer(shadow_ubo_desc);
    if (!shadow_ubo_r.has_value())
        return 13;
    const auto shadow_ubo = *shadow_ubo_r;

    // ---- Multi-light UBO (gap #2 + #3 foundation) ----
    // 8 non-sun lights * 64 bytes per slot + 16-byte header = 528 B.
    // std140 layout: each vec4 = 16-byte aligned.
    //   header: uint count + 3 uint pad
    //   slot:   vec4 pos_range
    //           vec4 dir_type     (xyz=dir for spot/dir / right-basis for area; w=type as float)
    //           vec4 color_int    (xyz=linear colour, w=intensity)
    //           vec4 extras       (x=cos_outer for spot, y=area_w, z=area_h, w=cos_inner)
    constexpr std::uint32_t kMaxLights = cd::hello_engine::kMaxLights;
    constexpr std::uint32_t kLightSlotBytes = 80;                                // W8-N: added tangent vec4
    constexpr std::uint32_t kLightUboBytes = 16 + kMaxLights * kLightSlotBytes;  // 656
    cd::rhi::BufferDesc lights_ubo_desc {};
    lights_ubo_desc.size = kLightUboBytes;
    lights_ubo_desc.usage = cd::rhi::BufferUsage::kUniform;
    lights_ubo_desc.memory = cd::rhi::MemoryUsage::kCpuToGpu;
    auto lights_ubo_r = device.create_buffer(lights_ubo_desc);
    if (!lights_ubo_r.has_value())
        return 16;
    const auto lights_ubo = *lights_ubo_r;

    // W8-BC per-frame TLAS-instance material table (SSBO, binding 10).
    // 32 B per instance: vec4 albedo + vec4 emissive. 256-slot
    // headroom comfortably covers the ECS entities (<40) + 25 PBR
    // spheres + floor + future probes without ever needing a resize.
    // Filled host-side in the same loop that pushes TLAS instances,
    // so the GPU index from rayQueryGetIntersectionInstanceIdEXT
    // lines up 1:1 with cd_instance_mats.data[i].
    // Phase 293 / Marathon Run 7 sub-N1E: InstanceMatGpu + kMaxInstMats +
    // kInstMatBytes layouts moved to HelloRayQuery.hpp.
    using cd::hello_engine::InstanceMatGpu;
    using cd::hello_engine::kMaxInstMats;
    using cd::hello_engine::kInstMatBytes;
    cd::rhi::BufferDesc inst_mat_desc {};
    inst_mat_desc.size = kInstMatBytes;
    inst_mat_desc.usage = cd::rhi::BufferUsage::kStorage | cd::rhi::BufferUsage::kTransferDst;
    inst_mat_desc.memory = cd::rhi::MemoryUsage::kCpuToGpu;
    auto inst_mat_r = device.create_buffer(inst_mat_desc);
    if (!inst_mat_r.has_value())
        return 16;
    const auto inst_mat_ssbo = *inst_mat_r;

    // ---- R1: IBL bake + GPU upload ----
    // CPU-side bake at startup: analytical-sky env cube -> diffuse
    // irradiance + prefiltered specular + BRDF LUT. Vulkan upload
    // creates kCube/k2D textures + clamp-to-edge sampler.
    //
    // W8-AW: chrome-mirror quality bump. User asked for a polished
    // chrome look (Filament/UE5 reference). Old bake (env 128, spec
    // base 64 / 32 samples) produced a soft blue smudge for the
    // chrome sphere because:
    //   1) base 64 spec cube is pixelated at rough=0.04 (mip 0 lookup)
    //   2) 32 importance samples per texel = noisy / undersampled
    //   3) analytic sky has no high-frequency features (no sun disk
    //      visible IN the cube), so a mirror has nothing crisp to
    //      reflect.
    // Fix: env 256, spec base 256 / 1024 samples, diff 32 / 64 samples,
    // and inject an HDR sun disk into the sky bake at the default sun
    // direction so chrome catches a visible bright spot. Bake budget
    // climbs to ~5-8 s on a desktop CPU (one-shot at boot).
    constexpr cd::math::Vec3f kIblSunDirToward { 0.3F, 0.9F, 0.2F };  // -direction
    const float kIblSunLen = std::sqrt(
        kIblSunDirToward.x * kIblSunDirToward.x + kIblSunDirToward.y * kIblSunDirToward.y +
        kIblSunDirToward.z * kIblSunDirToward.z
    );
    const cd::math::Vec3f kIblSunUnit { kIblSunDirToward.x / kIblSunLen,
                                        kIblSunDirToward.y / kIblSunLen,
                                        kIblSunDirToward.z / kIblSunLen };
    // Phase 291 / Marathon Run 7 sub-N1C: the sky+sun-disk CPU sampler
    // lives in cd::material::sky_with_sun_cpu() now. A small lambda
    // binds the sample's kIblSunUnit direction for cd::ibl::bake_sky_cube,
    // which expects a unary functor `Vec3f(dir)`.
    auto bake_sky_with_sun = [&](cd::math::Vec3f dir) noexcept
    {
        return cd::material::sky_with_sun_cpu(dir, kIblSunUnit);
    };
    // W8-AW tuned: team-lead's original 256 base spec + 1024 samples +
    // 64 diff samples ran the CPU bake into the minutes (Windows
    // marked the process Not Responding, white client window).
    // Cap sample counts to a usable boot budget. Chrome rough=0.04
    // samples mip 0 sharply — sample count only affects mid-rough
    // mips that the chrome row doesn't use anyway. Sun disk + 256
    // env base preserved so the sharp mip-0 lookup has high-frequency
    // features to reflect.
    // X1C (phase 285): parallel boot bake graph. The CPU-bound IBL +
    // procedural Earth texture bakes share zero state (env-cube is the
    // only shared input, fed into diff + spec), so they run as a small
    // JobGraph on a boot-scoped WorkStealingThreadPool. GPU uploads
    // stay serial after the join because cd::rhi::IDevice is not
    // documented as thread-safe today (see ADR-20260528 X1-FU-* TODO
    // on upload_buffer thread safety).
    //
    // DAG:
    //   A env_cube -> { B diff_irradiance, C spec_prefilter }
    //   D brdf_lut, E earth_albedo, F earth_normal, G earth_mr
    //     (D-G are independent roots, share boot_pool with A)
    cd::ibl::CubeMapRgbF env_cube_cpu;
    cd::ibl::CubeMapRgbF diff_cube_cpu;
    cd::ibl::PrefilteredSpecularCube spec_cube_cpu;
    cd::ibl::BrdfLut brdf_lut_cpu;
    std::vector<std::uint8_t> earth_albedo_cpu;
    std::vector<std::uint8_t> earth_normal_cpu;
    std::vector<std::uint8_t> earth_mr_cpu;
    constexpr std::uint32_t kTexSize = 512;
    constexpr std::uint32_t kNormalSize = 512;
    constexpr std::uint32_t kMrSize = 256;
    std::fprintf(stderr, "[boot] dispatching parallel asset bake graph...\n");
    {
        cd::concurrency::WorkStealingThreadPool boot_pool { 0 };
        cd::concurrency::JobGraph boot_graph;
        const auto a = boot_graph.add(
            [&]
            {
                std::fprintf(stderr, "[ibl] baking environment cubemap (128, sun-disk)...\n");
                env_cube_cpu = cd::ibl::bake_sky_cube(128, bake_sky_with_sun);
            }
        );
        const auto b = boot_graph.add(
            [&]
            {
                std::fprintf(stderr, "[ibl] convolving diffuse irradiance (16, 16 samples)...\n");
                diff_cube_cpu = cd::ibl::convolve_irradiance(env_cube_cpu, 16, 16.0F);
            },
            { a }
        );
        const auto c = boot_graph.add(
            [&]
            {
                // W8-AY: spec base 128 / 32 samples -- see ADR-20260528.
                std::fprintf(stderr, "[ibl] prefiltering specular mip chain (128 base, 6 mips, 32 samples)...\n");
                spec_cube_cpu = cd::ibl::prefilter_specular(env_cube_cpu, 128, 6, 32);
            },
            { a }
        );
        const auto d = boot_graph.add(
            [&]
            {
                std::fprintf(stderr, "[ibl] baking BRDF LUT...\n");
                brdf_lut_cpu = cd::ibl::bake_brdf_lut(64, 64, 256);
            }
        );
        const auto e = boot_graph.add(
            [&]
            {
                earth_albedo_cpu = cd::texture_synth::bake_earth_albedo_rgba8(kTexSize);
            }
        );
        const auto fnode = boot_graph.add(
            [&]
            {
                earth_normal_cpu = cd::texture_synth::bake_earth_normal_rgba8(kNormalSize);
            }
        );
        const auto g = boot_graph.add(
            [&]
            {
                earth_mr_cpu = cd::texture_synth::bake_earth_mr_rgba8(kMrSize);
            }
        );
        (void)a;
        (void)b;
        (void)c;
        (void)d;
        (void)e;
        (void)fnode;
        (void)g;
        const bool ok = boot_graph.run(boot_pool);
        if (!ok || boot_graph.failed_nodes() != 0)
        {
            std::fprintf(
                stderr,
                "[boot] FATAL: bake graph run failed (ok=%d, failed_nodes=%llu)\n",
                ok ? 1 : 0,
                static_cast<unsigned long long>(boot_graph.failed_nodes())
            );
            return 23;
        }
        // boot_pool joins via dtor as we leave the scope.
    }
    std::fprintf(stderr, "[ibl] uploading to GPU...\n");
    const auto gpu_spec_cube = cd::ibl_gpu::upload_prefiltered_specular(device, spec_cube_cpu);
    const auto gpu_diff_cube = cd::ibl_gpu::upload_cubemap_rgba16f(device, diff_cube_cpu);
    const auto gpu_brdf_lut = cd::ibl_gpu::upload_brdf_lut(device, brdf_lut_cpu);
    std::fprintf(stderr, "[ibl] done (spec %u mips, diff 16, brdf 64x64)\n", gpu_spec_cube.mip_count);

    cd::rhi::SamplerDesc ibl_sd {};
    ibl_sd.mag_filter = cd::rhi::SamplerFilter::kLinear;
    ibl_sd.min_filter = cd::rhi::SamplerFilter::kLinear;
    ibl_sd.mipmap_mode = cd::rhi::SamplerMipmapMode::kLinear;
    ibl_sd.address_u = cd::rhi::SamplerAddressMode::kClampToEdge;
    ibl_sd.address_v = cd::rhi::SamplerAddressMode::kClampToEdge;
    ibl_sd.address_w = cd::rhi::SamplerAddressMode::kClampToEdge;
    ibl_sd.max_lod = static_cast<float>(gpu_spec_cube.mip_count);
    auto ibl_samp_r = device.create_sampler(ibl_sd);
    if (!ibl_samp_r.has_value())
        return 23;
    const auto ibl_sampler = *ibl_samp_r;

    // ---- glTF baseColor texture (#1/#13) ----
    // R1.5 showcase: procedural Earth-like albedo (CPU bake hoisted
    // into the X1C boot JobGraph above; this block consumes the
    // already-baked buffer and uploads it serially to the GPU). The
    // texture is replaced later if a glTF auto-load resolves an
    // asset with a baseColor map.
    GpuTexture2D albedo_tex {};
    bool has_gltf_texture = false;
    {
        albedo_tex = create_texture_rgba8(device, earth_albedo_cpu.data(), kTexSize, kTexSize);
        has_gltf_texture = true;
        std::fprintf(
            stderr,
            "[showcase] procedural Earth-like albedo "
            "(%ux%u) bound\n",
            kTexSize,
            kTexSize
        );
    }

    // R2: procedural normal map derived from a height field - same
    // fBm Earth surface but stored as tangent-space normals (X1C: CPU
    // bake hoisted to the boot graph).
    GpuTexture2D normal_tex {};
    {
        normal_tex = create_texture_rgba8(device, earth_normal_cpu.data(), kNormalSize, kNormalSize);
        std::fprintf(stderr, "[showcase] procedural normal map (%ux%u) bound\n", kNormalSize, kNormalSize);
    }

    // R2: metallic-roughness-AO map (glTF 2.0 packing - R unused,
    // G roughness, B metallic, A AO; X1C: CPU bake hoisted to the boot
    // graph).
    GpuTexture2D mr_tex {};
    {
        mr_tex = create_texture_rgba8(device, earth_mr_cpu.data(), kMrSize, kMrSize);
        std::fprintf(
            stderr,
            "[showcase] procedural metallic-roughness "
            "(%ux%u) bound\n",
            kMrSize,
            kMrSize
        );
    }
    cd::rhi::SamplerDesc albedo_sd {};
    albedo_sd.mag_filter = cd::rhi::SamplerFilter::kLinear;
    albedo_sd.min_filter = cd::rhi::SamplerFilter::kLinear;
    albedo_sd.mipmap_mode = cd::rhi::SamplerMipmapMode::kLinear;
    albedo_sd.address_u = cd::rhi::SamplerAddressMode::kRepeat;
    albedo_sd.address_v = cd::rhi::SamplerAddressMode::kRepeat;
    albedo_sd.address_w = cd::rhi::SamplerAddressMode::kRepeat;
    auto albedo_samp_r = device.create_sampler(albedo_sd);
    if (!albedo_samp_r.has_value())
        return 19;
    const auto albedo_sampler = *albedo_samp_r;

    auto prim_inst_r = cd::material::MaterialInstance::create(device, prim_material);
    if (!prim_inst_r.has_value())
        return 14;
    auto& prim_inst = *prim_inst_r;
    {
        std::array<cd::rhi::DescriptorWrite, 10> writes {
            cd::rhi::DescriptorWrite { .binding = 0,
                                      .array_element = 0,
                                      .type = cd::rhi::DescriptorType::kUniformBuffer,
                                      .buffer = shadow_ubo,
                                      .buffer_offset = 0,
                                      .buffer_range = sizeof(cd::math::Mat4f) },
            cd::rhi::DescriptorWrite { .binding = 1,
                                      .array_element = 0,
                                      .type = cd::rhi::DescriptorType::kCombinedImageSampler,
                                      .view = shadow_target.view,
                                      .sampler = shadow_sampler },
            cd::rhi::DescriptorWrite { .binding = 3,
                                      .array_element = 0,
                                      .type = cd::rhi::DescriptorType::kUniformBuffer,
                                      .buffer = lights_ubo,
                                      .buffer_offset = 0,
                                      .buffer_range = kLightUboBytes },
            cd::rhi::DescriptorWrite { .binding = 4,
                                      .array_element = 0,
                                      .type = cd::rhi::DescriptorType::kCombinedImageSampler,
                                      .view = albedo_tex.view,
                                      .sampler = albedo_sampler },
            // R2: IBL (prefiltered spec + diffuse irradiance + BRDF LUT)
            // shared with the PBR pipeline so textured prim entities
            // (CesiumMan, Earth showcase) get true reflections.
            cd::rhi::DescriptorWrite { .binding = 5,
                                      .array_element = 0,
                                      .type = cd::rhi::DescriptorType::kCombinedImageSampler,
                                      .view = gpu_spec_cube.view,
                                      .sampler = ibl_sampler },
            cd::rhi::DescriptorWrite { .binding = 6,
                                      .array_element = 0,
                                      .type = cd::rhi::DescriptorType::kCombinedImageSampler,
                                      .view = gpu_diff_cube.view,
                                      .sampler = ibl_sampler },
            cd::rhi::DescriptorWrite { .binding = 7,
                                      .array_element = 0,
                                      .type = cd::rhi::DescriptorType::kCombinedImageSampler,
                                      .view = gpu_brdf_lut.view,
                                      .sampler = ibl_sampler },
            // R2: procedural normal map for textured entities.
            cd::rhi::DescriptorWrite { .binding = 8,
                                      .array_element = 0,
                                      .type = cd::rhi::DescriptorType::kCombinedImageSampler,
                                      .view = normal_tex.view,
                                      .sampler = albedo_sampler },
            // R2: metallic-roughness-AO map.
            cd::rhi::DescriptorWrite { .binding = 9,
                                      .array_element = 0,
                                      .type = cd::rhi::DescriptorType::kCombinedImageSampler,
                                      .view = mr_tex.view,
                                      .sampler = albedo_sampler },
            // W8-BC: per-frame TLAS-instance material SSBO. The buffer
            // is then re-uploaded every frame inside the push_inst
            // loop; this initial write just points the descriptor at
            // the allocation so the layout is satisfied at first draw.
            cd::rhi::DescriptorWrite { .binding = 10,
                                      .array_element = 0,
                                      .type = cd::rhi::DescriptorType::kStorageBuffer,
                                      .buffer = inst_mat_ssbo,
                                      .buffer_offset = 0,
                                      .buffer_range = kInstMatBytes }
        };
        if (auto wr = prim_inst.update(writes); !wr.has_value())
            return 15;
    }

    // W8-AR: pbr_inst (StandardPbrMaterial descriptor set) REMOVED.
    // Multi-light UBO + IBL descriptors are bound on prim_inst already
    // (bindings 0, 5, 6, 7 on the unified prim pipeline).

    // R3: composite material instance + HDR sampler binding. Two
    // instances for TAA ping-pong - composite_insts[i] reads
    // history_targets[i] (= the OPPOSITE target from what it writes
    // this frame, so the read history was produced by the prior frame).
    std::array<cd::material::MaterialInstance, 2> composite_insts {};
    for (std::uint32_t i = 0; i < 2; ++i)
    {
        auto r = cd::material::MaterialInstance::create(device, composite_material);
        if (!r.has_value())
            return 33;
        composite_insts[i] = std::move(*r);
    }

    // R3 multi-mip bloom - physical mip chain + per-pass material instances.
    //
    // Allocation: 4 RGBA16F render targets at /2, /4, /8, /16 of the
    // HDR target's size. Each instance binds exactly one source mip
    // (or the HDR target for the prefilter inst).
    BloomMipChain bloom_chain {};
    if (!create_bloom_chain(device, { window.width(), window.height() }, bloom_chain))
        return 43;

    // 1 prefilter (reads HDR, writes mip0)
    // 3 downsample insts: 0??'1, 1??'2, 2??'3
    // 3 upsample insts:   3??'2 (additive), 2??'1 (additive), 1??'0 (additive)
    auto bp_inst_r = cd::material::MaterialInstance::create(device, bloom_prefilter_material);
    if (!bp_inst_r.has_value())
        return 44;
    auto& bloom_prefilter_inst = *bp_inst_r;

    std::array<cd::material::MaterialInstance, 3> bloom_down_insts {};
    for (std::uint32_t i = 0; i < 3; ++i)
    {
        auto r = cd::material::MaterialInstance::create(device, bloom_downsample_material);
        if (!r.has_value())
            return 45;
        bloom_down_insts[i] = std::move(*r);
    }
    std::array<cd::material::MaterialInstance, 3> bloom_up_insts {};
    for (std::uint32_t i = 0; i < 3; ++i)
    {
        auto r = cd::material::MaterialInstance::create(device, bloom_upsample_material);
        if (!r.has_value())
            return 46;
        bloom_up_insts[i] = std::move(*r);
    }

    // Wire descriptors. All sample with the linear-clamp albedo_sampler
    // (good enough - bloom doesn't need a mipmap-capable variant since
    // each pass writes mip 0 of its respective dedicated target).
    auto bind_bloom_descriptors = [&]()
    {
        auto write_one = [&](cd::material::MaterialInstance& inst, cd::rhi::TextureViewHandle src_view)
        {
            std::array<cd::rhi::DescriptorWrite, 1> w {
                cd::rhi::DescriptorWrite { .binding = 0,
                                          .array_element = 0,
                                          .type = cd::rhi::DescriptorType::kCombinedImageSampler,
                                          .view = src_view,
                                          .sampler = albedo_sampler }
            };
            (void)inst.update(w);
        };
        write_one(bloom_prefilter_inst, hdr_target.view);
        write_one(bloom_down_insts[0], bloom_chain.mips[0].view);
        write_one(bloom_down_insts[1], bloom_chain.mips[1].view);
        write_one(bloom_down_insts[2], bloom_chain.mips[2].view);
        write_one(bloom_up_insts[0], bloom_chain.mips[3].view);
        write_one(bloom_up_insts[1], bloom_chain.mips[2].view);
        write_one(bloom_up_insts[2], bloom_chain.mips[1].view);
    };
    bind_bloom_descriptors();

    auto bind_composite_hdr = [&]()
    {
        for (std::uint32_t i = 0; i < 2; ++i)
        {
            std::array<cd::rhi::DescriptorWrite, 6> writes {
                cd::rhi::DescriptorWrite { .binding = 0,
                                          .array_element = 0,
                                          .type = cd::rhi::DescriptorType::kCombinedImageSampler,
                                          .view = hdr_target.view,
                                          .sampler = albedo_sampler },
                cd::rhi::DescriptorWrite { .binding = 1,
                                          .array_element = 0,
                                          .type = cd::rhi::DescriptorType::kCombinedImageSampler,
                                          .view = bloom_chain.mips[0].view,
                                          .sampler = albedo_sampler },
                cd::rhi::DescriptorWrite { .binding = 2,
                                          .array_element = 0,
                                          .type = cd::rhi::DescriptorType::kCombinedImageSampler,
                                          .view = depth.view,
                                          .sampler = albedo_sampler },
                cd::rhi::DescriptorWrite { .binding = 3,
                                          .array_element = 0,
                                          .type = cd::rhi::DescriptorType::kCombinedImageSampler,
                                          .view = gbuf_normal.view,
                                          .sampler = albedo_sampler },
                // TAA history - composite_insts[i] reads history[i],
                // and per-frame logic picks composite_insts[frame & 1]
                // so the read history was written by the prior frame.
                cd::rhi::DescriptorWrite { .binding = 4,
                                          .array_element = 0,
                                          .type = cd::rhi::DescriptorType::kCombinedImageSampler,
                                          .view = history_targets[i].view,
                                          .sampler = albedo_sampler },
                // R3 phase 226 - velocity G-Buffer for per-mesh motion
                // blur + TAA reprojection.
                cd::rhi::DescriptorWrite { .binding = 5,
                                          .array_element = 0,
                                          .type = cd::rhi::DescriptorType::kCombinedImageSampler,
                                          .view = gbuf_velocity.view,
                                          .sampler = albedo_sampler }
            };
            (void)composite_insts[i].update(writes);
        }
    };
    bind_composite_hdr();

    // ---- Meshes (one PBR sphere, five primitive entities) ----
    auto cube_cpu = cd::asset::make_cube();
    // make_cube ships per-face axis-coloured (red/green/blue) and
    // make_sphere/cone/cyl/torus ship pos-based rainbow vertex
    // colours. Both patterns FIGHT the per-instance tint multiply
    // (v_albedo = in_color * pc.tint.rgb), producing a muddy wash
    // where every entity looks similar regardless of its tint. Flat-
    // ten EVERY primitive to white (1,1,1) so the entity tint shows
    // unmodified - closes the user-flagged 'proseduriel cisimlerin
    // renkleri ayni' regression.
    auto sphere_cpu_mut = cd::asset::make_sphere(18, 28);
    auto cone_cpu_mut = cd::asset::make_cone(32);
    auto cyl_cpu_mut = cd::asset::make_cylinder(32);
    auto torus_cpu_mut = cd::asset::make_torus(0.45F, 0.18F, 16, 24);
    auto flatten_white = [](auto& mesh)
    {
        for (auto& v : mesh.vertices)
        {
            v.color[0] = 1.0F;
            v.color[1] = 1.0F;
            v.color[2] = 1.0F;
        }
    };
    flatten_white(cube_cpu);
    flatten_white(sphere_cpu_mut);
    flatten_white(cone_cpu_mut);
    flatten_white(cyl_cpu_mut);
    flatten_white(torus_cpu_mut);
    const auto& sphere_cpu = sphere_cpu_mut;
    const auto& cone_cpu = cone_cpu_mut;
    const auto& cyl_cpu = cyl_cpu_mut;
    const auto& torus_cpu = torus_cpu_mut;
    // Floor quad - 1000 m ?- 1000 m centred at origin, normal +Y. The
    // size is far larger than the camera ever reaches; the FS
    // distance-fade (30 m -> 60 m) handles the apparent infinite-grid
    // feel. Faz 1.5: real geometry on which the planar shadow pass
    // can project caster silhouettes. Procedural shader-space grid
    // landing in v1.7 frame-graph rework replaces this with a single
    // fullscreen plane intersection.
    const auto floor_cpu = cd::asset::make_plane(1000.0F);

    GpuMesh cube_mesh = upload_mesh(device, cube_cpu);
    GpuMesh sphere_mesh = upload_mesh(device, sphere_cpu);
    GpuMesh cone_mesh = upload_mesh(device, cone_cpu);
    GpuMesh cyl_mesh = upload_mesh(device, cyl_cpu);
    GpuMesh torus_mesh = upload_mesh(device, torus_cpu);
    GpuMesh floor_mesh = upload_mesh(device, floor_cpu);
    // W8-AR: pbr_sphere REMOVED. PBR sphere entities reuse sphere_mesh.
    // R1.5: torus knot procedural showcase used when no glTF asset
    // resolves. With CesiumMan.glb in assets/samples/, the auto-load
    // path takes priority and uses the actual imported character.
    const auto knot_cpu = cd::asset::make_torus_knot(0.7F, 0.20F, 2, 3, 256, 24);
    GpuMesh knot_mesh = upload_mesh(device, knot_cpu);

    // ---- glTF auto-load ----
    // Try a small list of well-known sample paths so the user can drop
    // any Khronos sample (DamagedHelmet.gltf, FlightHelmet.gltf ???)
    // into ./assets/samples/ and have hello_engine pick it up on next
    // launch. Falls back gracefully if nothing is found.
    GpuMesh gltf_mesh {};
    std::string gltf_loaded_name;

    // SK4: skinned-mesh state captured at gltf load (CesiumMan-style
    // assets). When valid, the per-frame loop CPU-skins the source
    // vertices via cd::anim::compute_skinning_matrices + a 4-weight
    // LBS and re-uploads them to gltf_mesh.vb so the existing prim
    // pipeline draws the deformed character without needing a
    // separate skinned-vertex pipeline.
    struct SkinnedRuntime
    {
        bool valid { false };
        cd::anim::Skeleton skeleton {};
        std::unordered_map<int, std::int32_t> node_to_joint {};
        // SK-fix: vertex JOINTS_0 hold skin-joint indices, NOT
        // skeleton-joint indices. Skin joint i -> skeleton joint
        // skin_joint_remap[i]. Without this remap CPU-LBS reads the
        // wrong matrix and the character renders as a twisted mess.
        std::vector<std::int32_t> skin_joint_remap;
        cd::asset_gltf::GltfAnimation animation {};
        // Per-vertex source data (bind-pose positions + normals + uvs
        // + bone influences). Parallel arrays — same length.
        std::vector<cd::math::Vec3f> base_positions;
        std::vector<cd::math::Vec3f> base_normals;
        std::vector<cd::math::Vec2f> base_uvs;
        std::vector<cd::asset_gltf::GltfSkinVertex> influences;
        // Scratch buffers reused per frame.
        std::vector<cd::math::Mat4f> palette_scratch;
        std::vector<cd::asset::PrimitiveVertex> deformed_scratch;
        cd::anim::Pose pose {};
        float anim_t { 0.0F };
    };

    SkinnedRuntime skinned;
    {
        // Search list - try the binary's CWD first, then walk up the
        // build tree (binary lives at build/<preset>/bin/<config>/),
        // and finally try a few project-root anchors so the asset
        // resolves whether the user runs from project root or from
        // inside the binary directory.
        const std::array<std::string, 30> kCandidates {
            // Same-dir (rare but supports portable layout).
            "CesiumMan.glb",
            "model.gltf",
            // From project root.
            "assets/samples/CesiumMan.glb",
            "assets/samples/DamagedHelmet.glb",
            "assets/samples/FlightHelmet.gltf",
            "assets/samples/DamagedHelmet.gltf",
            "assets/samples/BoomBox.gltf",
            "assets/samples/Duck.gltf",
            "assets/samples/Suzanne.glb",
            "assets/samples/Fox.glb",
            "assets/samples/model.gltf",
            // From build/<preset>/bin/<config>/ - walk up to project root.
            "../../../../assets/samples/CesiumMan.glb",
            "../../../../assets/samples/DamagedHelmet.glb",
            "../../../../assets/samples/DamagedHelmet.gltf",
            "../../../../assets/samples/Duck.gltf",
            "../../../../assets/samples/Suzanne.glb",
            "../../../../assets/samples/Fox.glb",
            // From build/<preset>/ - one less up-level.
            "../../assets/samples/CesiumMan.glb",
            "../../assets/samples/DamagedHelmet.glb",
            "../../assets/samples/DamagedHelmet.gltf",
            // Absolute path probe (project-tree fixed install layout).
            "C:/UserFiles/Project/CHROMODYNAMIC_ENGINE/assets/samples/CesiumMan.glb",
            "C:/UserFiles/Project/CHROMODYNAMIC_ENGINE/assets/samples/DamagedHelmet.glb",
            // Misc.
            "",  // placeholders so size stays at 30
            "",
            "",
            "",
            "",
            "",
            "",
            ""
        };
        for (const auto& p : kCandidates)
        {
            if (p.empty())
                continue;
            auto loaded = cd::asset_gltf::load_gltf(p);
            if (!loaded.has_value())
                continue;
            // Merge every primitive of every mesh into one big
            // PrimitiveVertex buffer so we can render with the
            // existing prim pipeline. Texture sampling would need an
            // extra descriptor binding - deferred to the next ship.
            cd::asset::PrimitiveMesh merged;
            for (const auto& m : loaded->meshes)
            {
                for (const auto& prim : m.primitives)
                {
                    const auto base = static_cast<std::uint16_t>(merged.vertices.size());
                    for (const auto& v : prim.vertices)
                    {
                        cd::asset::PrimitiveVertex pv {};
                        pv.pos[0] = v.position.x;
                        pv.pos[1] = v.position.y;
                        pv.pos[2] = v.position.z;
                        pv.normal[0] = v.normal.x;
                        pv.normal[1] = v.normal.y;
                        pv.normal[2] = v.normal.z;
                        pv.uv[0] = v.texcoord0.x;
                        pv.uv[1] = v.texcoord0.y;
                        pv.color[0] = 0.85F;
                        pv.color[1] = 0.82F;
                        pv.color[2] = 0.78F;
                        merged.vertices.push_back(pv);
                    }
                    for (auto idx : prim.indices)
                    {
                        if (base + idx > 0xFFFFU)
                            continue;  // skip overflow (sample uses 16-bit IB)
                        merged.indices.push_back(static_cast<std::uint16_t>(base + idx));
                    }
                }
            }
            if (merged.vertices.empty() || merged.indices.empty())
            {
                std::fprintf(stderr, "[gltf] %s parsed but contained no renderable geometry\n", p.c_str());
                continue;
            }
            gltf_mesh = upload_mesh(device, merged);
            gltf_loaded_name = p;
            // SK4: capture skinning data if the asset has a skin AND
            // at least one animation. The CPU-skinning per-frame path
            // requires bind-pose positions/normals/uvs + bone IDs +
            // weights per vertex. We merge the FIRST primitive of the
            // FIRST mesh; CesiumMan and most glTF Khronos samples ship
            // a single primitive per mesh so this covers the common
            // case end-to-end.
            if (!loaded->skins.empty() && !loaded->animations.empty() && !loaded->meshes.empty() &&
                !loaded->meshes[0].primitives.empty() && !loaded->meshes[0].primitives[0].skin_vertices.empty())
            {
                auto bundle = cd::asset_gltf::to_skeleton_bundle(*loaded, 0);
                skinned.skeleton = std::move(bundle.skeleton);
                skinned.node_to_joint = std::move(bundle.node_to_joint);
                skinned.skin_joint_remap = std::move(bundle.skin_joint_remap);
                skinned.animation = loaded->animations[0];
                const auto& prim_src = loaded->meshes[0].primitives[0];
                skinned.base_positions.reserve(prim_src.vertices.size());
                skinned.base_normals.reserve(prim_src.vertices.size());
                skinned.base_uvs.reserve(prim_src.vertices.size());
                for (const auto& v : prim_src.vertices)
                {
                    skinned.base_positions.push_back(v.position);
                    skinned.base_normals.push_back(v.normal);
                    skinned.base_uvs.push_back(v.texcoord0);
                }
                skinned.influences = prim_src.skin_vertices;
                skinned.pose = cd::anim::Pose::bind_pose(skinned.skeleton);
                skinned.deformed_scratch.resize(skinned.base_positions.size());
                skinned.valid = true;
                std::fprintf(
                    stderr,
                    "[skin] %zu vertices, %zu joints, %zu anim channels\n",
                    skinned.base_positions.size(),
                    skinned.skeleton.joint_count(),
                    skinned.animation.channels.size()
                );
            }
            // gap #1/#13 - pull the first material's baseColor
            // texture out of the glTF and upload it to the prim
            // pipeline's binding 4 slot. Falls back silently if the
            // asset has no textures.
            if (!loaded->materials.empty() && !loaded->textures.empty())
            {
                const auto& mat = loaded->materials.front();
                const int tex_idx = mat.base_color_texture;
                if (tex_idx >= 0 && tex_idx < static_cast<int>(loaded->textures.size()))
                {
                    const auto& gt = loaded->textures[static_cast<std::size_t>(tex_idx)];
                    if (!gt.rgba.empty() && gt.width > 0 && gt.height > 0)
                    {
                        GpuTexture2D tex = create_texture_rgba8(device, gt.rgba.data(), gt.width, gt.height);
                        if (tex.image.is_valid())
                        {
                            // Replace the 1x1 white default.
                            if (albedo_tex.view.is_valid())
                                device.destroy_texture_view(albedo_tex.view);
                            if (albedo_tex.image.is_valid())
                                device.destroy_texture(albedo_tex.image);
                            albedo_tex = tex;
                            // Re-write descriptor binding 4 to point
                            // at the new glTF texture.
                            std::array<cd::rhi::DescriptorWrite, 1> tw {
                                cd::rhi::DescriptorWrite { .binding = 4,
                                                          .array_element = 0,
                                                          .type = cd::rhi::DescriptorType::kCombinedImageSampler,
                                                          .view = albedo_tex.view,
                                                          .sampler = albedo_sampler }
                            };
                            (void)prim_inst.update(tw);
                            has_gltf_texture = true;
                            std::fprintf(stderr, "[gltf] baseColor texture loaded (%ux%u)\n", gt.width, gt.height);
                        }
                    }
                }
            }
            std::fprintf(
                stderr,
                "[gltf] loaded %s - %zu verts, %zu indices (textured=%d)\n",
                p.c_str(),
                merged.vertices.size(),
                merged.indices.size(),
                static_cast<int>(has_gltf_texture)
            );
            break;
        }
        if (gltf_loaded_name.empty())
        {
            std::fprintf(
                stderr,
                "[gltf] no asset found; drop a .gltf into ./assets/samples/ "
                "(e.g. Khronos DamagedHelmet) and re-launch.\n"
            );
        }
    }

    auto mesh_for = [&](PrimitiveKind k) -> const GpuMesh&
    {
        switch (k)
        {
            case PrimitiveKind::kSphere:
                return sphere_mesh;
            case PrimitiveKind::kCone:
                return cone_mesh;
            case PrimitiveKind::kCylinder:
                return cyl_mesh;
            case PrimitiveKind::kTorus:
                return torus_mesh;
            case PrimitiveKind::kGltf:
                return gltf_mesh.vb.is_valid() ? gltf_mesh : knot_mesh;
            default:
                return cube_mesh;
        }
    };

    // ---- Faz 1.7 - per-mesh-kind BLAS ----
    // One BLAS per shape (cube / sphere / cone / cylinder / torus +
    // floor quad). Geometry is static, so we build these once at
    // boot and keep them for the lifetime of the program.
    auto build_blas = [&](const GpuMesh& m, std::string_view name) -> cd::rhi::AccelStructureHandle
    {
        cd::rhi::AccelTriangleGeometry tri {};
        tri.vertex_buffer = m.vb;
        tri.vertex_offset = 0;
        tri.vertex_count = m.vertex_count;
        tri.vertex_stride = sizeof(cd::asset::PrimitiveVertex);
        tri.index_buffer = m.ib;
        tri.index_offset = 0;
        tri.index_count = m.index_count;
        tri.index_type = cd::rhi::IndexType::kUInt16;
        std::array<cd::rhi::AccelTriangleGeometry, 1> tris { tri };
        cd::rhi::AccelStructureDesc bd {};
        bd.kind = cd::rhi::AccelStructureKind::kBottomLevel;
        bd.triangles = std::span<const cd::rhi::AccelTriangleGeometry>(tris);
        bd.debug_name = name;
        auto r = device.create_acceleration_structure(bd);
        return r.has_value() ? *r : cd::rhi::AccelStructureHandle {};
    };
    cd::rhi::AccelStructureHandle blas_cube = build_blas(cube_mesh, "blas_cube");
    cd::rhi::AccelStructureHandle blas_sphere = build_blas(sphere_mesh, "blas_sphere");
    cd::rhi::AccelStructureHandle blas_cone = build_blas(cone_mesh, "blas_cone");
    cd::rhi::AccelStructureHandle blas_cyl = build_blas(cyl_mesh, "blas_cyl");
    cd::rhi::AccelStructureHandle blas_torus = build_blas(torus_mesh, "blas_torus");
    cd::rhi::AccelStructureHandle blas_floor = build_blas(floor_mesh, "blas_floor");
    cd::rhi::AccelStructureHandle blas_gltf =
        gltf_mesh.vb.is_valid() ? build_blas(gltf_mesh, "blas_gltf") : cd::rhi::AccelStructureHandle {};
    auto blas_for_kind = [&](PrimitiveKind k) -> cd::rhi::AccelStructureHandle
    {
        switch (k)
        {
            case PrimitiveKind::kSphere:
                return blas_sphere;
            case PrimitiveKind::kCone:
                return blas_cone;
            case PrimitiveKind::kCylinder:
                return blas_cyl;
            case PrimitiveKind::kTorus:
                return blas_torus;
            case PrimitiveKind::kGltf:
                return blas_gltf;
            default:
                return blas_cube;
        }
    };
    // Build all BLAS on a one-shot cmd buffer. The renderer's
    // per-frame cmd buffers don't exist until begin_frame, so we
    // borrow a transient one for this boot operation.
    {
        auto bcmd_ptr = device.create_command_buffer();
        if (bcmd_ptr == nullptr)
            return 16;
        auto& bcmd = *bcmd_ptr;
        bcmd.begin();
        for (auto h : { blas_cube, blas_sphere, blas_cone, blas_cyl, blas_torus, blas_floor, blas_gltf })
            if (h.is_valid())
                bcmd.build_acceleration_structure(h);
        bcmd.end();
        cd::rhi::SubmitDesc bsd {};
        std::array<cd::rhi::ICommandBuffer*, 1> bcbs { &bcmd };
        bsd.command_buffers = bcbs;
        (void)device.submit(bsd);
        device.wait_idle();
    }

    // Per-frame TLAS scratch. `current_tlas` is what the descriptor
    // points at this frame; `tlas_destroy_queue` holds handles whose
    // destroy must wait until the renderer has cycled past the
    // submission that referenced them (frames_in_flight=2 ??' wait 3
    // frames as a defensive margin).
    cd::rhi::AccelStructureHandle current_tlas {};

    struct DeferredTlas
    {
        cd::rhi::AccelStructureHandle h;
        std::uint32_t destroy_at_frame;
    };

    std::deque<DeferredTlas> tlas_destroy_queue;

    // ---- World / Scene / EditHistory ----
    cd::ecs::World world;
    cd::scene::Scene scene { world };
    cd::editor::EditHistory history;
    std::deque<std::string> log;
    auto log_push = [&](std::string s)
    {
        log.emplace_back(std::move(s));
        while (log.size() > 64)
            log.pop_front();
    };

    // ---- World / Project / Level / Layer container (gap #18) ----
    // Phase 295 / Marathon Run 7 sub-N1G: assembly moved to
    // setup_world_container() in the anon namespace above. Passive
    // editor outliner backing - entities still live in the ECS scene;
    // the outliner just groups them under layers by name.
    cd::world_container::World cd_world;
    setup_world_container(cd_world);

    std::vector<SceneEntity> entities;
    {
        // Phase 295 / sub-N1G: primitive showcase row + gltf/earth +
        // PBR-grid spawn extracted to three named helpers above.
        spawn_primitive_seeds(scene, entities);
        // glTF entity - seeded when auto-loader resolves an asset, OR
        // (R1.5 showcase) a procedural Earth-like textured sphere.
        // Phase 295 / sub-N1G: assembly extracted to
        // spawn_gltf_or_earth_entity() above.
        spawn_gltf_or_earth_entity(scene, gltf_mesh, gltf_loaded_name, entities);
        // ---- W8-AR: 16 PBR sphere ECS entities (4x4 metallic/rough grid) ----
        // Phase 295 / sub-N1G: SceneEntity assembly extracted to
        // spawn_pbr_grid_entities() above; spawn data + grid math
        // live in HelloPbrGrid.hpp (Phase 294 / sub-N1F).
        spawn_pbr_grid_entities(scene, entities);
    }
    log_push(
        std::string { "[boot] " } + std::to_string(entities.size()) + " ECS entities spawned" +
        (gltf_loaded_name.empty() ? "" : std::string { " (incl. glTF: " } + gltf_loaded_name + ")")
    );
    int selected = 0;
    // Selection kind - entities and lights are both pickable.
    enum class SelKind : std::uint8_t
    {
        kEntity = 0,
        kLight = 1
    };
    SelKind selected_kind = SelKind::kEntity;

    // Phase 151 - selection-outline state. Style defaults to
    // kWireframe (the cheapest of the three documented techniques
    // and the one we draw as an ImGui foreground overlay below).
    cd::editor::SelectionOutline outline;
    outline.style = cd::editor::OutlineStyle::kWireframe;

    // Phase 152 - axis-translation gizmo state + UI bookkeeping.
    cd::editor::AxisGizmo gizmo;
    bool gizmo_visible = true;  // toggle via palette
    enum class GizmoMode : std::uint8_t
    {
        kTranslate = 0,
        kRotate = 1,
        kScale = 2
    };
    GizmoMode gizmo_mode = GizmoMode::kTranslate;
    ImVec2 gizmo_drag_anchor { 0, 0 };                            // screen-pixel mouse at begin_drag
    cd::math::Vec3f gizmo_drag_world_start {};                    // target position at begin_drag
    cd::math::Vec3f gizmo_drag_scale_start { 1.0F, 1.0F, 1.0F };  // scale at begin_drag
    cd::math::Quatf gizmo_drag_rot_start {};                      // rotation at begin_drag
    // Light-specific drag state (gaps #16 + #17): rotation drives
    // light.direction, scale drives light.range / area_width.
    cd::math::Vec3f light_drag_dir_start { 0.0F, -1.0F, 0.0F };
    // W8-N: capture area tangent at drag begin so the rotate gizmo
    // can rotate BOTH direction and tangent by the same quaternion,
    // keeping the rect's local +X axis in sync with the normal.
    cd::math::Vec3f light_drag_tangent_start { 1.0F, 0.0F, 0.0F };
    float light_drag_range_start { 0.0F };
    float light_drag_area_w_start { 1.0F };
    float light_drag_area_h_start { 1.0F };
    // Faz 1.5 UX fix - ray-plane projection initial hit on the
    // active axis at begin_drag. delta = current_axis_offset -
    // initial_axis_offset, robust against grazing-camera angles.
    // 'inf' marker = no valid initial hit, fall back to screen-space.
    float gizmo_drag_initial_offset = 0.0F;
    bool gizmo_drag_use_ray_plane = false;
    // Cross-frame: was the mouse on an axis arrow LAST frame? Used so
    // the pick path (which runs earlier in the frame than the gizmo
    // overlay) can suppress entity-pick when the user is starting a
    // gizmo drag. One-frame lag is invisible at 60+ FPS.
    bool gizmo_was_hovered = false;

    // ---- Camera + SceneCameraController (orbit) ----
    cd::camera::Camera cam {};
    cam.eye = { 0.0F, 2.5F, 8.0F };
    cam.target = { 0.0F, 0.5F, 0.0F };
    cam.fov_y = 0.9F;
    cam.near_z = 0.05F;
    cam.far_z = 200.0F;
    cd::scene::SceneCameraController scene_cam;
    scene_cam.attach(cam, scene, /*follow=*/ {});
    scene_cam.set_auto_spin(false);  // user-controlled by default; toggle from palette
    scene_cam.orbit().auto_spin_rate = 0.25F;

    // ---- Free-look camera state (WASD + right-mouse look + wheel zoom) ----
    // When the user holds the right mouse button, we disable auto-spin and
    // switch to FPS-style yaw/pitch from mouse delta + WASD translation.
    bool cam_right_drag = false;
    float cam_yaw = 0.0F;      // around +Y
    float cam_pitch = -0.15F;  // looking slightly down
    float cam_dist = 8.0F;     // distance from target (used as zoom)
    float last_mouse_x = 0.0F;
    float last_mouse_y = 0.0F;
    bool has_last_mouse = false;
    bool key_w = false, key_a = false, key_s = false, key_d = false;
    bool key_q = false, key_e = false;         // up/down
    bool key_shift = false, key_ctrl = false;  // W6-F speed modifiers
    constexpr float kCamMoveSpeed = 6.0F;      // m/s
    constexpr float kCamLookSpeed = 0.005F;    // rad/pixel

    // ---- Pick state (3D click-to-select) ----
    // Left click in the viewport casts a ray from the mouse pixel into
    // world space and tests against every entity's sphere bound.
    bool pending_pick = false;
    float pick_x = 0.0F, pick_y = 0.0F;

    // ---- Manual camera mode ----
    // Once the user touches WASD or right-mouse drag, the camera goes
    // into "manual mode" and scene_cam stops updating cam.eye/target -
    // otherwise the orbit camera snaps the eye back to its own pose on
    // every frame. Manual mode persists until palette "Camera: Toggle
    // Auto-Spin" is hit (which re-engages scene_cam orbit).
    bool cam_manual_mode = false;

    // ---- Audio chain (continuous tick) ----
    cd::audio::Mixer<2> audio_bus;
    audio_bus.set_gain(0, 0.6F);
    audio_bus.set_gain(1, 0.7F);
    cd::audio::Compressor comp;
    comp.prepare(
        static_cast<float>(kAudioSampleRate),
        /*threshold=*/0.40F,
        /*ratio=*/6.0F,
        /*attack=*/0.004F,
        /*release=*/0.080F
    );
    cd::audio::SimpleReverb reverb;
    reverb.prepare(kAudioSampleRate / 8);
    reverb.set_feedback(0.35F);
    cd::audio::LowPass lowpass;
    lowpass.prepare(static_cast<float>(kAudioSampleRate), /*cutoff=*/6500.0F);
    cd::audio::Limiter limiter;
    limiter.prepare(
        static_cast<float>(kAudioSampleRate),
        /*thresh=*/0.92F,
        /*attack=*/0.0002F,
        /*release=*/0.040F
    );
    std::uint64_t audio_t = 0;
    bool audio_muted = true;  // start muted; palette "Audio: Toggle Mute" opens it
    float audio_peak_window = 0.0F;
    float audio_comp_db_window = 0.0F;
    float audio_limiter_gain_min = 1.0F;
    std::deque<float> audio_meter_history;  // last ~120 ticks of peak

    // Phase 139 - last-5-seconds ring buffer of DSP chain output (s16
    // PCM). User clicks "Save WAV" in the Audio panel and the buffer
    // gets dumped to disk; play with any system audio player.
    constexpr std::size_t kAudioRingFrames = kAudioSampleRate * 5u;  // 5 s mono
    std::vector<std::int16_t> audio_ring(kAudioRingFrames, 0);
    std::size_t audio_ring_write = 0;
    std::uint64_t audio_total_written = 0;

    // Phase 139 v2 - WASAPI live playback. Pre-render 2 seconds of the
    // DSP chain at startup, create a looping clip, play. The visual
    // panel keeps ticking against the same DSP for an in-sync meter,
    // but the audible output is the pre-rendered loop (WASAPI clip
    // semantics don't expose continuous-stream push from sample code).
    // Without this the user heard nothing because the engine's audio
    // backend was never instantiated by hello_engine.
    auto audio_backend = cd::audio::make_wasapi_audio_backend();
    cd::audio::ClipHandle live_clip {};
    cd::audio::VoiceHandle live_voice {};
    bool audio_live_ok = (audio_backend != nullptr);
    if (audio_live_ok)
    {
        // Render 2 seconds of audio through the same DSP chain that
        // the on-screen meter walks every frame, then feed it to
        // WASAPI as a looping clip.
        constexpr std::size_t kPreRenderFrames = kAudioSampleRate * 2u;
        std::vector<float> live_buf(kPreRenderFrames, 0.0F);
        // Use a separate set of DSP nodes so the "live ticker" the
        // UI walks isn't pre-cooked by this render pass.
        cd::audio::Mixer<2> m2;
        m2.set_gain(0, 0.6F);
        m2.set_gain(1, 0.7F);
        cd::audio::Compressor c2;
        c2.prepare(static_cast<float>(kAudioSampleRate), 0.40F, 6.0F, 0.004F, 0.080F);
        cd::audio::SimpleReverb r2;
        r2.prepare(kAudioSampleRate / 8);
        r2.set_feedback(0.35F);
        cd::audio::LowPass l2;
        l2.prepare(static_cast<float>(kAudioSampleRate), 6500.0F);
        cd::audio::Limiter L2;
        L2.prepare(static_cast<float>(kAudioSampleRate), 0.92F, 0.0002F, 0.040F);
        for (std::size_t i = 0; i < kPreRenderFrames; ++i)
        {
            m2.mix(0, square_wave(i, 440.0F));
            m2.mix(1, burst_noise(i));
            float x = m2.pull();
            x = c2.process(x);
            const float wet = r2.process(x);
            x = 0.75F * x + 0.20F * wet;
            x = l2.process(x);
            x = L2.process(x);
            if (x > 1.0F)
                x = 1.0F;
            if (x < -1.0F)
                x = -1.0F;
            live_buf[i] = x * 0.7F;  // -3 dB headroom on output
        }
        cd::audio::ClipDesc cd_desc {};
        cd_desc.samples = std::span<const float>(live_buf);
        cd_desc.channels = 1;
        cd_desc.sample_rate = kAudioSampleRate;
        auto clip_r = audio_backend->create_clip(cd_desc);
        if (clip_r.has_value())
        {
            live_clip = *clip_r;
            // Start silent so the user doesn't get a sudden tone. The
            // "Audio: Toggle Mute" palette command unmutes to 0.65F.
            auto voice_r = audio_backend->play(live_clip, /*vol=*/0.0F, /*loop=*/true);
            if (voice_r.has_value())
                live_voice = *voice_r;
        }
    }

    // ---- Net sim (continuous tick) ----
    cd::net::Throttle net_throttle { /*cap=*/4.0F, /*rate=*/30.0F };
    cd::net::SnapshotBuffer<float> net_snapbuf;  // tiny scalar state for the demo
    cd::net::LatencyStats net_rtt;
    bool net_enabled = true;
    std::uint32_t net_sent = 0;
    std::uint32_t net_recv = 0;
    std::uint32_t net_drop = 0;
    std::uint64_t net_raw_bytes = 0;
    std::uint64_t net_wire_bytes = 0;
    cd::math::Random net_rng { 0xC0FFEE42u };
    float net_baseline = 0.0F;
    double net_t = 0.0;
    double next_net_tick = 0.0;

    // ---- cd::light demo (Phase 171/172) ----
    // 4 lights representing the four common light types. Each has a
    // CCT slider that drives the color via Krystek's CCT??'RGB; the
    // panel previews the resulting linear RGB.
    struct LightRow
    {
        std::string name;
        cd::light::Light light;
        bool enabled { true };
        float kelvin { 6500.0F };  // mirrors light.color_kelvin
    };

    // W8-AC: defaults — only the cyan ceiling rect-area is enabled on
    // boot. Earlier defaults had sun + point + spot + area all enabled
    // simultaneously; the user kept testing area light changes while
    // the warm point/spot contribution dominated the visible result,
    // so every area-light fix looked like a no-op even when it
    // worked. Forcing area-only on boot makes the area calibration
    // immediately verifiable; the user can re-check the other lights
    // via the Lights panel.
    // W8-BB: every light enabled by default. User saw the W8-BA
    // result with sun+point+spot+two area panels all on and called
    // it "cok iyi oldu bunu referans kabul edebiliriz" — that's
    // the boot baseline now. Individual rows can still be toggled
    // off in the Lights panel.
    std::vector<LightRow> lights;
    lights.push_back(
        { "Sun (cool 6500K)", cd::light::directional({ -0.35F, -0.65F, -0.7F }, { 1, 1, 1 }, 100000.0F), true, 6500.0F }
    );
    lights.push_back(
        { "Tungsten point (2700K)",
          cd::light::point({ 2.0F, 3.0F, -3.0F }, { 1, 1, 1 }, 3000.0F, 15.0F),
          true,
          2700.0F }
    );
    lights.push_back(
        { "Halogen spot (3200K)",
          // W8-I: spot pulled CLOSER to the sphere grid + bumped lumens.
          // W8-H placement (0, 5, 4) was camera-side (good for hitting
          // the +Z face of spheres) but ~10 m away — inverse-square
          // attenuation crushed the per-pixel contribution to ~0.01,
          // visibly black after tonemap. Moving to (0, 5, 0) brings
          // distance down to ~4.7 m and lumens 1800 -> 6000 raises the
          // peak so the spot reads as a real flashlight on the grid.
          // From (0, 5, 0) aim at grid centre (0, 3.5, -4.5):
          // dir = (0, -0.316, -0.949) (normalised).
          cd::light::spot({ 0.0F, 5.0F, 0.0F }, { 0.0F, -0.316F, -0.949F }, { 1, 1, 1 }, 6000.0F, 20.0F, 0.35F, 0.55F),
          true,
          3200.0F }
    );
    lights.push_back(
        { "Cyan rect-area (8000K)",
          // W8-Z: default to a CEILING PANEL (normal pointing straight
          // down) so the floor is in the +N hemisphere and gets lit out
          // of the box. With a downward-facing area light + W8-W shadow
          // bias, the character casts a visible floor shadow without
          // the user having to rotate the gizmo at all. Position raised
          // slightly to clear the camera framing.
          // W8-AI: restore the OLDER W5-B-era cyan rect placement —
          // upright panel at front of the scene facing the sphere
          // column (z=2, normal pointing -Z). User explicitly said the
          // dual cyan+magenta tone where each side of the sphere column
          // picks up a different colour was the "calisiyor" reference
          // they remembered. Ceiling default (W8-Z) was the wrong
          // direction for their mental model.
          cd::light::
              rect_area({ 0.0F, 4.5F, 2.0F }, { 0, 0, -1 }, { 1, 0, 0 }, 3.0F, 1.0F, { 0.6F, 0.85F, 1.0F }, 2500.0F),
          true,
          8000.0F }
    );
    // W5-B: very bright magenta neon strip behind the sphere rig so
    // metallic surfaces pick up a deeply saturated, HIGHLY DYNAMIC
    // highlight. This is the showcase scene that reveals tonemap
    // operator differences (Narkowicz crushes the magenta, Hable
    // rolls it off, AGX preserves the chroma gradient toward the
    // peak). Without an emitter that exceeds the SDR ceiling the
    // tonemap palette has nothing distinctive to compress.
    lights.push_back(
        { "Magenta HDR neon (25000K)",
          cd::light::
              rect_area({ 0.0F, 1.8F, -7.5F }, { 0, 0, 1 }, { 1, 0, 0 }, 4.0F, 0.4F, { 1.0F, 0.18F, 0.85F }, 6000.0F),
          true,
          25000.0F }
    );
    // W8-AH: magenta re-enabled by default alongside cyan so the
    // user sees the dual blue+magenta scene they remembered as
    // "working" (before W8-AC isolated just the cyan ceiling).
    // W8-AB: magenta neon lumens 25000 -> 6000. The 25000 lm value
    // was chosen during W5-B when the area multiplier was 0.20 —
    // visible peak under that calibration. After W8-AB unified
    // area calibration, 25000 lm produced a saturated white blob
    // on the sphere column. 6000 lm keeps the HDR-showcase intent
    // (warmer-than-white tint, exceeds SDR ceiling on closest
    // metal spheres) without overwhelming the rest of the scene.

    // Per-frame ClusterGrid for stats. View-space Z range here is just
    // for the panel's "lights per cluster" preview.
    cd::light::ClusterGrid cluster_grid;
    cd::light::ClusterGridDesc cluster_desc;
    cluster_desc.tiles_x = 8;
    cluster_desc.tiles_y = 4;
    cluster_desc.slices_z = 8;
    cluster_desc.near_z = 0.1F;
    cluster_desc.far_z = 100.0F;
    cluster_grid.configure(cluster_desc);

    // ---- AsyncStreamer demo (Phase 150) ----
    // Drives a background worker thread that processes simulated load
    // requests with a sleep so the streamer panel can show pending ??'
    // in-flight ??' complete transitions in real time.
    std::atomic<std::uint32_t> streamer_completed { 0 };
    std::atomic<std::uint32_t> streamer_failed { 0 };
    cd::asset::AsyncStreamer streamer { [&streamer_completed, &streamer_failed](cd::asset::AssetId id) -> bool
                                        {
                                            const auto v = id.value();
                                            std::this_thread::sleep_for(std::chrono::milliseconds(120 + (v % 5) * 80));
                                            const bool ok = (v % 17 != 0);
                                            if (ok)
                                                streamer_completed.fetch_add(1, std::memory_order_relaxed);
                                            else
                                                streamer_failed.fetch_add(1, std::memory_order_relaxed);
                                            return ok;
                                        } };
    streamer.start();
    std::vector<cd::asset::AssetId> streamer_tracked;
    std::uint64_t streamer_next_id = 1;
    auto streamer_enqueue = [&](std::int32_t priority)
    {
        cd::asset::AssetId id { streamer_next_id++ };
        cd::asset::StreamRequest req;
        req.id = id;
        req.priority = priority;
        streamer.enqueue(req);
        streamer_tracked.push_back(id);
        if (streamer_tracked.size() > 32)
            streamer_tracked.erase(streamer_tracked.begin(), streamer_tracked.begin() + 8);
    };

    // ---- Random viz ----
    cd::math::Random rand_rng { 0xA1B2C3D4u };
    Histogram hist_uniform;
    Histogram hist_normal;
    auto rebuild_random_viz = [&]()
    {
        std::vector<float> u;
        u.reserve(8192);
        for (int i = 0; i < 8192; ++i)
            u.push_back(rand_rng.next_float());
        hist_uniform.rebuild(u, 0.0F, 1.0F, 24);
        std::vector<float> n;
        n.reserve(8192);
        bool have_cached = false;
        float cached = 0.0F;
        for (int i = 0; i < 8192; ++i)
        {
            if (have_cached)
            {
                have_cached = false;
                n.push_back(cached);
                continue;
            }
            float u1 = rand_rng.next_float();
            if (u1 < 1e-7F)
                u1 = 1e-7F;
            const float u2 = rand_rng.next_float();
            const float r = std::sqrt(-2.0F * std::log(u1));
            const float t = 6.28318530717958F * u2;
            cached = r * std::sin(t);
            have_cached = true;
            n.push_back(r * std::cos(t));
        }
        hist_normal.rebuild(n, -3.0F, 3.0F, 24);
    };
    rebuild_random_viz();
    std::uint64_t next_random_refresh = 0;

    // ---- Counter table (engine self-stats) ----
    cd::core::CounterTable counters;

    // ---- Command palette ----
    cd::editor::CommandPalette palette;
    bool palette_visible = false;
    std::string palette_query;
    // FX state - runtime-tweakable, pushed into PrimPush::fx_params
    // each draw. tonemap_op: 0=Narkowicz, 1=Hill, 2=Hable, 3=AGX.
    // Default = Hable (Uncharted 2). AGX desaturates the LDR-range
    // shading the sample produces; Hable preserves tints on the
    // front primitives + back metallic spheres. AGX still wins on
    // HDR-heavy frames - switch via palette ('Tonemap: AGX').
    int tonemap_op = 2;  // 0=Narkowicz 1=Hill 2=Hable 3=AGX 4=HDR10 PQ
    palette.register_command(
        70,
        "Tonemap: AGX (Sobotka 2022)",
        [&]
        {
            tonemap_op = 3;
            log_push("[fx] tonemap = AGX");
        }
    );
    palette.register_command(
        71,
        "Tonemap: Hill ACES (Filament fit)",
        [&]
        {
            tonemap_op = 1;
            log_push("[fx] tonemap = Hill ACES");
        }
    );
    palette.register_command(
        72,
        "Tonemap: Hable / Uncharted 2",
        [&]
        {
            tonemap_op = 2;
            log_push("[fx] tonemap = Hable");
        }
    );
    palette.register_command(
        73,
        "Tonemap: Narkowicz ACES",
        [&]
        {
            tonemap_op = 0;
            log_push("[fx] tonemap = Narkowicz");
        }
    );
    palette.register_command(
        74,
        "Tonemap: HDR10 PQ (ST.2084, Rec.2020)",
        [&]
        {
            tonemap_op = 4;
            log_push("[fx] tonemap = HDR10 PQ (use only on HDR display)");
        }
    );
    // v1.4 day-ship FX wire-in. The post_gtao / post_bloom / post_ssr
    // libraries are linked (CMakeLists) and their Settings structs
    // are reachable; the multi-pass GPU dispatch lands in v1.7
    // frame-graph rework. Until then, the prim FS runs cheap inline
    // approximations gated by fx_params.z (GTAO crease darkening)
    // and fx_params.w (highlight bloom). The library settings live
    // here so the editor UI work in v1.6 can bind sliders straight
    // to these without renaming.
    cd::post_gtao::Settings fx_gtao {};
    cd::post_bloom::Settings fx_bloom {};
    cd::post_ssr::Settings fx_ssr {};
    cd::post_dof::CameraSettings fx_dof {};
    cd::post_motion_blur::Settings fx_mblur {};
    cd::post_taa::Settings fx_taa {};
    cd::post_smaa::Settings fx_smaa {};
    float fx_gtao_strength = 0.0F;  // 0 = off
    float fx_bloom_strength = 0.0F;
    float fx_smaa_strength = 0.0F;
    float fx_motion_blur = 0.0F;         // LIVE: composite camera-velocity (phase 215)
    float fx_taa_amount = 0.0F;          // LIVE: composite TAA ping-pong (phase 216-217)
    float fx_dof_strength = 0.0F;        // wired to composite (phase207)
    float fx_vignette_strength = 0.25F;  // soft default - readable cinematic edge
    float fx_film_grain = 0.0F;          // 0 = off; 0.5 = visible filmic noise
    float fx_chromab_strength = 0.0F;    // 0 = off; 0.5 = subtle radial RGB split
    // Composite tonemap/HDR knobs - own the entire post-fx settle here.
    float fx_exposure = 3.0F;           // pre-tonemap exposure boost
    float fx_saturation_boost = 1.50F;  // post-tonemap saturation pull-away
    float fx_bloom_post = 0.04F;        // bloom mip0 contribution mixed into HDR
    float fx_ao_strength = 0.55F;       // composite AO crease darkening
    // W4-E: bumped default 0.35 -> 0.75 so light shafts are obviously
    // visible on first run. User reported they were hard to read at the
    // previous default.
    float fx_shafts_strength = 0.75F;  // light shafts radial intensity
    float fx_ssr_strength = 0.5F;      // SSR reflection contribution (default on)

    // Previous-frame camera basis snapshot - populated AFTER each
    // composite invoke so the next frame's reprojection sees t-1.
    // First frame: prev = current (zero velocity).
    struct PrevCamBasis
    {
        cd::math::Vec3f right { 1.0F, 0.0F, 0.0F };
        cd::math::Vec3f up { 0.0F, 1.0F, 0.0F };
        cd::math::Vec3f fwd { 0.0F, 0.0F, -1.0F };
        cd::math::Vec3f pos { 0.0F, 0.0F, 0.0F };
        float half_w { 1.0F };
        float half_h { 1.0F };
        bool valid { false };
    } prev_cam_basis {};

    // R3 phase 227 - prev frame's UN-JITTERED VP matrix for the velocity
    // pass. UN-jittered so jitter doesn't pollute the velocity output.
    cd::math::Mat4f prev_vp_unjittered = cd::math::Mat4f::identity();
    bool prev_vp_valid = false;
    (void)prev_vp_valid;

    // TAA history target state - both start kUndefined and we cycle
    // them through ColorAttachment ??" ShaderResource as composite
    // ping-pongs which one it reads vs writes per frame.
    std::array<cd::rhi::ResourceState, 2> history_states { cd::rhi::ResourceState::kUndefined,
                                                           cd::rhi::ResourceState::kUndefined };
    bool fx_hdr10_request = false;  // queued for swapchain-output rework
    float fx_fog_density = 0.0F;
    float fx_aerial_perspective = 0.0F;
    float fx_clouds_coverage = 0.0F;  // queued - needs 3D Worley/Perlin noise tex
    float fx_light_shafts = 0.0F;     // LIVE in composite (phase 208) - legacy var kept
    cd::atmosphere::Parameters fx_atmosphere {};
    cd::light_shafts::Settings fx_lshafts {};
    cd::volumetric_clouds::Settings fx_clouds {};
    cd::volumetric_fog::GridConfig fx_vfog {};
    (void)fx_atmosphere;
    (void)fx_lshafts;
    (void)fx_clouds;
    (void)fx_vfog;
    // Advanced BRDF wire-in (queued for v1.7 material-system rework).
    // Settings live here so the editor UI can attach immediately when
    // the dispatch lands. Each toggle logs queue status.
    float fx_ltc_ggx_strength = 0.0F;
    float fx_sheen_strength = 0.0F;
    float fx_clearcoat_strength = 0.0F;
    float fx_sss_strength = 0.0F;
    // Debug view modes: 0 final, 1 albedo, 2 world normal, 3 MR map,
    // 4 AO, 5 normal-mapped surface normal, 6 vertex UVs.
    int fx_view_mode = 0;
    float fx_decal_count = 0.0F;         // count placeholder
    float fx_particle_emit_rate = 0.0F;  // /sec placeholder
    palette.register_command(
        100,
        "BRDF: Toggle LTC-GGX area-light specular (queued v1.7)",
        [&]
        {
            fx_ltc_ggx_strength = (fx_ltc_ggx_strength > 0.001F) ? 0.0F : 1.0F;
            log_push(
                fx_ltc_ggx_strength > 0.001F ? "[brdf] LTC-GGX queued (v1.7 material rework)" : "[brdf] LTC-GGX off"
            );
        }
    );
    palette.register_command(
        101,
        "BRDF: Toggle Sheen (queued v1.7)",
        [&]
        {
            fx_sheen_strength = (fx_sheen_strength > 0.001F) ? 0.0F : 0.5F;
            log_push(fx_sheen_strength > 0.001F ? "[brdf] Sheen queued (v1.7 material rework)" : "[brdf] Sheen off");
        }
    );
    palette.register_command(
        102,
        "BRDF: Toggle Clearcoat (queued v1.7)",
        [&]
        {
            fx_clearcoat_strength = (fx_clearcoat_strength > 0.001F) ? 0.0F : 0.6F;
            log_push(
                fx_clearcoat_strength > 0.001F ? "[brdf] Clearcoat queued (v1.7 material rework)"
                                               : "[brdf] Clearcoat off"
            );
        }
    );
    palette.register_command(
        103,
        "BRDF: Toggle SSS / Burley diffusion (queued v1.7)",
        [&]
        {
            fx_sss_strength = (fx_sss_strength > 0.001F) ? 0.0F : 0.6F;
            log_push(fx_sss_strength > 0.001F ? "[brdf] SSS queued (v1.7 needs neighbourhood pass)" : "[brdf] SSS off");
        }
    );
    palette.register_command(
        104,
        "FX: Spawn Decal (queued v1.7)",
        [&]
        {
            fx_decal_count += 1.0F;
            log_push("[fx] Decal queued (v1.7 needs projector volume + GBuffer)");
        }
    );
    palette.register_command(
        105,
        "FX: Toggle GPU Particles 10k/sec (queued v1.7)",
        [&]
        {
            fx_particle_emit_rate = (fx_particle_emit_rate > 0.001F) ? 0.0F : 10000.0F;
            log_push(
                fx_particle_emit_rate > 0.001F ? "[fx] GPU particles queued (v1.7 needs compute pipe)"
                                               : "[fx] GPU particles off"
            );
        }
    );
    (void)fx_ltc_ggx_strength;
    (void)fx_sheen_strength;
    (void)fx_clearcoat_strength;
    (void)fx_sss_strength;
    (void)fx_decal_count;
    (void)fx_particle_emit_rate;
    // v1.5 GI wire-in (queued for v1.7 frame-graph + acceleration
    // structure dispatch). Settings + reservoirs instantiated so the
    // editor UI binds without renaming.
    cd::restir_di::Reservoir fx_restir_di_reservoir {};
    cd::restir_gi::Reservoir fx_restir_gi_reservoir {};
    cd::ddgi::GridConfig fx_ddgi_grid {};
    cd::nrc::Config fx_nrc_cfg {};
    (void)fx_restir_di_reservoir;
    (void)fx_restir_gi_reservoir;
    (void)fx_ddgi_grid;
    (void)fx_nrc_cfg;
    bool fx_restir_di_on = false;
    bool fx_restir_gi_on = false;
    bool fx_ddgi_on = false;
    bool fx_nrc_on = false;
    palette.register_command(
        110,
        "GI: Toggle ReSTIR DI (queued v1.7)",
        [&]
        {
            fx_restir_di_on = !fx_restir_di_on;
            log_push(fx_restir_di_on ? "[gi] ReSTIR DI queued (v1.7 needs RT compute pipe)" : "[gi] ReSTIR DI off");
        }
    );
    palette.register_command(
        111,
        "GI: Toggle ReSTIR GI (queued v1.7)",
        [&]
        {
            fx_restir_gi_on = !fx_restir_gi_on;
            log_push(fx_restir_gi_on ? "[gi] ReSTIR GI queued (v1.7 needs RT compute pipe)" : "[gi] ReSTIR GI off");
        }
    );
    palette.register_command(
        112,
        "GI: Toggle DDGI probe update (queued v1.7)",
        [&]
        {
            fx_ddgi_on = !fx_ddgi_on;
            log_push(fx_ddgi_on ? "[gi] DDGI queued (v1.7 needs probe-volume RT)" : "[gi] DDGI off");
        }
    );
    palette.register_command(
        113,
        "GI: Toggle NRC (TinyCudaNN backend, queued v1.7)",
        [&]
        {
            fx_nrc_on = !fx_nrc_on;
            log_push(fx_nrc_on ? "[gi] NRC queued (v1.7 needs CUDA inference path)" : "[gi] NRC off");
        }
    );
    palette.register_command(
        120,
        "RHI: Status (active backend + parity)",
        [&]
        {
            log_push("[rhi] active = Vulkan (production)");
            log_push("[rhi] D3D12 partial - PSO/desc/shader stubs (v1.8.1)");
            log_push("[rhi] OpenGL partial - no RT support (v1.8.2)");
            log_push("[rhi] Metal skeleton - Apple-only stub (v1.8.3)");
            log_push("[rhi] WebGPU not started (v1.8.4 via Dawn)");
            log_push("[rhi] see docs/RHI_PARITY_STATUS.md");
        }
    );
    palette.register_command(
        130,
        "Physics: Status (Jolt + cloth roadmap)",
        [&]
        {
            log_push("[phys] primitives: Aabb/Sphere/Capsule/Obb/Ray ready");
            log_push("[phys] IPhysicsWorld interface ready; Jolt impl v1.9.1");
            log_push("[phys] cloth (PBD) v1.9.3; character controller v1.9.4");
            log_push("[phys] see docs/PHYSICS_V19_PLAN.md");
        }
    );
    palette.register_command(
        131,
        "Script: Status (Lua 5.4 + AI BT roadmap)",
        [&]
        {
            log_push("[script] cd::script::Engine skeleton present");
            log_push("[script] Lua 5.4 integration v1.9.5");
            log_push("[script] behaviour-tree nodes v1.9.5");
        }
    );
    palette.register_command(
        140,
        "v2.0: Production Milestone Status",
        [&]
        {
            log_push("[v2.0] cooker (assetc):     v2.0.1 - pending");
            log_push("[v2.0] profiler:            cd::profile sinks ready (v2.0.2)");
            log_push("[v2.0] crash reporter:      cd::diag::CrashReporter ready (v2.0.3)");
            log_push("[v2.0] HRTF audio:          cd::audio core ready (v2.0.4)");
            log_push("[v2.0] i18n + a11y:         ICU integration pending (v2.0.5)");
            log_push("[v2.0] hot-reload:          vfs + shader Compiler ready (v2.0.6)");
            log_push("[v2.0] 24h stress harness:  pending CI hardware (v2.0.7)");
            log_push("[v2.0] ENGINE_GUIDE.md:     pending (v2.0.8)");
            log_push("[v2.0] see docs/PRODUCTION_V20_PLAN.md");
        }
    );
    (void)fx_restir_di_on;
    (void)fx_restir_gi_on;
    (void)fx_ddgi_on;
    (void)fx_nrc_on;
    palette.register_command(
        80,
        "FX: Toggle GTAO (inline approx)",
        [&]
        {
            fx_gtao_strength = (fx_gtao_strength > 0.001F) ? 0.0F : 0.65F;
            log_push(fx_gtao_strength > 0.001F ? "[fx] GTAO on" : "[fx] GTAO off");
        }
    );
    palette.register_command(
        81,
        "FX: Toggle Bloom (inline approx)",
        [&]
        {
            fx_bloom_strength = (fx_bloom_strength > 0.001F) ? 0.0F : 0.55F;
            log_push(fx_bloom_strength > 0.001F ? "[fx] Bloom on" : "[fx] Bloom off");
        }
    );
    palette.register_command(
        82,
        "FX: GTAO Settings (radius=1m, dirs=4)",
        [&]
        {
            fx_gtao.radius = 1.0F;
            fx_gtao.direction_count = 4;
            log_push("[fx] GTAO settings reset to defaults");
        }
    );
    palette.register_command(
        83,
        "FX: Bloom Settings (threshold=1.0, intensity=0.04)",
        [&]
        {
            fx_bloom.threshold = 1.0F;
            fx_bloom.intensity = 0.04F;
            log_push("[fx] Bloom settings reset to defaults");
        }
    );
    palette.register_command(
        84,
        "FX: Toggle SMAA (inline luma-edge blur)",
        [&]
        {
            fx_smaa_strength = (fx_smaa_strength > 0.001F) ? 0.0F : 0.55F;
            log_push(fx_smaa_strength > 0.001F ? "[fx] SMAA on" : "[fx] SMAA off");
        }
    );
    palette.register_command(
        85,
        "FX: Toggle Motion Blur",
        [&]
        {
            fx_motion_blur = (fx_motion_blur > 0.001F) ? 0.0F : 0.5F;
            log_push(
                fx_motion_blur > 0.001F ? "[fx] MotionBlur on (composite camera-velocity)" : "[fx] MotionBlur off"
            );
        }
    );
    palette.register_command(
        86,
        "FX: Toggle TAA",
        [&]
        {
            fx_taa_amount = (fx_taa_amount > 0.001F) ? 0.0F : 0.85F;
            log_push(fx_taa_amount > 0.001F ? "[fx] TAA on (history + Halton jitter)" : "[fx] TAA off");
        }
    );
    palette.register_command(
        87,
        "FX: Toggle DOF",
        [&]
        {
            fx_dof_strength = (fx_dof_strength > 0.001F) ? 0.0F : 0.5F;
            log_push(fx_dof_strength > 0.001F ? "[fx] DOF on (composite bokeh)" : "[fx] DOF off");
        }
    );
    palette.register_command(
        88,
        "FX: Toggle HDR10 (queued)",
        [&]
        {
            fx_hdr10_request = !fx_hdr10_request;
            log_push(fx_hdr10_request ? "[fx] HDR10 request queued (swapchain rework)" : "[fx] HDR10 off");
        }
    );
    palette.register_command(
        90,
        "FX: Toggle Height Fog (inline exp)",
        [&]
        {
            fx_fog_density = (fx_fog_density > 0.001F) ? 0.0F : 0.6F;
            log_push(fx_fog_density > 0.001F ? "[fx] Height fog on" : "[fx] Height fog off");
        }
    );
    palette.register_command(
        91,
        "FX: Toggle Aerial Perspective (inline)",
        [&]
        {
            fx_aerial_perspective = (fx_aerial_perspective > 0.001F) ? 0.0F : 0.7F;
            log_push(fx_aerial_perspective > 0.001F ? "[fx] Aerial perspective on" : "[fx] Aerial perspective off");
        }
    );
    palette.register_command(
        92,
        "FX: Toggle Clouds",
        [&]
        {
            fx_clouds_coverage = (fx_clouds_coverage > 0.001F) ? 0.0F : 0.55F;
            log_push(fx_clouds_coverage > 0.001F ? "[fx] Clouds on (composite fBm sky overlay)" : "[fx] Clouds off");
        }
    );
    palette.register_command(
        93,
        "FX: Toggle Light Shafts",
        [&]
        {
            fx_shafts_strength = (fx_shafts_strength > 0.001F) ? 0.0F : 0.5F;
            log_push(
                fx_shafts_strength > 0.001F ? "[fx] Light shafts on (Mitchell 2007 god rays)" : "[fx] Light shafts off"
            );
        }
    );
    // Silence -Wunused-variable on the not-yet-dispatched libs.
    (void)fx_ssr;
    (void)fx_dof;
    (void)fx_mblur;
    (void)fx_taa;
    (void)fx_smaa;
    palette.register_command(
        1,
        "Edit: Undo",
        [&]
        {
            if (history.undo())
                log_push("[palette] Undo");
        }
    );
    palette.register_command(
        2,
        "Edit: Redo",
        [&]
        {
            if (history.redo())
                log_push("[palette] Redo");
        }
    );
    palette.register_command(
        3,
        "Edit: Clear History",
        [&]
        {
            history.clear();
            log_push("[palette] History cleared");
        }
    );
    palette.register_command(
        10,
        "Select: Cube",
        [&]
        {
            for (std::size_t i = 0; i < entities.size(); ++i)
                if (entities[i].name == "Cube")
                {
                    selected = int(i);
                    log_push("[palette] Select Cube");
                    break;
                }
        }
    );
    palette.register_command(
        11,
        "Select: Sphere",
        [&]
        {
            for (std::size_t i = 0; i < entities.size(); ++i)
                if (entities[i].name == "Sphere")
                {
                    selected = int(i);
                    log_push("[palette] Select Sphere");
                    break;
                }
        }
    );
    palette.register_command(
        12,
        "Select: Cone",
        [&]
        {
            for (std::size_t i = 0; i < entities.size(); ++i)
                if (entities[i].name == "Cone")
                {
                    selected = int(i);
                    log_push("[palette] Select Cone");
                    break;
                }
        }
    );
    palette.register_command(
        13,
        "Select: Cylinder",
        [&]
        {
            for (std::size_t i = 0; i < entities.size(); ++i)
                if (entities[i].name == "Cylinder")
                {
                    selected = int(i);
                    log_push("[palette] Select Cylinder");
                    break;
                }
        }
    );
    palette.register_command(
        14,
        "Select: Torus",
        [&]
        {
            for (std::size_t i = 0; i < entities.size(); ++i)
                if (entities[i].name == "Torus")
                {
                    selected = int(i);
                    log_push("[palette] Select Torus");
                    break;
                }
        }
    );
    palette.register_command(
        20,
        "Transform: Reset Selected",
        [&]
        {
            if (selected >= 0 && selected < int(entities.size()))
            {
                auto& ent = entities[size_t(selected)];
                if (auto* lt = scene.local(ent.handle); lt)
                {
                    lt->value.position = {};
                    lt->value.scale = { 1.0F, 1.0F, 1.0F };
                    lt->value.rotation = { 0.0F, 0.0F, 0.0F, 1.0F };
                    log_push("[palette] Reset selected transform");
                }
            }
        }
    );
    palette.register_command(
        30,
        "Camera: Toggle Auto-Spin",
        [&]
        {
            scene_cam.set_auto_spin(!scene_cam.auto_spin());
            // Re-engaging auto-spin also exits manual mode so the
            // orbit camera takes back control.
            if (scene_cam.auto_spin())
                cam_manual_mode = false;
            log_push(std::string("[palette] Auto-spin: ") + (scene_cam.auto_spin() ? "ON" : "OFF"));
        }
    );
    palette.register_command(
        31,
        "Camera: Follow Selected",
        [&]
        {
            if (selected >= 0 && selected < int(entities.size()))
            {
                scene_cam.attach(cam, scene, entities[size_t(selected)].handle);
                log_push("[palette] Camera following: " + entities[size_t(selected)].name);
            }
        }
    );
    palette.register_command(
        40,
        "Audio: Toggle Mute",
        [&]
        {
            audio_muted = !audio_muted;
            // Phase 139 v3 - drive WASAPI voice volume so mute is audible.
            if (audio_live_ok && audio_backend && live_voice.is_valid())
                audio_backend->set_volume(live_voice, audio_muted ? 0.0F : 0.65F);
            log_push(std::string("[palette] Audio: ") + (audio_muted ? "MUTED" : "LIVE"));
        }
    );
    palette.register_command(
        50,
        "Net: Toggle Sim",
        [&]
        {
            net_enabled = !net_enabled;
            log_push(std::string("[palette] Net sim: ") + (net_enabled ? "RUNNING" : "PAUSED"));
        }
    );
    palette.register_command(
        60,
        "Random: Reseed + Refresh",
        [&]
        {
            rand_rng = cd::math::Random { static_cast<std::uint64_t>(std::rand()) };
            rebuild_random_viz();
            log_push("[palette] Random reseeded");
        }
    );
    palette.register_command(
        70,
        "Help: Print Shortcuts",
        [&]
        {
            log_push("Ctrl+Shift+P / F1: command palette");
            log_push("Esc: close palette / quit");
            log_push("WASD: move camera target | Q/E: down/up");
            log_push("Right-mouse drag: FPS look | wheel: zoom");
            log_push("Left-click entity: select | empty space: unselect");
            log_push("F: focus camera on selected");
            log_push("Space: cycle gizmo mode (Translate/Rotate/Scale)");
        }
    );

    // Phase 154 - scene save/load round-trip. The serializer pulls
    // transforms out of cd::scene::Scene; per-entity metadata (name,
    // tint, primitive kind) rides the WriteExtras/ReadExtras callbacks
    // so the round-trip is lossless.
    constexpr const char* kSavePath = "hello_engine.cdscene.json";
    auto kind_name = [](PrimitiveKind k) -> const char*
    {
        switch (k)
        {
            case PrimitiveKind::kSphere:
                return "Sphere";
            case PrimitiveKind::kCone:
                return "Cone";
            case PrimitiveKind::kCylinder:
                return "Cylinder";
            case PrimitiveKind::kTorus:
                return "Torus";
            case PrimitiveKind::kGltf:
                return "Gltf";
            case PrimitiveKind::kCube:
                return "Cube";
        }
        return "Cube";
    };
    palette.register_command(
        80,
        "Scene: Save",
        [&]
        {
            auto find_entity = [&](cd::ecs::Entity e) -> const SceneEntity*
            {
                for (const auto& en : entities)
                    if (en.handle.id == e.id)
                        return &en;
                return nullptr;
            };
            auto root = cd::scene::serialize_scene_with(
                scene,
                [&](cd::ecs::Entity e, cd::asset_json::Object& obj)
                {
                    const auto* en = find_entity(e);
                    if (en == nullptr)
                        return;
                    obj["name"] = cd::asset_json::Value { en->name };
                    obj["kind"] = cd::asset_json::Value { std::string { kind_name(en->kind) } };
                    cd::asset_json::Array tint;
                    tint.push_back(cd::asset_json::Value { static_cast<double>(en->tint.x) });
                    tint.push_back(cd::asset_json::Value { static_cast<double>(en->tint.y) });
                    tint.push_back(cd::asset_json::Value { static_cast<double>(en->tint.z) });
                    obj["tint"] = cd::asset_json::Value { std::move(tint) };
                }
            );
            // Extend with a top-level "lights" array so the lights
            // panel state round-trips through save/load too -
            // priority gap #15.
            {
                cd::asset_json::Array light_arr;
                for (const auto& l : lights)
                {
                    cd::asset_json::Object lo;
                    lo["name"] = cd::asset_json::Value { l.name };
                    lo["enabled"] = cd::asset_json::Value { l.enabled };
                    lo["type"] = cd::asset_json::Value { static_cast<int>(l.light.type) };
                    lo["kelvin"] = cd::asset_json::Value { static_cast<double>(l.kelvin) };
                    lo["intensity"] = cd::asset_json::Value { static_cast<double>(l.light.intensity) };
                    lo["range"] = cd::asset_json::Value { static_cast<double>(l.light.range) };
                    cd::asset_json::Array pos;
                    pos.push_back(cd::asset_json::Value { static_cast<double>(l.light.position.x) });
                    pos.push_back(cd::asset_json::Value { static_cast<double>(l.light.position.y) });
                    pos.push_back(cd::asset_json::Value { static_cast<double>(l.light.position.z) });
                    lo["position"] = cd::asset_json::Value { std::move(pos) };
                    // Persist the linear RGB colour so the load path can
                    // restore a user-picked tint (kelvin=0 mode).
                    cd::asset_json::Array col;
                    col.push_back(cd::asset_json::Value { static_cast<double>(l.light.color.x) });
                    col.push_back(cd::asset_json::Value { static_cast<double>(l.light.color.y) });
                    col.push_back(cd::asset_json::Value { static_cast<double>(l.light.color.z) });
                    lo["color"] = cd::asset_json::Value { std::move(col) };
                    cd::asset_json::Array dir;
                    dir.push_back(cd::asset_json::Value { static_cast<double>(l.light.direction.x) });
                    dir.push_back(cd::asset_json::Value { static_cast<double>(l.light.direction.y) });
                    dir.push_back(cd::asset_json::Value { static_cast<double>(l.light.direction.z) });
                    lo["direction"] = cd::asset_json::Value { std::move(dir) };
                    light_arr.push_back(cd::asset_json::Value { std::move(lo) });
                }
                auto& obj = root.as_object_mut();
                obj["lights"] = cd::asset_json::Value { std::move(light_arr) };
            }
            const auto text = cd::asset_json::serialize(root, /*pretty=*/true);
            std::ofstream f { kSavePath, std::ios::binary | std::ios::trunc };
            if (f)
            {
                f.write(text.data(), static_cast<std::streamsize>(text.size()));
                log_push(
                    std::string { "[scene] Saved " } + std::to_string(entities.size()) + " entities + " +
                    std::to_string(lights.size()) + " lights to " + kSavePath
                );
            }
            else
            {
                log_push("[scene] Save failed (ofstream)");
            }
        }
    );
    palette.register_command(
        95,
        "Gizmo: Toggle Visibility",
        [&]
        {
            gizmo_visible = !gizmo_visible;
            log_push(std::string("[gizmo] visible=") + (gizmo_visible ? "true" : "false"));
        }
    );
    palette.register_command(
        90,
        "Streamer: Enqueue 8 burst",
        [&]
        {
            for (int i = 0; i < 8; ++i)
                streamer_enqueue(i * 10);
            log_push("[palette] Streamer +8 burst");
        }
    );
    palette.register_command(
        91,
        "Streamer: Enqueue 32 burst",
        [&]
        {
            for (int i = 0; i < 32; ++i)
                streamer_enqueue(i % 4);
            log_push("[palette] Streamer +32 burst");
        }
    );
    palette.register_command(
        81,
        "Scene: Load (replace world)",
        [&]
        {
            auto r = cd::asset_json::load(kSavePath);
            if (!r.has_value())
            {
                log_push(std::string { "[scene] Load failed: " } + std::string { r.error().message });
                return;
            }
            // Build a fresh world+scene; old `world` / `scene` get
            // replaced via assignment (cd::scene::Scene holds a
            // reference so we have to rebuild entities vector too).
            // The simpler path: clear `entities`, deserialize into the
            // existing scene, and pull metadata back from the JSON.
            for (auto& en : entities)
            {
                if (en.handle.is_valid())
                    scene.destroy_node(en.handle);
            }
            entities.clear();
            std::vector<SceneEntity> loaded;
            auto rd = cd::scene::deserialize_scene_with(
                scene,
                *r,
                [&](cd::ecs::Entity e, const cd::asset_json::Object& obj)
                {
                    SceneEntity en;
                    en.handle = e;
                    en.kind = PrimitiveKind::kCube;
                    en.tint = { 1.0F, 1.0F, 1.0F };
                    if (auto it = obj.find("name"); it != obj.end() && it->second.is_string())
                        en.name = it->second.as_string();
                    if (auto it = obj.find("kind"); it != obj.end() && it->second.is_string())
                        en.kind = kind_from_name(it->second.as_string());
                    if (auto it = obj.find("tint");
                        it != obj.end() && it->second.is_array() && it->second.as_array().size() == 3)
                    {
                        const auto& a = it->second.as_array();
                        if (a[0].is_number() && a[1].is_number() && a[2].is_number())
                        {
                            en.tint = {
                                static_cast<float>(a[0].as_number()),
                                static_cast<float>(a[1].as_number()),
                                static_cast<float>(a[2].as_number()),
                            };
                        }
                    }
                    loaded.push_back(std::move(en));
                }
            );
            if (!rd.has_value())
            {
                log_push(std::string { "[scene] Deserialize failed: " } + std::string { rd.error().message });
                return;
            }
            entities = std::move(loaded);
            // Lights from the optional top-level "lights" array
            // (priority gap #15). Missing or malformed is non-fatal:
            // we keep the panel's current lights[] vector intact.
            if (r->is_object())
            {
                const auto& root_obj = r->as_object();
                if (auto it = root_obj.find("lights"); it != root_obj.end() && it->second.is_array())
                {
                    const auto& la = it->second.as_array();
                    std::vector<LightRow> new_lights;
                    new_lights.reserve(la.size());
                    for (const auto& lv : la)
                    {
                        if (!lv.is_object())
                            continue;
                        const auto& lo = lv.as_object();
                        LightRow row {};
                        if (auto n = lo.find("name"); n != lo.end() && n->second.is_string())
                            row.name = n->second.as_string();
                        if (auto en = lo.find("enabled"); en != lo.end() && en->second.is_bool())
                            row.enabled = en->second.as_bool();
                        if (auto t = lo.find("type"); t != lo.end() && t->second.is_number())
                            row.light.type = static_cast<cd::light::LightType>(static_cast<int>(t->second.as_number()));
                        if (auto k = lo.find("kelvin"); k != lo.end() && k->second.is_number())
                            row.kelvin = static_cast<float>(k->second.as_number());
                        if (auto i = lo.find("intensity"); i != lo.end() && i->second.is_number())
                            row.light.intensity = static_cast<float>(i->second.as_number());
                        if (auto rg = lo.find("range"); rg != lo.end() && rg->second.is_number())
                            row.light.range = static_cast<float>(rg->second.as_number());
                        if (auto p = lo.find("position");
                            p != lo.end() && p->second.is_array() && p->second.as_array().size() == 3)
                        {
                            const auto& a = p->second.as_array();
                            if (a[0].is_number() && a[1].is_number() && a[2].is_number())
                                row.light.position = { static_cast<float>(a[0].as_number()),
                                                       static_cast<float>(a[1].as_number()),
                                                       static_cast<float>(a[2].as_number()) };
                        }
                        if (auto d = lo.find("direction");
                            d != lo.end() && d->second.is_array() && d->second.as_array().size() == 3)
                        {
                            const auto& a = d->second.as_array();
                            if (a[0].is_number() && a[1].is_number() && a[2].is_number())
                                row.light.direction = { static_cast<float>(a[0].as_number()),
                                                        static_cast<float>(a[1].as_number()),
                                                        static_cast<float>(a[2].as_number()) };
                        }
                        // Restore user-picked RGB if present (kelvin=0
                        // mode), otherwise compute colour from CCT.
                        if (auto c = lo.find("color");
                            c != lo.end() && c->second.is_array() && c->second.as_array().size() == 3)
                        {
                            const auto& a = c->second.as_array();
                            if (a[0].is_number() && a[1].is_number() && a[2].is_number())
                                row.light.color = { static_cast<float>(a[0].as_number()),
                                                    static_cast<float>(a[1].as_number()),
                                                    static_cast<float>(a[2].as_number()) };
                        }
                        else if (row.kelvin > 0.0F)
                        {
                            row.light.color = cd::light::cct_to_linear_rgb(row.kelvin);
                        }
                        new_lights.push_back(std::move(row));
                    }
                    if (!new_lights.empty())
                        lights = std::move(new_lights);
                }
            }
            selected = entities.empty() ? -1 : 0;
            history.clear();
            log_push(
                std::string { "[scene] Loaded " } + std::to_string(entities.size()) + " entities + " +
                std::to_string(lights.size()) + " lights from " + kSavePath
            );
        }
    );

    // ---- Frame loop ----
    using clock = std::chrono::steady_clock;
    const auto frame_loop_start = clock::now();
    auto last_tick = frame_loop_start;
    std::uint32_t frame_idx = 0;
    bool needs_rebuild = false;
    std::vector<cd::platform::OSEvent> events;
    events.reserve(64);

    // Phase 139 v2 - platform-level modifier tracking. cd::imgui_backend
    // doesn't forward Ctrl/Shift state into ImGui's IO reliably, so we
    // track from the same OSEvent KeyDown/KeyUp pairs that drive the
    // rest of the sample.
    bool mod_ctrl = false;
    bool mod_shift = false;

    while (true)
    {
        events.clear();
        if (!window.pump_events(events))
            break;
        for (const auto& e : events)
        {
            ctx.handle_event(e);
            if (e.kind == cd::platform::OSEventKind::kKeyDown && e.key == cd::platform::KeyCode::kEscape)
            {
                // ESC priority chain (lessons-learned ??P3):
                //   1) active gizmo drag ??' cancel + revert
                //   2) palette visible    ??' close palette
                //   3) selection active   ??' clear selection
                //   4) otherwise          ??' no-op (NEVER quit)
                //
                // User feedback: ESC kept closing the window even with
                // the priority chain, because empty editor state fell
                // through to window.request_close(). Production editors
                // (Unity, Blender, UE) never quit on ESC - quit is a
                // menu / close-button action only. Match that.
                if (gizmo.is_dragging())
                {
                    (void)gizmo.end_drag();
                    log_push("[esc] gizmo drag cancelled");
                }
                else if (palette_visible)
                {
                    palette_visible = false;
                    palette_query.clear();
                    log_push("[esc] palette closed");
                }
                else if (selected >= 0)
                {
                    selected = -1;
                    log_push("[esc] selection cleared");
                }
                // else: do nothing - ESC must never close the window.
            }
            else if (e.kind == cd::platform::OSEventKind::kResize)
            {
                needs_rebuild = true;
            }
            // F1 alternatif (focus-ba????ms??z, zero-modifier).
            else if (e.kind == cd::platform::OSEventKind::kKeyDown && e.key == cd::platform::KeyCode::kF1)
            {
                palette_visible = !palette_visible;
                if (palette_visible)
                    palette_query.clear();
            }
            // Phase 139 v2 - platform modifier tracking + Ctrl+Shift+P.
            else if (e.kind == cd::platform::OSEventKind::kKeyDown)
            {
                if (e.key == cd::platform::KeyCode::kLCtrl || e.key == cd::platform::KeyCode::kRCtrl)
                    mod_ctrl = true;
                if (e.key == cd::platform::KeyCode::kLShift || e.key == cd::platform::KeyCode::kRShift)
                    mod_shift = true;
                if (e.key == cd::platform::KeyCode::kP && mod_ctrl && mod_shift)
                {
                    palette_visible = !palette_visible;
                    if (palette_visible)
                        palette_query.clear();
                }
            }
            else if (e.kind == cd::platform::OSEventKind::kKeyUp)
            {
                if (e.key == cd::platform::KeyCode::kLCtrl || e.key == cd::platform::KeyCode::kRCtrl)
                    mod_ctrl = false;
                if (e.key == cd::platform::KeyCode::kLShift || e.key == cd::platform::KeyCode::kRShift)
                    mod_shift = false;
            }

            // ---- WASD movement keys (continuous state) ----
            const bool key_dn = (e.kind == cd::platform::OSEventKind::kKeyDown);
            const bool key_up = (e.kind == cd::platform::OSEventKind::kKeyUp);
            if (key_dn || key_up)
            {
                const bool v = key_dn;
                if (e.key == cd::platform::KeyCode::kW)
                    key_w = v;
                if (e.key == cd::platform::KeyCode::kA)
                    key_a = v;
                if (e.key == cd::platform::KeyCode::kS)
                    key_s = v;
                if (e.key == cd::platform::KeyCode::kD)
                    key_d = v;
                if (e.key == cd::platform::KeyCode::kQ)
                    key_q = v;
                if (e.key == cd::platform::KeyCode::kE)
                    key_e = v;
                // W6-F: shift = fast (x2.5), ctrl = slow (x0.25). Either
                // L or R modifier engages the multiplier.
                if (e.key == cd::platform::KeyCode::kLShift || e.key == cd::platform::KeyCode::kRShift)
                    key_shift = v;
                if (e.key == cd::platform::KeyCode::kLCtrl || e.key == cd::platform::KeyCode::kRCtrl)
                    key_ctrl = v;
                // Engage manual mode on any WASD/QE press so scene_cam
                // stops fighting the user.
                if (key_dn && (e.key == cd::platform::KeyCode::kW || e.key == cd::platform::KeyCode::kA ||
                               e.key == cd::platform::KeyCode::kS || e.key == cd::platform::KeyCode::kD ||
                               e.key == cd::platform::KeyCode::kQ || e.key == cd::platform::KeyCode::kE))
                {
                    cam_manual_mode = true;
                    scene_cam.set_auto_spin(false);
                }
            }
            // F = focus the camera on the currently selected entity (frame).
            if (key_dn && e.key == cd::platform::KeyCode::kF && selected >= 0 &&
                selected < static_cast<int>(entities.size()))
            {
                if (auto* lt = scene.local(entities[static_cast<std::size_t>(selected)].handle))
                {
                    cam.target.x = lt->value.position.x;
                    cam.target.y = lt->value.position.y;
                    cam.target.z = lt->value.position.z;
                    log_push("[cam] focus " + entities[static_cast<std::size_t>(selected)].name);
                }
            }
            // Space = cycle gizmo mode translate ??' rotate ??' scale ??' translate.
            if (key_dn && e.key == cd::platform::KeyCode::kSpace && !ImGui::GetIO().WantCaptureKeyboard)
            {
                gizmo_mode = static_cast<GizmoMode>((static_cast<std::uint8_t>(gizmo_mode) + 1u) % 3u);
                const char* mode_str = gizmo_mode == GizmoMode::kTranslate ? "TRANSLATE"
                                       : gizmo_mode == GizmoMode::kRotate  ? "ROTATE"
                                                                           : "SCALE";
                log_push(std::string { "[gizmo] mode: " } + mode_str);
            }
            // Delete = remove currently selected entity OR light (user feedback:
            // "objeleri ve isiklari kafama gore silebilmeliyim"). Gated on
            // WantCaptureKeyboard so text-input fields in ImGui don't trigger.
            if (key_dn && e.key == cd::platform::KeyCode::kDelete && !ImGui::GetIO().WantCaptureKeyboard &&
                selected >= 0)
            {
                if (selected_kind == SelKind::kEntity && selected < static_cast<int>(entities.size()))
                {
                    const std::string name = entities[static_cast<std::size_t>(selected)].name;
                    scene.destroy_node(entities[static_cast<std::size_t>(selected)].handle);
                    entities.erase(entities.begin() + selected);
                    log_push(std::string { "[edit] entity deleted: " } + name);
                }
                else if (selected_kind == SelKind::kLight && selected < static_cast<int>(lights.size()))
                {
                    const std::string name = lights[static_cast<std::size_t>(selected)].name;
                    lights.erase(lights.begin() + selected);
                    log_push(std::string { "[edit] light deleted: " } + name);
                }
                selected = -1;
            }

            // ---- Right-mouse drag ??' FPS look; left-click ??' request pick ----
            if (e.kind == cd::platform::OSEventKind::kMouseButtonDown)
            {
                if (e.mouse_button == cd::platform::MouseButton::kRight)
                {
                    cam_right_drag = true;
                    cam_manual_mode = true;  // persist until user re-enables auto-spin
                    has_last_mouse = false;
                    scene_cam.set_auto_spin(false);
                    // INIT yaw/pitch + dist from the current orbit camera so
                    // the right-drag mode doesn't snap to a default pose.
                    const float dxd = cam.target.x - cam.eye.x;
                    const float dyd = cam.target.y - cam.eye.y;
                    const float dzd = cam.target.z - cam.eye.z;
                    const float dist = std::sqrt(dxd * dxd + dyd * dyd + dzd * dzd);
                    if (dist > 1e-3F)
                    {
                        cam_dist = dist;
                        cam_pitch = std::asin(dyd / dist);
                        cam_yaw = std::atan2(dxd, -dzd);
                    }
                }
                else if (e.mouse_button == cd::platform::MouseButton::kLeft && !ImGui::GetIO().WantCaptureMouse)
                {
                    pending_pick = true;
                    pick_x = e.mouse_x;
                    pick_y = e.mouse_y;
                }
            }
            if (e.kind == cd::platform::OSEventKind::kMouseButtonUp &&
                e.mouse_button == cd::platform::MouseButton::kRight)
            {
                cam_right_drag = false;
            }
            if (e.kind == cd::platform::OSEventKind::kMouseMove)
            {
                if (cam_right_drag && has_last_mouse)
                {
                    const float dx = e.mouse_x - last_mouse_x;
                    const float dy = e.mouse_y - last_mouse_y;
                    // FPS convention: mouse right ? camera yaws right (world
                    // appears to drift left). User-reported B03 - sign on X
                    // was inverted; Y stays as "mouse down ? look down".
                    cam_yaw += dx * kCamLookSpeed;
                    cam_pitch -= dy * kCamLookSpeed;
                    // Clamp pitch so we don't flip the camera over.
                    constexpr float kHalfPi = 1.5707963F;
                    if (cam_pitch > kHalfPi - 0.05F)
                        cam_pitch = kHalfPi - 0.05F;
                    if (cam_pitch < -kHalfPi + 0.05F)
                        cam_pitch = -kHalfPi + 0.05F;
                }
                last_mouse_x = e.mouse_x;
                last_mouse_y = e.mouse_y;
                has_last_mouse = true;
            }
            if (e.kind == cd::platform::OSEventKind::kMouseWheel && !ImGui::GetIO().WantCaptureMouse)
            {
                cam_dist *= (e.wheel > 0.0F) ? 0.9F : 1.1F;
                if (cam_dist < 1.0F)
                    cam_dist = 1.0F;
                if (cam_dist > 100.0F)
                    cam_dist = 100.0F;
            }
        }
        if (needs_rebuild)
        {
            if (window.width() == 0 || window.height() == 0)
                continue;
            if (!renderer.recreate_swapchain({ window.width(), window.height() }).has_value())
                continue;
            if (!create_depth_target(
                    device,
                    { window.width(), window.height() },
                    kDepthFormat,
                    depth,
                    cd::rhi::TextureUsage::kSampled
                ))
                continue;
            if (!create_color_target(device, { window.width(), window.height() }, kHdrFormat, hdr_target))
                continue;
            if (!create_color_target(device, { window.width(), window.height() }, kNormalFormat, gbuf_normal))
                continue;
            if (!create_color_target(device, { window.width(), window.height() }, kAlbedoFormat, gbuf_albedo))
                continue;
            if (!create_color_target(device, { window.width(), window.height() }, kMrFormat, gbuf_mr))
                continue;
            if (!create_color_target(device, { window.width(), window.height() }, kVelocityFormat, gbuf_velocity))
                continue;
            bool history_ok = true;
            for (auto& h : history_targets)
            {
                if (!create_color_target(device, { window.width(), window.height() }, kHistoryFormat, h))
                {
                    history_ok = false;
                    break;
                }
            }
            if (!history_ok)
                continue;
            history_states[0] = cd::rhi::ResourceState::kUndefined;
            history_states[1] = cd::rhi::ResourceState::kUndefined;
            if (!create_bloom_chain(device, { window.width(), window.height() }, bloom_chain))
                continue;
            bind_bloom_descriptors();
            bind_composite_hdr();
            depth_initialised_on_gpu = false;
            needs_rebuild = false;
        }

        // ---- dt ----
        const auto now = clock::now();
        const float dt = std::chrono::duration<float>(now - last_tick).count();
        last_tick = now;

        // ---- Tick audio chain (always-on synthesis) ----
        if (!audio_muted)
        {
            float peak = 0.0F;
            float comp_db_min = 0.0F;
            float lim_gain_min = 1.0F;
            for (std::size_t s = 0; s < kAudioBufferLen; ++s, ++audio_t)
            {
                audio_bus.mix(0, square_wave(audio_t, 440.0F));
                audio_bus.mix(1, burst_noise(audio_t));
                float x = audio_bus.pull();
                x = comp.process(x);
                if (comp.gain_db() < comp_db_min)
                    comp_db_min = comp.gain_db();
                const float wet = reverb.process(x);
                x = 0.75F * x + 0.20F * wet;
                x = lowpass.process(x);
                x = limiter.process(x);
                if (limiter.current_gain() < lim_gain_min)
                    lim_gain_min = limiter.current_gain();
                if (std::fabs(x) > peak)
                    peak = std::fabs(x);
                // Phase 139 - capture to 5 s ring buffer.
                if (x > 1.0F)
                    x = 1.0F;
                if (x < -1.0F)
                    x = -1.0F;
                audio_ring[audio_ring_write] = static_cast<std::int16_t>(x * 32760.0F);
                ++audio_ring_write;
                if (audio_ring_write >= kAudioRingFrames)
                    audio_ring_write = 0;
                ++audio_total_written;
            }
            audio_peak_window = peak;
            audio_comp_db_window = comp_db_min;
            audio_limiter_gain_min = lim_gain_min;
            audio_meter_history.push_back(peak);
            while (audio_meter_history.size() > 120)
                audio_meter_history.pop_front();
            counters.increment("audio_ticks");
        }

        // ---- Tick net sim ----
        if (net_enabled)
        {
            net_t += static_cast<double>(dt);
            net_throttle.update(dt);
            while (net_t >= next_net_tick)
            {
                next_net_tick += 1.0 / 60.0;  // 60 Hz server tick
                if (net_throttle.try_consume(1.0F))
                {
                    const float v = std::sin(static_cast<float>(net_t) * 1.2F);
                    // delta vs last baseline
                    const std::byte cur_bytes[4] = {
                        std::byte((std::uint32_t(v * 1e6F) >> 0) & 0xFFu),
                        std::byte((std::uint32_t(v * 1e6F) >> 8) & 0xFFu),
                        std::byte((std::uint32_t(v * 1e6F) >> 16) & 0xFFu),
                        std::byte((std::uint32_t(v * 1e6F) >> 24) & 0xFFu),
                    };
                    const std::byte base_bytes[4] = {
                        std::byte((std::uint32_t(net_baseline * 1e6F) >> 0) & 0xFFu),
                        std::byte((std::uint32_t(net_baseline * 1e6F) >> 8) & 0xFFu),
                        std::byte((std::uint32_t(net_baseline * 1e6F) >> 16) & 0xFFu),
                        std::byte((std::uint32_t(net_baseline * 1e6F) >> 24) & 0xFFu),
                    };
                    const auto delta = cd::net::write_delta(
                        std::span<const std::byte>(base_bytes),
                        std::span<const std::byte>(cur_bytes)
                    );
                    net_raw_bytes += 4;
                    net_wire_bytes += delta.size();
                    net_baseline = v;
                    ++net_sent;
                    if (net_rng.next_float() < 0.10F)
                    {
                        ++net_drop;
                    }
                    else
                    {
                        const double lat = 0.03 + 0.06 * static_cast<double>(net_rng.next_float());
                        net_rtt.record(static_cast<std::uint32_t>(lat * 2.0 * 1e6));
                        net_snapbuf.push(net_t + lat, v);
                        ++net_recv;
                    }
                }
            }
            (void)net_snapbuf.sample(net_t - 0.10);  // client-side interp
            net_snapbuf.drop_older_than(net_t - 0.5);
            counters.increment("net_ticks");
        }

        // ---- Random viz periodic refresh ----
        if (frame_idx >= next_random_refresh)
        {
            rebuild_random_viz();
            next_random_refresh = frame_idx + 120;  // ~2 s @ 60 fps
        }

        // ---- SK4+SK7: real skinned-mesh animation (CPU-LBS) ----
        // When the loaded asset carries skin + animation data
        // (CesiumMan ships both), sample the first animation track at
        // current time, build the matrix palette via
        // cd::anim::compute_skinning_matrices, CPU-skin every vertex
        // with 4-weight LBS, and re-upload the deformed PrimitiveVertex
        // buffer to gltf_mesh.vb. Existing prim pipeline renders the
        // deformed character without needing a separate skinned-vertex
        // pipeline. CesiumMan has ~3k vertices so the per-frame cost
        // stays well under 1 ms on a modern CPU.
        // When the asset has no skin (or has no skin_vertices), we
        // fall back to the W4-F turntable so the imported model still
        // reads as 'alive' rather than a static statue.
        if (skinned.valid && gltf_mesh.vb.is_valid())
        {
            // Advance + loop animation time.
            skinned.anim_t += dt;
            if (skinned.animation.duration > 0.0F)
            {
                while (skinned.anim_t > skinned.animation.duration)
                    skinned.anim_t -= skinned.animation.duration;
            }
            // Reset to bind pose then overlay the animation channels —
            // joints without an animation track stay at their bind
            // position (correct glTF sampling semantics).
            skinned.pose = cd::anim::Pose::bind_pose(skinned.skeleton);
            cd::asset_gltf::sample_gltf_animation(
                skinned.animation,
                skinned.node_to_joint,
                skinned.anim_t,
                skinned.pose
            );
            // Per-joint skinning matrices = world(pose) * inverse_bind.
            cd::anim::compute_skinning_matrices(skinned.skeleton, skinned.pose, skinned.palette_scratch);
            // CPU-skin every vertex.
            const auto& bone_palette = skinned.palette_scratch;
            const std::size_t nv = skinned.base_positions.size();
            for (std::size_t i = 0; i < nv; ++i)
            {
                const auto& inf = skinned.influences[i];
                // Normalise weights so artist-authored non-normalised
                // skin data still produces a unit blend.
                float w_sum = inf.weights[0] + inf.weights[1] + inf.weights[2] + inf.weights[3];
                if (w_sum < 1e-5F)
                    w_sum = 1.0F;
                const float inv_w = 1.0F / w_sum;
                cd::math::Mat4f skin_mat {};  // zero
                for (std::size_t k = 0; k < 4; ++k)
                {
                    const std::uint16_t skin_joint = inf.joints[k];
                    const float w = inf.weights[k] * inv_w;
                    if (w <= 0.0F)
                        continue;
                    // SK-fix: translate skin-joint index to skeleton-
                    // joint index. Vertex JOINTS_0 attributes hold the
                    // position within gskin.joints[], not the topo-
                    // sorted skeleton index — wrong palette lookup
                    // produced the twisted-limb render the user saw.
                    if (skin_joint >= skinned.skin_joint_remap.size())
                        continue;
                    const std::int32_t sj = skinned.skin_joint_remap[skin_joint];
                    if (sj < 0 || sj >= static_cast<std::int32_t>(bone_palette.size()))
                        continue;
                    const auto& m = bone_palette[static_cast<std::size_t>(sj)];
                    for (std::size_t c = 0; c < 4; ++c)
                        for (std::size_t r = 0; r < 4; ++r)
                            skin_mat[c][r] += w * m[c][r];
                }
                const auto& bp = skinned.base_positions[i];
                const auto& bn = skinned.base_normals[i];
                // pos: full mat4 transform.
                cd::math::Vec4f p4 {
                    skin_mat[0][0] * bp.x + skin_mat[1][0] * bp.y + skin_mat[2][0] * bp.z + skin_mat[3][0],
                    skin_mat[0][1] * bp.x + skin_mat[1][1] * bp.y + skin_mat[2][1] * bp.z + skin_mat[3][1],
                    skin_mat[0][2] * bp.x + skin_mat[1][2] * bp.y + skin_mat[2][2] * bp.z + skin_mat[3][2],
                    skin_mat[0][3] * bp.x + skin_mat[1][3] * bp.y + skin_mat[2][3] * bp.z + skin_mat[3][3]
                };
                // normal: 3x3 transform (no translation), no normalize
                // (renormalised by FS via length-corrected lighting).
                cd::math::Vec3f n3 { skin_mat[0][0] * bn.x + skin_mat[1][0] * bn.y + skin_mat[2][0] * bn.z,
                                     skin_mat[0][1] * bn.x + skin_mat[1][1] * bn.y + skin_mat[2][1] * bn.z,
                                     skin_mat[0][2] * bn.x + skin_mat[1][2] * bn.y + skin_mat[2][2] * bn.z };
                auto& out = skinned.deformed_scratch[i];
                out.pos[0] = p4.x;
                out.pos[1] = p4.y;
                out.pos[2] = p4.z;
                out.normal[0] = n3.x;
                out.normal[1] = n3.y;
                out.normal[2] = n3.z;
                out.uv[0] = skinned.base_uvs[i].x;
                out.uv[1] = skinned.base_uvs[i].y;
                out.color[0] = 0.85F;
                out.color[1] = 0.82F;
                out.color[2] = 0.78F;
            }
            // Upload deformed vertices to GPU buffer used by the kGltf
            // entity draw. gltf_mesh.vb was created CpuToGpu so the
            // per-frame upload is non-blocking.
            (void)device.upload_buffer(
                gltf_mesh.vb,
                0,
                std::span<const std::byte>(
                    reinterpret_cast<const std::byte*>(skinned.deformed_scratch.data()),
                    skinned.deformed_scratch.size() * sizeof(cd::asset::PrimitiveVertex)
                )
            );
        }
        else
        {
            // W4-F fallback turntable for assets without skin data.
            static float cesium_yaw_t = 0.0F;
            cesium_yaw_t += dt * 0.5F;
            for (auto& ent : entities)
            {
                if (ent.kind != PrimitiveKind::kGltf)
                    continue;
                auto* lt = scene.local(ent.handle);
                if (lt == nullptr)
                    continue;
                const float half = cesium_yaw_t * 0.5F;
                const float sy = std::sin(half);
                const float cy = std::cos(half);
                const float qx1 = 0.0F, qy1 = sy, qz1 = 0.0F, qw1 = cy;
                const float qx2 = -0.7071068F, qy2 = 0.0F, qz2 = 0.0F, qw2 = 0.7071068F;
                lt->value.rotation.x = qw1 * qx2 + qx1 * qw2 + qy1 * qz2 - qz1 * qy2;
                lt->value.rotation.y = qw1 * qy2 - qx1 * qz2 + qy1 * qw2 + qz1 * qx2;
                lt->value.rotation.z = qw1 * qz2 + qx1 * qy2 - qy1 * qx2 + qz1 * qw2;
                lt->value.rotation.w = qw1 * qw2 - qx1 * qx2 - qy1 * qy2 - qz1 * qz2;
                break;
            }
        }

        // ---- Update scene camera ----
        // If the user is right-dragging OR pressing any WASD key, take
        // direct control: the SceneCameraController's orbit is bypassed
        // and we drive cam.eye / cam.target from yaw/pitch/dist + WASD.
        const bool wasd_active = key_w || key_a || key_s || key_d || key_q || key_e;
        if (cam_right_drag || wasd_active)
        {
            // On WASD-first frame, sync yaw/pitch/dist from current cam so
            // the position doesn't snap.
            static bool wasd_was_active_prev = false;
            if (wasd_active && !wasd_was_active_prev && !cam_right_drag)
            {
                const float dxd = cam.target.x - cam.eye.x;
                const float dyd = cam.target.y - cam.eye.y;
                const float dzd = cam.target.z - cam.eye.z;
                const float dist = std::sqrt(dxd * dxd + dyd * dyd + dzd * dzd);
                if (dist > 1e-3F)
                {
                    cam_dist = dist;
                    cam_pitch = std::asin(dyd / dist);
                    cam_yaw = std::atan2(dxd, -dzd);
                }
            }
            wasd_was_active_prev = wasd_active;

            // Forward = view direction in world space.
            const float cp = std::cos(cam_pitch), sp = std::sin(cam_pitch);
            const float cy = std::cos(cam_yaw), sy = std::sin(cam_yaw);
            cd::math::Vec3f forward { cp * sy, sp, -cp * cy };
            cd::math::Vec3f right { cy, 0.0F, sy };

            // WASD moves the camera *target* (and eye follows by cam_dist).
            // W6-F: hold shift for fast (x2.5), hold ctrl for slow (x0.25);
            // both held cancel and stay at 1x — useful for fine alignment
            // while inspecting a specific shader / area light.
            float spd_scale = 1.0F;
            if (key_shift)
                spd_scale *= 2.5F;
            if (key_ctrl)
                spd_scale *= 0.25F;
            const float spd = kCamMoveSpeed * dt * spd_scale;
            if (key_w)
            {
                cam.target.x += forward.x * spd;
                cam.target.y += forward.y * spd;
                cam.target.z += forward.z * spd;
            }
            if (key_s)
            {
                cam.target.x -= forward.x * spd;
                cam.target.y -= forward.y * spd;
                cam.target.z -= forward.z * spd;
            }
            if (key_d)
            {
                cam.target.x += right.x * spd;
                cam.target.z += right.z * spd;
            }
            if (key_a)
            {
                cam.target.x -= right.x * spd;
                cam.target.z -= right.z * spd;
            }
            if (key_e)
            {
                cam.target.y += spd;
            }
            if (key_q)
            {
                cam.target.y -= spd;
            }

            // Eye = target - forward * cam_dist (so the target stays in view).
            cam.eye.x = cam.target.x - forward.x * cam_dist;
            cam.eye.y = cam.target.y - forward.y * cam_dist;
            cam.eye.z = cam.target.z - forward.z * cam_dist;
        }
        else if (!cam_manual_mode)
        {
            // Only auto-orbit if the user hasn't started manual control.
            // Once manual mode engages, the camera stays exactly where
            // the user left it on right-mouse release / WASD release.
            scene_cam.update(dt);
        }

        // ---- 3D click-to-pick ----
        // Unproject the click pixel to a world ray, then sphere-test
        // each entity. The gizmo overlay (rendered later in this
        // frame) may set `pending_pick=false` if the click landed on
        // an axis arrow - in that case it consumed the click and we
        // skip the pick. The frame here is one-late but for a UX
        // click the lag is invisible.
        if (pending_pick && gizmo_was_hovered)
        {
            // The user is clicking on a gizmo arrow (hover detected
            // last frame). Don't repick; let the gizmo claim the drag.
            pending_pick = false;
        }
        if (pending_pick)
        {
            pending_pick = false;
            const float vw = static_cast<float>(window.width());
            const float vh = static_cast<float>(window.height());
            if (vw > 0 && vh > 0)
            {
                const float aspect_pick = vw / vh;
                // Invert VP analytically would be ideal; we use unproject
                // via two ray endpoints (NDC near + far) ??' world.
                const float ndc_x = (2.0F * pick_x / vw) - 1.0F;
                const float ndc_y = 1.0F - (2.0F * pick_y / vh);
                // Build inverse VP by row-by-row 4x4 inversion. Use the
                // engine's existing utility if present; otherwise a small
                // local Gauss-Jordan would do. Quick path: use camera
                // basis directly.
                const float cp = std::cos(cam_pitch), sp = std::sin(cam_pitch);
                const float cy = std::cos(cam_yaw), sy = std::sin(cam_yaw);
                cd::math::Vec3f fwd { cp * sy, sp, -cp * cy };
                cd::math::Vec3f rgt { cy, 0.0F, sy };
                cd::math::Vec3f up_v { fwd.y * rgt.z - fwd.z * rgt.y,
                                       fwd.z * rgt.x - fwd.x * rgt.z,
                                       fwd.x * rgt.y - fwd.y * rgt.x };
                // Use the orbit camera's basis when we're NOT in WASD mode.
                if (!cam_right_drag && !wasd_active)
                {
                    fwd.x = cam.target.x - cam.eye.x;
                    fwd.y = cam.target.y - cam.eye.y;
                    fwd.z = cam.target.z - cam.eye.z;
                    const float fl = std::sqrt(fwd.x * fwd.x + fwd.y * fwd.y + fwd.z * fwd.z);
                    if (fl > 1e-5F)
                    {
                        fwd.x /= fl;
                        fwd.y /= fl;
                        fwd.z /= fl;
                    }
                    cd::math::Vec3f world_up { 0, 1, 0 };
                    rgt.x = fwd.y * world_up.z - fwd.z * world_up.y;
                    rgt.y = fwd.z * world_up.x - fwd.x * world_up.z;
                    rgt.z = fwd.x * world_up.y - fwd.y * world_up.x;
                    const float rl = std::sqrt(rgt.x * rgt.x + rgt.y * rgt.y + rgt.z * rgt.z);
                    if (rl > 1e-5F)
                    {
                        rgt.x /= rl;
                        rgt.y /= rl;
                        rgt.z /= rl;
                    }
                    up_v.x = rgt.y * fwd.z - rgt.z * fwd.y;
                    up_v.y = rgt.z * fwd.x - rgt.x * fwd.z;
                    up_v.z = rgt.x * fwd.y - rgt.y * fwd.x;
                }
                const float tan_half_fov = std::tan(cam.fov_y * 0.5F);
                const float scale_x = aspect_pick * tan_half_fov;
                const float scale_y = tan_half_fov;
                cd::math::Vec3f ray_dir { fwd.x + rgt.x * ndc_x * scale_x + up_v.x * ndc_y * scale_y,
                                          fwd.y + rgt.y * ndc_x * scale_x + up_v.y * ndc_y * scale_y,
                                          fwd.z + rgt.z * ndc_x * scale_x + up_v.z * ndc_y * scale_y };
                const float rdl = std::sqrt(ray_dir.x * ray_dir.x + ray_dir.y * ray_dir.y + ray_dir.z * ray_dir.z);
                if (rdl > 1e-5F)
                {
                    ray_dir.x /= rdl;
                    ray_dir.y /= rdl;
                    ray_dir.z /= rdl;
                }

                // Sphere-test every entity. Radius scales with the
                // entity's transform scale so clicking anywhere on a
                // big imported asset (e.g. CesiumMan at scale 2.2)
                // still selects it - not just the central pivot.
                // Closes user-flagged 'cisimler ve isiklar sadece
                // pivottan secilebiliyor'.
                float best_t = 1e30F;
                int best_i = -1;
                for (std::size_t i = 0; i < entities.size(); ++i)
                {
                    auto* lt = scene.local(entities[i].handle);
                    if (lt == nullptr)
                        continue;
                    const cd::math::Vec3f c { lt->value.position.x, lt->value.position.y, lt->value.position.z };
                    const float ms = std::max({ lt->value.scale.x, lt->value.scale.y, lt->value.scale.z });
                    // Unit primitive half-extent ??? 0.55; for compound
                    // / oblong meshes (humanoid) bump by 1.6 along the
                    // longest dimension.
                    const float pick_r = 0.55F * std::max(1.0F, ms) * 1.6F;
                    const cd::math::Vec3f oc { cam.eye.x - c.x, cam.eye.y - c.y, cam.eye.z - c.z };
                    const float b = oc.x * ray_dir.x + oc.y * ray_dir.y + oc.z * ray_dir.z;
                    const float cc = oc.x * oc.x + oc.y * oc.y + oc.z * oc.z - pick_r * pick_r;
                    const float disc = b * b - cc;
                    if (disc < 0.0F)
                        continue;
                    const float t = -b - std::sqrt(disc);
                    if (t > 0.0F && t < best_t)
                    {
                        best_t = t;
                        best_i = static_cast<int>(i);
                    }
                }
                // Also try light positions (point/spot only - directional
                // has no world position, area is bigger but we use its center).
                int best_light = -1;
                float best_light_t = best_t;
                for (std::size_t i = 0; i < lights.size(); ++i)
                {
                    const auto& Lt = lights[i].light;
                    if (Lt.type == cd::light::LightType::kDirectional)
                        continue;
                    const cd::math::Vec3f c { Lt.position.x, Lt.position.y, Lt.position.z };
                    const cd::math::Vec3f oc { cam.eye.x - c.x, cam.eye.y - c.y, cam.eye.z - c.z };
                    // Big-pick light bulb hit-sphere so clicking near
                    // the gizmo or anywhere around the visible bulb
                    // selects the light, not just its centre dot.
                    constexpr float kLightPickR = 0.9F;
                    const float b = oc.x * ray_dir.x + oc.y * ray_dir.y + oc.z * ray_dir.z;
                    const float cc = oc.x * oc.x + oc.y * oc.y + oc.z * oc.z - kLightPickR * kLightPickR;
                    const float disc = b * b - cc;
                    if (disc < 0.0F)
                        continue;
                    const float t = -b - std::sqrt(disc);
                    if (t > 0.0F && t < best_light_t)
                    {
                        best_light_t = t;
                        best_light = static_cast<int>(i);
                    }
                }
                if (best_light >= 0)
                {
                    selected = best_light;
                    selected_kind = SelKind::kLight;
                    log_push("[pick] selected light " + lights[static_cast<std::size_t>(best_light)].name);
                }
                else if (best_i >= 0)
                {
                    selected = best_i;
                    selected_kind = SelKind::kEntity;
                    log_push("[pick] selected " + entities[static_cast<std::size_t>(best_i)].name);
                }
                else
                {
                    // Empty-space click ??' unselect.
                    if (selected >= 0)
                    {
                        log_push("[pick] cleared selection");
                        selected = -1;
                    }
                }
            }
        }

        // ---- Begin GPU frame ----
        auto frame_r = renderer.begin_frame();
        if (!frame_r.has_value())
        {
            if (frame_r.error().code ==
                static_cast<std::uint32_t>(cd::render::render_errors::Code::kSwapchainOutOfDate))
            {
                needs_rebuild = true;
                continue;
            }
            return 10;
        }
        auto& frame = *frame_r;
        auto& cmd = *frame.command_buffer;

        // ---- Faz 1.7 - per-frame TLAS rebuild ----
        // 1) tick deferred destroy queue (TLAS handles older than 3
        //    frames are guaranteed past the in-flight window),
        // 2) collect instances (ECS entities + sphere grid + floor),
        // 3) create + build the TLAS on this frame's cmd buffer,
        // 4) defer destroy of the previous frame's TLAS,
        // 5) update the prim_inst descriptor binding 2 to the new TLAS.
        while (!tlas_destroy_queue.empty() && tlas_destroy_queue.front().destroy_at_frame <= frame_idx)
        {
            device.destroy_acceleration_structure(tlas_destroy_queue.front().h);
            tlas_destroy_queue.pop_front();
        }
        {
            std::vector<cd::rhi::AccelInstance> instances;
            instances.reserve(entities.size() + 25 + 1);
            // W8-BC parallel material array - filled in lockstep with
            //  so the GPU rayQueryGetIntersectionInstanceIdEXT
            // result indexes the right slot. Floor and the skinned
            // gltf BLAS land here too (gltf entity's tint).
            std::vector<InstanceMatGpu> inst_mats;
            inst_mats.reserve(entities.size() + 1);
            auto push_inst =
                [&](cd::rhi::AccelStructureHandle blas, const cd::math::Mat4f& m, const cd::math::Vec3f& albedo)
            {
                if (!blas.is_valid())
                    return;
                instances.push_back(cd::hello_engine::make_accel_instance(blas, m));
                InstanceMatGpu im {};
                cd::hello_engine::fill_inst_mat(im, albedo);
                inst_mats.push_back(im);
            };
            // W8-AV: PBR spheres back in TLAS as RT occluders too.
            // The W8-AU skip + the legacy hardcoded 5x5 push were two
            // separate problems — the W8-AU skip turned out to also
            // disable the legit RT shadows the user wanted (chrome
            // sphere casting shadow on the floor under the area
            // light), so undo the skip. The legacy hardcoded grid
            // stays removed (it was duplicate occluder geometry at
            // pre-W8-AR coordinates).
            //
            // X1B (phase 284): parallel TLAS instance build via
            // cd::concurrency::parallel_for. Per-entity slot is written
            // by index into pre-sized scratch arrays (no push_back from
            // worker threads); a serial compaction step collects valid
            // slots into the final instances/inst_mats arrays so the
            // floor and skinned glTF tail stays in deterministic order
            // and the GPU instance-index correspondence is preserved.
            const std::size_t kEntCount = entities.size();
            std::vector<cd::rhi::AccelInstance> ent_inst_scratch(kEntCount);
            std::vector<InstanceMatGpu> ent_mat_scratch(kEntCount);
            std::vector<std::uint8_t> ent_valid(kEntCount, 0u);
            cd::concurrency::parallel_for(
                std::size_t { 0 },
                kEntCount,
                [&](std::size_t i)
                {
                    const auto& ent = entities[i];
                    auto* lt = scene.local(ent.handle);
                    if (lt == nullptr)
                        return;
                    const auto blas = blas_for_kind(ent.kind);
                    if (!blas.is_valid())
                        return;
                    const auto m = cd::math::to_mat4(lt->value);
                    ent_inst_scratch[i] = cd::hello_engine::make_accel_instance(blas, m);
                    InstanceMatGpu im {};
                    cd::hello_engine::fill_inst_mat(im, ent.tint);
                    ent_mat_scratch[i] = im;
                    ent_valid[i] = 1u;
                }
            );
            // Serial compaction preserves entity ordering so the GPU
            // instanceCustomIndex lookup into inst_mat_ssbo stays aligned
            // with the TLAS hit's instance id.
            for (std::size_t i = 0; i < kEntCount; ++i)
            {
                if (ent_valid[i] == 0u)
                    continue;
                instances.push_back(ent_inst_scratch[i]);
                inst_mats.push_back(ent_mat_scratch[i]);
            }
            // Floor: identity scale, y = kFloorY (matches the floor draw).
            // W8-BC: distinct neutral grey so chrome reflections show a
            // proper grey floor, not garbage or a wrong entity tint.
            {
                cd::math::Mat4f fm = cd::math::Mat4f::identity();
                fm[3][1] = -0.55F;
                push_inst(blas_floor, fm, cd::math::Vec3f { 0.5F, 0.5F, 0.5F });
            }
            // Phase 251 — refresh the skinned BLAS so RT shadow rays
            // trace against the current animation pose instead of the
            // bind pose. CPU-LBS already re-uploaded gltf_mesh.vb
            // earlier in this frame; the BLAS storage + scratch were
            // sized for the original triangle count (unchanged), so
            // an in-place rebuild via vkCmdBuildAccelerationStructuresKHR
            // (MODE_BUILD_KHR with the same dst handle) overwrites the
            // BLAS contents from the freshly-skinned vertex data. We
            // then issue an AS-build → AS-build memory barrier so the
            // TLAS build (which dereferences blas device addresses)
            // observes the updated BLAS rather than racing the write.
            // Static-geometry BLAS (cube/sphere/etc.) stay at bind
            // build from boot — only the animated gltf BLAS needs the
            // refresh.
            if (skinned.valid && blas_gltf.is_valid())
            {
                cmd.build_acceleration_structure(blas_gltf);
                cmd.acceleration_structure_barrier();
            }

            cd::rhi::AccelStructureDesc tld {};
            tld.kind = cd::rhi::AccelStructureKind::kTopLevel;
            tld.instances = std::span<const cd::rhi::AccelInstance>(instances);
            tld.debug_name = "tlas_frame";
            auto new_r = device.create_acceleration_structure(tld);
            if (new_r.has_value())
            {
                cmd.build_acceleration_structure(*new_r);
                if (current_tlas.is_valid())
                    tlas_destroy_queue.push_back({ current_tlas, frame_idx + 3 });
                current_tlas = *new_r;
                std::array<cd::rhi::DescriptorWrite, 1> tlas_writes {
                    cd::rhi::DescriptorWrite { .binding = 2,
                                              .array_element = 0,
                                              .type = cd::rhi::DescriptorType::kAccelerationStructure,
                                              .accel = current_tlas }
                };
                (void)prim_inst.update(tlas_writes);
            }
            // W8-BC: upload the per-frame instance materials. Clamp to
            // the SSBO capacity (defensive - kMaxInstMats = 256 dwarfs
            // current entity count, but futureproof). Re-issue the
            // binding-10 descriptor write each frame so the GPU sees
            // the freshly uploaded contents even if the underlying
            // buffer handle stays put.
            if (!inst_mats.empty())
            {
                const std::uint32_t n =
                    std::min<std::uint32_t>(static_cast<std::uint32_t>(inst_mats.size()), kMaxInstMats);
                const std::size_t bytes = static_cast<std::size_t>(n) * sizeof(InstanceMatGpu);
                (void)device.upload_buffer(
                    inst_mat_ssbo,
                    0,
                    std::span<const std::byte>(reinterpret_cast<const std::byte*>(inst_mats.data()), bytes)
                );
                std::array<cd::rhi::DescriptorWrite, 1> ssbo_writes {
                    cd::rhi::DescriptorWrite { .binding = 10,
                                              .array_element = 0,
                                              .type = cd::rhi::DescriptorType::kStorageBuffer,
                                              .buffer = inst_mat_ssbo,
                                              .buffer_offset = 0,
                                              .buffer_range = kInstMatBytes }
                };
                (void)prim_inst.update(ssbo_writes);
            }
        }

        if (!depth_initialised_on_gpu)
        {
            std::array<cd::rhi::TextureBarrier, 1> db {
                cd::rhi::TextureBarrier {
                                         .texture = depth.image,
                                         .from = cd::rhi::ResourceState::kUndefined,
                                         .to = cd::rhi::ResourceState::kDepthWrite,
                                         .range = { .base_mip = 0, .mip_count = 1, .base_layer = 0, .layer_count = 1 } }
            };
            cmd.barrier({}, db);
            depth_initialised_on_gpu = true;
        }
        else
        {
            // Subsequent frames: composite-pass GTAO sampled the depth
            // target as ShaderResource at the end of the prior frame;
            // bring it back to kDepthWrite before the HDR scene pass.
            std::array<cd::rhi::TextureBarrier, 1> db {
                cd::rhi::TextureBarrier {
                                         .texture = depth.image,
                                         .from = cd::rhi::ResourceState::kShaderResource,
                                         .to = cd::rhi::ResourceState::kDepthWrite,
                                         .range = { .base_mip = 0, .mip_count = 1, .base_layer = 0, .layer_count = 1 } }
            };
            cmd.barrier({}, db);
        }

        // ---- Shadow map pass (Faz 1.6 CSM) ----
        // Pick the first enabled directional light for the shadow caster.
        // No directional ??' shadow map is cleared to white (no shadow).
        cd::math::Vec3f csm_sun_dir { -0.4F, -0.9F, -0.2F };
        bool csm_has_sun = false;
        for (const auto& lrow : lights)
        {
            if (!lrow.enabled)
                continue;
            if (lrow.light.type != cd::light::LightType::kDirectional)
                continue;
            csm_sun_dir = lrow.light.direction;
            csm_has_sun = true;
            break;
        }
        // Build the sun's view + ortho. Eye placed -30 m along the
        // ray, looking at origin. Up vector flips to +Z when the sun
        // is nearly vertical to avoid the look_at degeneracy.
        {
            cd::math::Vec3f sd = csm_sun_dir;
            // Normalize defensively in case the slider produced a tiny
            // vector before renormalize fired.
            const float sd_len = std::sqrt(sd.x * sd.x + sd.y * sd.y + sd.z * sd.z);
            if (sd_len > 1e-4F)
            {
                sd.x /= sd_len;
                sd.y /= sd_len;
                sd.z /= sd_len;
            }
            else
            {
                sd = { 0.0F, -1.0F, 0.0F };
            }
            const cd::math::Vec3f eye { -sd.x * 30.0F, -sd.y * 30.0F, -sd.z * 30.0F };
            const cd::math::Vec3f tgt { 0.0F, 0.0F, 0.0F };
            const cd::math::Vec3f up =
                (std::fabs(sd.y) > 0.99F) ? cd::math::Vec3f { 0.0F, 0.0F, 1.0F } : cd::math::Vec3f { 0.0F, 1.0F, 0.0F };
            const auto light_view = cd::math::look_at(eye, tgt, up);
            const auto light_proj = cd::math::ortho(-25.0F, 25.0F, -25.0F, 25.0F, 0.1F, 60.0F);
            const cd::math::Mat4f light_vp = light_proj * light_view;
            // Upload to UBO (kCpuToGpu, no staging).
            (void)device.upload_buffer(
                shadow_ubo,
                0,
                std::span<const std::byte>(reinterpret_cast<const std::byte*>(&light_vp), sizeof(light_vp))
            );
        }

        // First-frame transition for the shadow target.
        if (!shadow_initialised_on_gpu)
        {
            std::array<cd::rhi::TextureBarrier, 1> sb {
                cd::rhi::TextureBarrier {
                                         .texture = shadow_target.image,
                                         .from = cd::rhi::ResourceState::kUndefined,
                                         .to = cd::rhi::ResourceState::kDepthWrite,
                                         .range = { .base_mip = 0, .mip_count = 1, .base_layer = 0, .layer_count = 1 } }
            };
            cmd.barrier({}, sb);
            shadow_initialised_on_gpu = true;
        }
        else
        {
            // Subsequent frames: shader-resource ??' depth-write.
            std::array<cd::rhi::TextureBarrier, 1> sb {
                cd::rhi::TextureBarrier {
                                         .texture = shadow_target.image,
                                         .from = cd::rhi::ResourceState::kShaderResource,
                                         .to = cd::rhi::ResourceState::kDepthWrite,
                                         .range = { .base_mip = 0, .mip_count = 1, .base_layer = 0, .layer_count = 1 } }
            };
            cmd.barrier({}, sb);
        }
        {
            cd::rhi::DepthStencilAttachmentInfo sda {};
            sda.view = shadow_target.view;
            sda.depth_load = cd::rhi::LoadOp::kClear;
            sda.depth_store = cd::rhi::StoreOp::kStore;
            sda.clear.depth = 1.0F;
            cd::rhi::RenderPassBeginInfo srp {};
            srp.render_area = cd::rhi::Rect2D {
                { 0, 0 },
                kShadowMapSize
            };
            srp.color_attachments = {};
            srp.depth_stencil = &sda;
            cmd.begin_render_pass(srp);
            cmd.set_viewport(
                cd::rhi::Viewport { 0.0F,
                                    0.0F,
                                    static_cast<float>(kShadowMapSize.width),
                                    static_cast<float>(kShadowMapSize.height),
                                    0.0F,
                                    1.0F }
            );
            cmd.set_scissor(
                cd::rhi::Rect2D {
                    { 0, 0 },
                    kShadowMapSize
            }
            );
            if (csm_has_sun)
            {
                shadow_material.apply(cmd);
                // Rebuild light_vp into a local - we already uploaded but
                // also need it as a CPU-side push for the per-caster
                // light_mvp computation. Re-derive (cheap).
                cd::math::Vec3f sd = csm_sun_dir;
                const float sl = std::sqrt(sd.x * sd.x + sd.y * sd.y + sd.z * sd.z);
                if (sl > 1e-4F)
                {
                    sd.x /= sl;
                    sd.y /= sl;
                    sd.z /= sl;
                }
                else
                {
                    sd = { 0.0F, -1.0F, 0.0F };
                }
                const cd::math::Vec3f eye { -sd.x * 30.0F, -sd.y * 30.0F, -sd.z * 30.0F };
                const cd::math::Vec3f tgt { 0.0F, 0.0F, 0.0F };
                const cd::math::Vec3f up = (std::fabs(sd.y) > 0.99F) ? cd::math::Vec3f { 0.0F, 0.0F, 1.0F }
                                                                     : cd::math::Vec3f { 0.0F, 1.0F, 0.0F };
                const auto light_view2 = cd::math::look_at(eye, tgt, up);
                const auto light_proj2 = cd::math::ortho(-25.0F, 25.0F, -25.0F, 25.0F, 0.1F, 60.0F);
                const cd::math::Mat4f light_vp2 = light_proj2 * light_view2;
                // Casters: each ECS entity (using its mesh+transform).
                // W8-AV: re-enable PBR sphere CSM casting. The "huge
                // black blobs" the user saw before W8-AT were caused
                // by the W8-AS altitude bump (y up to 5.75) producing
                // very long shadow-map texel projections. With the
                // W8-AV altitude reset (y up to 3.05) shadows are
                // normal-sized again.
                // X1E (phase 287): parallel CSM caster prep. The
                // light_mvp per entity is computed via parallel_for
                // into a pre-sized scratch vector; draw pass binds +
                // pushes + draws serially (Vulkan cmd recording is not
                // thread-safe per buffer).
                std::vector<cd::math::Mat4f> csm_light_mvp(entities.size());
                std::vector<std::uint8_t> csm_valid(entities.size(), 0u);
                cd::concurrency::parallel_for(
                    std::size_t { 0 },
                    entities.size(),
                    [&](std::size_t i)
                    {
                        const auto& ent = entities[i];
                        const auto& mesh = mesh_for(ent.kind);
                        if (!mesh.vb.is_valid())
                            return;
                        auto* lt = scene.local(ent.handle);
                        if (lt == nullptr)
                            return;
                        const auto model = cd::math::to_mat4(lt->value);
                        csm_light_mvp[i] = light_vp2 * model;
                        csm_valid[i] = 1u;
                    }
                );
                for (std::size_t i = 0; i < entities.size(); ++i)
                {
                    if (csm_valid[i] == 0u)
                        continue;
                    const auto& ent = entities[i];
                    const auto& mesh = mesh_for(ent.kind);
                    cmd.bind_vertex_buffer(0, mesh.vb, 0);
                    cmd.bind_index_buffer(mesh.ib, 0, cd::rhi::IndexType::kUInt16);
                    cmd.push_constants(
                        shadow_material.pipeline_layout(),
                        cd::rhi::ShaderStage::kVertex,
                        0,
                        sizeof(cd::math::Mat4f),
                        &csm_light_mvp[i]
                    );
                    cmd.draw_indexed(mesh.index_count, 1, 0, 0, 0);
                }
                // W8-AS: leftover 5x5 PBR-grid CSM caster loop REMOVED.
                // The entity caster loop above already drew the 16 PBR
                // sphere ECS entities into the shadow map — drawing the
                // legacy hardcoded grid again created the ghost shadow
                // the user reported ("eski pbrlarin gölgesi gozukuyor").
            }
            cmd.end_render_pass();
        }
        // Transition back to shader-resource for main pass sampling.
        {
            std::array<cd::rhi::TextureBarrier, 1> sb {
                cd::rhi::TextureBarrier {
                                         .texture = shadow_target.image,
                                         .from = cd::rhi::ResourceState::kDepthWrite,
                                         .to = cd::rhi::ResourceState::kShaderResource,
                                         .range = { .base_mip = 0, .mip_count = 1, .base_layer = 0, .layer_count = 1 } }
            };
            cmd.barrier({}, sb);
        }

        // R3: scene draws into the HDR + G-Buffer normal off-screen
        // targets; composite + ImGui write to the swapchain in a
        // follow-up render pass. Transition both to ColorAttachment
        // on first use; subsequent frames re-enter from kShaderResource
        // (composite sampled them last frame).
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
        std::array<cd::rhi::ColorAttachmentInfo, 4> color_attach {
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
        cd::rhi::DepthStencilAttachmentInfo depth_attach {};
        depth_attach.view = depth.view;
        depth_attach.depth_load = cd::rhi::LoadOp::kClear;
        depth_attach.depth_store = cd::rhi::StoreOp::kStore;
        depth_attach.clear.depth = 1.0F;

        cd::rhi::RenderPassBeginInfo rp {};
        rp.render_area = cd::rhi::Rect2D {
            { 0, 0 },
            frame.extent
        };
        rp.color_attachments = color_attach;
        rp.depth_stencil = &depth_attach;
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

        const float aspect = static_cast<float>(frame.extent.width) / static_cast<float>(frame.extent.height);
        cd::math::Mat4f vp_unjittered = cd::camera::view_projection(cam, aspect);

        // R3 Halton(2,3) sub-pixel jitter for proper TAA accumulation
        // - only active when TAA is dialled in. cd::post_taa owns the
        // Halton sequence; we just gate it on the TAA strength dial.
        const cd::math::Vec2f jitter_px =
            (fx_taa_amount > 0.001F) ? cd::post_taa::jitter_offset(frame_idx, 8U) : cd::math::Vec2f { 0.0F, 0.0F };
        const float jx_ndc = jitter_px.x * 2.0F / static_cast<float>(frame.extent.width);
        const float jy_ndc = jitter_px.y * 2.0F / static_cast<float>(frame.extent.height);

        // T_jitter * vp - adds jx_ndc * w to clip.x so post-divide
        // ndc.x shifts by jx_ndc. Column-major: for each column c,
        // add the bottom-row entry * jitter into rows 0/1.
        cd::math::Mat4f vp = vp_unjittered;
        for (std::size_t c = 0; c < 4; ++c)
        {
            vp[c][0] += jx_ndc * vp_unjittered[c][3];
            vp[c][1] += jy_ndc * vp_unjittered[c][3];
        }

        // ---- Sky pass ----
        cd::math::Vec3f forward { cam.target.x - cam.eye.x, cam.target.y - cam.eye.y, cam.target.z - cam.eye.z };
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
        // Phase G - sky pulls sun direction + intensity + color from
        // the first enabled directional light. CCT slider in the
        // Lights panel now affects the SKY tint too (sunset feel at
        // 2000-3000K, neutral at D65, cold blue at 10000K).
        // Defaults must be ZERO so disabling every directional light
        // leaves the sky truly dark - the prior 0.9 default caused
        // the 'all-lights-off => bright white sky' bug.
        cd::math::Vec3f sky_sun_dir { -0.4F, -0.6F, -0.7F };
        cd::math::Vec3f sky_sun_col { 0.0F, 0.0F, 0.0F };
        float sky_sun_strength = 0.0F;
        for (const auto& lrow : lights)
        {
            if (!lrow.enabled)
                continue;
            if (lrow.light.type != cd::light::LightType::kDirectional)
                continue;
            sky_sun_dir = lrow.light.direction;
            sky_sun_col = lrow.light.color;
            sky_sun_strength = std::min(2.5F, lrow.light.intensity / 80000.0F);
            break;
        }
        spush.sun_dir[0] = sky_sun_dir.x;
        spush.sun_dir[1] = sky_sun_dir.y;
        spush.sun_dir[2] = sky_sun_dir.z;
        spush.sun_dir[3] = sky_sun_strength;
        spush.sun_color[0] = sky_sun_col.x;
        spush.sun_color[1] = sky_sun_col.y;
        spush.sun_color[2] = sky_sun_col.z;
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

        // W8-AR: dedicated PBR-grid draw block REMOVED. The 16 PBR demo
        // spheres are ECS entities now (boot block, search "is_pbr =
        // true") and are drawn by the unified entity loop below alongside
        // every other primitive. One render path, one shader, one shadow
        // pass — PBR is just a per-entity attribute (SceneEntity::is_pbr).

        // ---- ECS entity primitives row (front of the viewport) ----
        // Per-fragment lighting now: sun + first enabled point light with
        // distance attenuation. So rotating/moving an entity (or moving
        // a light) updates its shading correctly.
        // Lights-off baseline: NOTHING contributes. Defaults are
        // intentionally zeroed so the scene goes to (near-)black when
        // every light is disabled - user feedback: "isik yoksa golge
        // yada isik beklemem". The for-loop below promotes the first
        // enabled directional to the sun slot; absent that, sun_str
        // stays 0 and the FS sun term contributes nothing.
        cd::math::Vec3f sun_dir { 0.0F, -1.0F, 0.0F };
        cd::math::Vec3f sun_col { 0.0F, 0.0F, 0.0F };
        float sun_str = 0.0F;
        float ambient_w = 0.0F;
        bool has_sun = false;
        for (const auto& lrow : lights)
        {
            if (!lrow.enabled)
                continue;
            if (lrow.light.type != cd::light::LightType::kDirectional)
                continue;
            sun_dir = lrow.light.direction;
            sun_col = lrow.light.color;
            sun_str = std::min(2.5F, lrow.light.intensity / 80000.0F);
            // Sky hemisphere tied to sun being enabled: no sun, no
            // sky bounce - the universe is dark.
            ambient_w = 0.18F;
            has_sun = true;
            break;
        }
        (void)has_sun;
        // ---- Multi-light UBO fill (gap #2) ----
        // Walk every enabled non-sun light and pack into the
        // descriptor-bound UBO. Up to kMaxLights (8) slots; extras
        // drop silently (logged once via the counter).
        {
            LightUboGpu ubo {};
            ubo.count = 0;
            for (const auto& lrow : lights)
            {
                if (!lrow.enabled)
                    continue;
                if (ubo.count >= cd::hello_engine::kMaxLights)
                    break;
                // pack_light_slot returns false for directional lights
                // (sun is driven by PrimPush.sun_dir, not the multi-light UBO).
                if (!pack_light_slot(ubo.slots[ubo.count], lrow.light))
                    continue;
                ++ubo.count;
            }
            (void)device.upload_buffer(
                lights_ubo,
                0,
                std::span<const std::byte>(reinterpret_cast<const std::byte*>(&ubo), sizeof(ubo))
            );
            counters.set("lights_active", ubo.count);
        }

        prim_material.apply(cmd);
        prim_inst.bind(cmd, 0);  // Faz 1.6 CSM + Faz 1.9 light UBO

        // ---- Floor (large flat quad) ----
        // Faz 1.5 - real geometry on which the planar-shadow pass can
        // project caster silhouettes. Floor sits at y = kFloorY so the
        // front-row primitives (which extend ??0.5 m around y=0) just
        // touch it.
        constexpr float kFloorY = -0.55F;
        constexpr float kShadowLift = 0.01F;
        {
            cmd.bind_vertex_buffer(0, floor_mesh.vb, 0);
            cmd.bind_index_buffer(floor_mesh.ib, 0, cd::rhi::IndexType::kUInt16);
            cd::math::Mat4f floor_model = cd::math::Mat4f::identity();
            floor_model[3][1] = kFloorY;  // translate quad to y = kFloorY
            const auto floor_mvp = vp * floor_model;
            PrimPush fp {};
            fp.mvp = floor_mvp;
            fp.model = floor_model;
            // Slightly cool neutral floor - receives lighting + hemisphere AO.
            // tint[3] = 2.0 is the FS sentinel that enables the analytic
            // grid overlay (depth-tested via the floor geometry, so the
            // grid no longer shows through other objects).
            // Shadow-catcher + grid-helper combo (gaps #20 + #21).
            // Floor body colour kept subtle so the plane reads more
            // like an editor helper than a scene mesh - shadows
            // (much darker, see planar-shadow tint below) and grid
            // lines (much brighter) both stand out against it. FS
            // also fades the floor with camera distance for a
            // pseudo-infinite-grid feel pending the real procedural-
            // grid helper in v1.6 editor.
            fp.tint[0] = 0.15F;
            fp.tint[1] = 0.16F;
            fp.tint[2] = 0.18F;
            fp.tint[3] = 2.0F;
            fp.sun_dir[0] = sun_dir.x;
            fp.sun_dir[1] = sun_dir.y;
            fp.sun_dir[2] = sun_dir.z;
            fp.sun_dir[3] = sun_str;
            fp.sun_color[0] = sun_col.x;
            fp.sun_color[1] = sun_col.y;
            fp.sun_color[2] = sun_col.z;
            fp.sun_color[3] = ambient_w;
            fp.fx_params[0] = static_cast<float>(tonemap_op);
            fp.fx_params[1] = 0.0F;
            // Floor opts out of GTAO crease darkening - its normal is
            // flat so dFdx/dFdy returns zero, but bloom on bright grid
            // lines is a nice subtle highlight.
            fp.fx_params[2] = 0.0F;
            fp.fx_params[3] = fx_bloom_strength;
            fp.fx_params2[0] = fx_smaa_strength;
            fp.fx_params2[1] = fx_motion_blur;
            fp.fx_params2[2] = fx_taa_amount;
            fp.fx_params2[3] = fx_dof_strength;
            fp.fx_params3[0] = fx_fog_density;
            fp.fx_params3[1] = fx_aerial_perspective;
            fp.fx_params3[2] = fx_clouds_coverage;
            fp.fx_params3[3] = fx_light_shafts;
            fp.camera_pos[0] = cam.eye.x;
            fp.camera_pos[1] = cam.eye.y;
            fp.camera_pos[2] = cam.eye.z;
            fp.camera_pos[3] = 0.0F;
            fp.fx_params4[0] = fp.fx_params4[1] = fp.fx_params4[2] = 0.0F;
            fp.fx_params4[3] = static_cast<float>(fx_view_mode);
            cmd.push_constants(
                prim_material.pipeline_layout(),
                cd::rhi::ShaderStage::kVertex | cd::rhi::ShaderStage::kFragment,
                0,
                sizeof(fp),
                &fp
            );
            cmd.draw_indexed(floor_mesh.index_count, 1, 0, 0, 0);
            counters.increment("draws_prim");
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
        std::vector<PrimPush> ent_push_scratch(entities.size());
        std::vector<std::uint8_t> ent_push_valid(entities.size(), 0u);
        cd::concurrency::parallel_for(
            std::size_t { 0 },
            entities.size(),
            [&](std::size_t i)
            {
                const auto& ent = entities[i];
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
                // W8-AR sentinel routing (see pre-X1D comment for the
                // full rationale): tint.w==3.0 routes through Cook-
                // Torrance, 1.0 stays on the standard Lambert + textured
                // path.
                pp.tint[0] = ent.tint.x;
                pp.tint[1] = ent.tint.y;
                pp.tint[2] = ent.tint.z;
                pp.tint[3] = ent.is_pbr ? 3.0F : 1.0F;
                pp.sun_dir[0] = sun_dir.x;
                pp.sun_dir[1] = sun_dir.y;
                pp.sun_dir[2] = sun_dir.z;
                pp.sun_dir[3] = sun_str;
                pp.sun_color[0] = sun_col.x;
                pp.sun_color[1] = sun_col.y;
                pp.sun_color[2] = sun_col.z;
                pp.sun_color[3] = ambient_w;
                pp.fx_params[0] = static_cast<float>(tonemap_op);
                pp.fx_params[1] = (!ent.is_pbr && ent.kind == PrimitiveKind::kGltf && has_gltf_texture) ? 1.0F : 0.0F;
                pp.fx_params[2] = fx_gtao_strength;
                pp.fx_params[3] = fx_bloom_strength;
                pp.fx_params2[0] = fx_smaa_strength;
                pp.fx_params2[1] = fx_motion_blur;
                pp.fx_params2[2] = fx_taa_amount;
                pp.fx_params2[3] = fx_dof_strength;
                pp.fx_params3[0] = fx_fog_density;
                pp.fx_params3[1] = fx_aerial_perspective;
                pp.fx_params3[2] = fx_clouds_coverage;
                pp.fx_params3[3] = fx_light_shafts;
                pp.camera_pos[0] = cam.eye.x;
                pp.camera_pos[1] = cam.eye.y;
                pp.camera_pos[2] = cam.eye.z;
                pp.camera_pos[3] = 0.0F;
                if (ent.is_pbr)
                {
                    pp.fx_params4[0] = ent.metallic;
                    pp.fx_params4[1] = ent.roughness;
                    pp.fx_params4[2] = 0.0F;
                }
                else
                {
                    pp.fx_params4[0] = fx_clearcoat_strength;
                    pp.fx_params4[1] = fx_sheen_strength;
                    pp.fx_params4[2] = fx_sss_strength;
                }
                pp.fx_params4[3] = static_cast<float>(fx_view_mode);
                ent_push_valid[i] = 1u;
            }
        );
        for (std::size_t i = 0; i < entities.size(); ++i)
        {
            if (ent_push_valid[i] == 0u)
                continue;
            const auto& ent = entities[i];
            const auto& mesh = mesh_for(ent.kind);
            cmd.bind_vertex_buffer(0, mesh.vb, 0);
            cmd.bind_index_buffer(mesh.ib, 0, cd::rhi::IndexType::kUInt16);
            cmd.push_constants(
                prim_material.pipeline_layout(),
                cd::rhi::ShaderStage::kVertex | cd::rhi::ShaderStage::kFragment,
                0,
                sizeof(PrimPush),
                &ent_push_scratch[i]
            );
            cmd.draw_indexed(mesh.index_count, 1, 0, 0, 0);
            counters.increment("draws_prim");
        }

        // ---- Planar projective shadows (Faz 1.5) ----
        // For each caster (ECS entities + 5?-5 PBR sphere grid), build a
        // shadow projection matrix that flattens the geometry onto the
        // floor plane along the sun direction, then redraw with the
        // tint.w sentinel that triggers the shader's shadow-bypass
        // (flat dark output, no lighting). Hard shadows - soft shadows
        // need alpha blending in MaterialDesc (Faz 1.6 / future work).
        // Skips when sun is disabled or pointing upward.
        if (sun_str > 1e-4F && sun_dir.y < -1e-3F)
        {
            const auto S = make_planar_shadow_matrix(sun_dir, kFloorY, kShadowLift);
            PrimPush sp {};
            // Shadow tint: tint.w < 0.5 triggers shader bypass; rgb is the
            // shadow color (linear, post-tonemap output).
            sp.tint[0] = 0.04F;
            sp.tint[1] = 0.04F;
            sp.tint[2] = 0.05F;
            sp.tint[3] = 0.0F;
            // Zero out lighting fields - shadow path doesn't read them
            // but keep the push deterministic for SPIR-V validators.
            sp.sun_dir[0] = sp.sun_dir[1] = sp.sun_dir[2] = sp.sun_dir[3] = 0.0F;
            sp.sun_color[0] = sp.sun_color[1] = sp.sun_color[2] = sp.sun_color[3] = 0.0F;
            sp.fx_params[0] = sp.fx_params[1] = sp.fx_params[2] = sp.fx_params[3] = 0.0F;
            sp.fx_params2[0] = sp.fx_params2[1] = sp.fx_params2[2] = sp.fx_params2[3] = 0.0F;
            sp.fx_params3[0] = sp.fx_params3[1] = sp.fx_params3[2] = sp.fx_params3[3] = 0.0F;
            sp.camera_pos[0] = sp.camera_pos[1] = sp.camera_pos[2] = sp.camera_pos[3] = 0.0F;
            sp.fx_params4[0] = sp.fx_params4[1] = sp.fx_params4[2] = sp.fx_params4[3] = 0.0F;

            // X1E (phase 287): parallel planar shadow caster prep.
            // Each entity's shadow_model + mvp is computed via
            // parallel_for; sp stays a constant template per entity
            // (only mvp/model vary), the draw pass uploads per-entity
            // sp through push_constants. W8-AV: PBR sphere planar
            // shadows re-enabled, see CSM caster comment for rationale.
            std::vector<PrimPush> plan_push(entities.size());
            std::vector<std::uint8_t> plan_valid(entities.size(), 0u);
            cd::concurrency::parallel_for(
                std::size_t { 0 },
                entities.size(),
                [&](std::size_t i)
                {
                    const auto& ent = entities[i];
                    const auto& mesh = mesh_for(ent.kind);
                    if (!mesh.vb.is_valid())
                        return;
                    auto* lt = scene.local(ent.handle);
                    if (lt == nullptr)
                        return;
                    const auto model = cd::math::to_mat4(lt->value);
                    const auto shadow_model = S * model;
                    PrimPush& dst = plan_push[i];
                    dst = sp;
                    dst.mvp = vp * shadow_model;
                    dst.model = shadow_model;
                    plan_valid[i] = 1u;
                }
            );
            for (std::size_t i = 0; i < entities.size(); ++i)
            {
                if (plan_valid[i] == 0u)
                    continue;
                const auto& ent = entities[i];
                const auto& mesh = mesh_for(ent.kind);
                cmd.bind_vertex_buffer(0, mesh.vb, 0);
                cmd.bind_index_buffer(mesh.ib, 0, cd::rhi::IndexType::kUInt16);
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

            // W8-AR: dedicated PBR-grid shadow caster loop REMOVED.
            // The 16 PBR sphere entities now cast shadows through the
            // entity-casters loop above (they're regular ECS entities).
        }

        // ---- ImGui frame ----
        ctx.new_frame();

        // Palette hotkeys via ImGui (after new_frame so IO modifier
        // state is current). Multiple combos because user reported
        // Ctrl+Shift+P sometimes not firing - IME / global keyboard
        // hooks can intercept the chord. F2 + GraveAccent + the chord
        // all toggle, any one works.
        if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_P) ||
            ImGui::IsKeyChordPressed(ImGuiKey_F2) || ImGui::IsKeyChordPressed(ImGuiKey_GraveAccent))
        {
            palette_visible = !palette_visible;
            if (palette_visible)
                palette_query.clear();
        }

        // DockSpace host.
        {
            const ImGuiViewport* main_vp = ImGui::GetMainViewport();
            ImGui::SetNextWindowPos(main_vp->WorkPos);
            ImGui::SetNextWindowSize(main_vp->WorkSize);
            ImGui::SetNextWindowViewport(main_vp->ID);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0F);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0F);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2 { 0, 0 });
            ImGui::Begin(
                "##cd_dockhost",
                nullptr,
                ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
                    ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus |
                    ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_NoBackground
            );
            ImGui::PopStyleVar(3);
            const ImGuiID dock_id = ImGui::GetID("cd_engine_dock");
            if (!dock_initialised && ImGui::DockBuilderGetNode(dock_id) == nullptr)
            {
                ImGui::DockBuilderRemoveNode(dock_id);
                const int flags = static_cast<int>(ImGuiDockNodeFlags_DockSpace) |
                                  static_cast<int>(ImGuiDockNodeFlags_PassthruCentralNode);
                ImGui::DockBuilderAddNode(dock_id, static_cast<ImGuiDockNodeFlags>(flags));
                ImGui::DockBuilderSetNodeSize(dock_id, main_vp->WorkSize);
                ImGuiID m = dock_id;
                ImGuiID dock_left = ImGui::DockBuilderSplitNode(m, ImGuiDir_Left, 0.16F, nullptr, &m);
                ImGuiID dock_right = ImGui::DockBuilderSplitNode(m, ImGuiDir_Right, 0.25F, nullptr, &m);
                ImGuiID dock_bot = ImGui::DockBuilderSplitNode(m, ImGuiDir_Down, 0.30F, nullptr, &m);
                ImGuiID dock_botR = ImGui::DockBuilderSplitNode(dock_bot, ImGuiDir_Right, 0.50F, nullptr, &dock_bot);
                ImGui::DockBuilderDockWindow("Outliner", dock_left);
                ImGui::DockBuilderDockWindow("Scene", dock_left);
                ImGui::DockBuilderDockWindow("Inspector", dock_right);
                ImGui::DockBuilderDockWindow("Counters", dock_right);
                ImGui::DockBuilderDockWindow("Random", dock_right);
                ImGui::DockBuilderDockWindow("Audio", dock_bot);
                ImGui::DockBuilderDockWindow("Net Sim", dock_bot);
                ImGui::DockBuilderDockWindow("Streamer", dock_bot);
                ImGui::DockBuilderDockWindow("Lights", dock_right);
                ImGui::DockBuilderDockWindow("History", dock_botR);
                ImGui::DockBuilderFinish(dock_id);
                dock_initialised = true;
            }
            ImGui::DockSpace(dock_id, ImVec2 { 0, 0 }, ImGuiDockNodeFlags_PassthruCentralNode);
            ImGui::End();
        }

        // ---- Scene tree ----
        ImGui::Begin("Scene");
        ImGui::Text("Entities (%zu)", entities.size());
        ImGui::Separator();
        for (std::size_t i = 0; i < entities.size(); ++i)
        {
            const bool sel = (selected == static_cast<int>(i));
            char row[128] {};
            std::snprintf(row, sizeof(row), "%s##e%zu", entities[i].name.c_str(), i);
            if (ImGui::Selectable(row, sel))
                selected = static_cast<int>(i);
        }
        ImGui::Separator();
        ImGui::TextDisabled("Ctrl+Shift+P = command palette");
        ImGui::TextDisabled("Esc closes palette / quits");
        ImGui::End();

        // ---- Inspector ----
        ImGui::Begin("Inspector");
        if (selected >= 0 && selected < static_cast<int>(entities.size()))
        {
            auto& ent = entities[static_cast<std::size_t>(selected)];
            auto* lt = scene.local(ent.handle);
            if (lt != nullptr)
            {
                ImGui::Text("Entity: %s", ent.name.c_str());
                ImGui::TextColored(ImVec4(ent.tint.x, ent.tint.y, ent.tint.z, 1.0F), "tint preview");
                ImGui::Separator();

                ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x * 0.62F);

                // Position
                ImGui::SeparatorText("Position");
                {
                    static cd::math::Vec3f pre {};
                    float xyz[3] { lt->value.position.x, lt->value.position.y, lt->value.position.z };
                    bool changed = ImGui::DragFloat3("##pos", xyz, 0.05F, -10.0F, 10.0F, "%.3f");
                    if (ImGui::IsItemActivated())
                        pre = lt->value.position;
                    if (changed)
                        lt->value.position = { xyz[0], xyz[1], xyz[2] };
                    if (ImGui::IsItemDeactivatedAfterEdit())
                    {
                        cd::math::Vec3f delta { lt->value.position.x - pre.x,
                                                lt->value.position.y - pre.y,
                                                lt->value.position.z - pre.z };
                        if (delta.x != 0 || delta.y != 0 || delta.z != 0)
                        {
                            lt->value.position = pre;
                            history.push(std::make_unique<cd::editor::TranslateCommand>(scene, ent.handle, delta));
                            log_push("drag: Translate " + ent.name);
                        }
                    }
                }
                // Rotation (Euler XYZ in degrees). Converts to/from
                // quaternion every frame so the underlying Transform
                // stays canonical.
                ImGui::SeparatorText("Rotation (deg)");
                {
                    static cd::math::Quatf pre {};
                    const cd::math::Quatf& q = lt->value.rotation;
                    // Quat to Euler XYZ (radians) - small approximation
                    // works for inspector readout, gimbal-locked at
                    // pitch == 90 (rare for editor poses).
                    const float sinp = 2.0F * (q.w * q.x + q.y * q.z);
                    const float cosp = 1.0F - 2.0F * (q.x * q.x + q.y * q.y);
                    const float pitch = std::atan2(sinp, cosp);
                    float t2 = 2.0F * (q.w * q.y - q.z * q.x);
                    t2 = std::clamp(t2, -1.0F, 1.0F);
                    const float yaw = std::asin(t2);
                    const float siny = 2.0F * (q.w * q.z + q.x * q.y);
                    const float cosy = 1.0F - 2.0F * (q.y * q.y + q.z * q.z);
                    const float roll = std::atan2(siny, cosy);
                    constexpr float kRad2Deg = 57.2957795F;
                    float eul[3] { pitch * kRad2Deg, yaw * kRad2Deg, roll * kRad2Deg };
                    bool changed = ImGui::DragFloat3("##rot", eul, 1.0F, -180.0F, 180.0F, "%.1f");
                    if (ImGui::IsItemActivated())
                        pre = lt->value.rotation;
                    if (changed)
                    {
                        constexpr float kDeg2Rad = 0.01745329F;
                        // Rebuild quaternion from Euler XYZ (intrinsic).
                        const float cx = std::cos(eul[0] * kDeg2Rad * 0.5F);
                        const float sx = std::sin(eul[0] * kDeg2Rad * 0.5F);
                        const float cy = std::cos(eul[1] * kDeg2Rad * 0.5F);
                        const float sy = std::sin(eul[1] * kDeg2Rad * 0.5F);
                        const float cz = std::cos(eul[2] * kDeg2Rad * 0.5F);
                        const float sz = std::sin(eul[2] * kDeg2Rad * 0.5F);
                        lt->value.rotation = { sx * cy * cz - cx * sy * sz,
                                               cx * sy * cz + sx * cy * sz,
                                               cx * cy * sz - sx * sy * cz,
                                               cx * cy * cz + sx * sy * sz };
                    }
                    if (ImGui::IsItemDeactivatedAfterEdit())
                    {
                        // Compute delta-rotation = current * inverse(pre)
                        cd::math::Quatf cur = lt->value.rotation;
                        cd::math::Quatf inv_pre { -pre.x, -pre.y, -pre.z, pre.w };
                        cd::math::Quatf delta {
                            cur.w * inv_pre.x + cur.x * inv_pre.w + cur.y * inv_pre.z - cur.z * inv_pre.y,
                            cur.w * inv_pre.y - cur.x * inv_pre.z + cur.y * inv_pre.w + cur.z * inv_pre.x,
                            cur.w * inv_pre.z + cur.x * inv_pre.y - cur.y * inv_pre.x + cur.z * inv_pre.w,
                            cur.w * inv_pre.w - cur.x * inv_pre.x - cur.y * inv_pre.y - cur.z * inv_pre.z
                        };
                        const float mag =
                            std::abs(delta.x) + std::abs(delta.y) + std::abs(delta.z) + std::abs(1.0F - delta.w);
                        if (mag > 1e-4F)
                        {
                            lt->value.rotation = pre;
                            history.push(std::make_unique<cd::editor::RotateCommand>(scene, ent.handle, delta));
                            log_push("drag: Rotate " + ent.name);
                        }
                    }
                }
                // Scale
                ImGui::SeparatorText("Scale");
                {
                    static cd::math::Vec3f pre { 1, 1, 1 };
                    float xyz[3] { lt->value.scale.x, lt->value.scale.y, lt->value.scale.z };
                    bool changed = ImGui::DragFloat3("##sca", xyz, 0.02F, 0.05F, 5.0F, "%.3f");
                    if (ImGui::IsItemActivated())
                        pre = lt->value.scale;
                    if (changed)
                        lt->value.scale = { xyz[0], xyz[1], xyz[2] };
                    if (ImGui::IsItemDeactivatedAfterEdit())
                    {
                        cd::math::Vec3f factor { pre.x != 0 ? lt->value.scale.x / pre.x : 1,
                                                 pre.y != 0 ? lt->value.scale.y / pre.y : 1,
                                                 pre.z != 0 ? lt->value.scale.z / pre.z : 1 };
                        if (factor.x != 1 || factor.y != 1 || factor.z != 1)
                        {
                            lt->value.scale = pre;
                            history.push(std::make_unique<cd::editor::ScaleCommand>(scene, ent.handle, factor));
                            log_push("drag: Scale " + ent.name);
                        }
                    }
                }
                // Material tint (DragFloat3 RGB). No undo entry yet -
                // ComponentEditCommand lands with the v1.7 ECS work.
                ImGui::SeparatorText("Tint");
                {
                    float rgb[3] { ent.tint.x, ent.tint.y, ent.tint.z };
                    if (ImGui::ColorEdit3("##tint", rgb, ImGuiColorEditFlags_NoInputs))
                    {
                        ent.tint = { rgb[0], rgb[1], rgb[2] };
                    }
                }
                ImGui::PopItemWidth();
            }
        }
        else
        {
            ImGui::TextDisabled("no selection");
        }
        ImGui::End();

        // ---- R-Showcase panel: unified R1-R8 feature toggles ----
        // Single panel listing every realism-roadmap feature with
        // an in-place checkbox/slider so the user can experience the
        // engine's full capability surface from one place.
        ImGui::Begin("R-Showcase");
        ImGui::TextDisabled("CHROMODYNAMIC realism roadmap (live)");
        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.4F, 0.9F, 0.4F, 1), "R1  HDR cubemap IBL");
        ImGui::SameLine();
        ImGui::TextDisabled("(spec 6mip + diff 16 + brdf 64x64)");
        ImGui::TextColored(ImVec4(0.4F, 0.9F, 0.4F, 1), "R2  Material textures");
        ImGui::SameLine();
        ImGui::TextDisabled("(albedo + normal + MR + AO)");
        if (ImGui::CollapsingHeader("R2-Debug  View modes (see each map)"))
        {
            const char* labels[] = { "Final", "Albedo",           "World normal", "MR (G=rough,B=metal)",
                                     "AO",    "Perturbed normal", "UVs" };
            for (int i = 0; i < 7; ++i)
            {
                if (ImGui::RadioButton(labels[i], fx_view_mode == i))
                    fx_view_mode = i;
            }
        }
        // Sun direction controller - drives the directional light + IBL
        // gate. Each axis [-1,1]; normalised before push fill.
        if (ImGui::CollapsingHeader("Sun direction"))
        {
            if (!lights.empty())
            {
                auto& sun = lights[0].light;
                bool d_changed = false;
                d_changed |= ImGui::SliderFloat("dir.x", &sun.direction.x, -1.0F, 1.0F);
                d_changed |= ImGui::SliderFloat("dir.y", &sun.direction.y, -1.0F, 1.0F);
                d_changed |= ImGui::SliderFloat("dir.z", &sun.direction.z, -1.0F, 1.0F);
                if (d_changed)
                {
                    const float dl = std::sqrt(
                        sun.direction.x * sun.direction.x + sun.direction.y * sun.direction.y +
                        sun.direction.z * sun.direction.z
                    );
                    if (dl > 1e-4F)
                    {
                        sun.direction.x /= dl;
                        sun.direction.y /= dl;
                        sun.direction.z /= dl;
                    }
                }
                ImGui::SliderFloat("intensity (lx)", &sun.intensity, 0.0F, 200000.0F);
                if (ImGui::Button("Reset sun"))
                {
                    sun.direction = { -0.3F, -0.9F, -0.2F };
                    sun.intensity = 100000.0F;
                }
            }
        }
        if (ImGui::CollapsingHeader("R6  Advanced BRDFs"))
        {
            ImGui::SliderFloat("Clearcoat", &fx_clearcoat_strength, 0.0F, 1.0F);
            ImGui::SliderFloat("Sheen", &fx_sheen_strength, 0.0F, 1.0F);
            ImGui::SliderFloat("SSS (Burley)", &fx_sss_strength, 0.0F, 1.0F);
        }
        if (ImGui::CollapsingHeader("R7  Camera composition"))
        {
            ImGui::SliderFloat("Vignette", &fx_vignette_strength, 0.0F, 1.0F);
            ImGui::SliderFloat("ChromAberration", &fx_chromab_strength, 0.0F, 1.0F);
            ImGui::SliderFloat("Film grain", &fx_film_grain, 0.0F, 1.0F);
        }
        if (ImGui::CollapsingHeader("R4-FX  Inline scene post-fx (legacy)"))
        {
            ImGui::TextDisabled("DEPRECATED - composite owns the real versions.");
            ImGui::TextDisabled("Sliders disabled. Use R3 Composite post-fx panel.");
            ImGui::BeginDisabled();
            ImGui::SliderFloat("GTAO inline", &fx_gtao_strength, 0.0F, 1.0F);
            ImGui::SliderFloat("Bloom inline", &fx_bloom_strength, 0.0F, 1.0F);
            ImGui::SliderFloat("SMAA inline", &fx_smaa_strength, 0.0F, 1.0F);
            ImGui::SliderFloat("Height fog", &fx_fog_density, 0.0F, 1.0F);
            ImGui::SliderFloat("Aerial persp", &fx_aerial_perspective, 0.0F, 1.0F);
            ImGui::EndDisabled();
        }
        if (ImGui::CollapsingHeader("R3  Composite post-fx (live)", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::TextDisabled("single composite pass - AO/DOF/shafts/bloom/atmo");
            // W6-E: preset buttons — quick A/B between known-good visual
            // setups so the user doesn't have to remember every default.
            if (ImGui::Button("Defaults"))
            {
                fx_exposure = 3.0F;
                fx_saturation_boost = 1.50F;
                fx_bloom_post = 0.04F;
                fx_ao_strength = 0.55F;
                fx_dof_strength = 0.0F;
                fx_shafts_strength = 0.75F;
                fx_ssr_strength = 0.5F;
                fx_motion_blur = 0.0F;
                fx_taa_amount = 0.0F;
                fx_clouds_coverage = 0.0F;
                fx_fog_density = 0.0F;
                fx_aerial_perspective = 0.0F;
                fx_chromab_strength = 0.0F;
                fx_film_grain = 0.0F;
                fx_vignette_strength = 0.25F;
                log_push("[fx] Reset all composite knobs to defaults");
            }
            ImGui::SameLine();
            if (ImGui::Button("Cinematic"))
            {
                fx_exposure = 2.5F;
                fx_saturation_boost = 1.65F;
                fx_bloom_post = 0.08F;
                fx_ao_strength = 0.65F;
                fx_dof_strength = 0.35F;
                fx_shafts_strength = 0.85F;
                fx_ssr_strength = 0.55F;
                fx_motion_blur = 0.30F;
                fx_taa_amount = 0.80F;
                fx_clouds_coverage = 0.45F;
                fx_fog_density = 0.20F;
                fx_aerial_perspective = 0.50F;
                fx_chromab_strength = 0.25F;
                fx_film_grain = 0.15F;
                fx_vignette_strength = 0.40F;
                tonemap_op = 2;  // Hable
                log_push("[fx] Cinematic preset");
            }
            ImGui::SameLine();
            if (ImGui::Button("Performance"))
            {
                fx_exposure = 1.5F;
                fx_saturation_boost = 1.20F;
                fx_bloom_post = 0.0F;
                fx_ao_strength = 0.0F;
                fx_dof_strength = 0.0F;
                fx_shafts_strength = 0.0F;
                fx_ssr_strength = 0.0F;
                fx_motion_blur = 0.0F;
                fx_taa_amount = 0.0F;
                fx_clouds_coverage = 0.0F;
                fx_fog_density = 0.0F;
                fx_aerial_perspective = 0.0F;
                fx_chromab_strength = 0.0F;
                fx_film_grain = 0.0F;
                fx_vignette_strength = 0.0F;
                tonemap_op = 0;  // Narkowicz (cheapest)
                log_push("[fx] Performance preset (all post-fx off)");
            }
            ImGui::SameLine();
            if (ImGui::Button("HDR Demo"))
            {
                fx_exposure = 1.0F;
                fx_saturation_boost = 1.40F;
                fx_bloom_post = 0.12F;
                fx_ao_strength = 0.55F;
                fx_shafts_strength = 0.90F;
                fx_clouds_coverage = 0.30F;
                fx_fog_density = 0.0F;
                fx_chromab_strength = 0.15F;
                fx_vignette_strength = 0.30F;
                tonemap_op = 3;  // AGX — best for wide DR
                log_push("[fx] HDR demo preset (AGX tonemap + wide DR)");
            }
            ImGui::SliderFloat("Exposure", &fx_exposure, 0.1F, 10.0F);
            ImGui::SliderFloat("Saturation boost", &fx_saturation_boost, 0.5F, 2.5F);
            ImGui::SliderFloat("Bloom strength", &fx_bloom_post, 0.0F, 0.30F);
            ImGui::SliderFloat("AO strength", &fx_ao_strength, 0.0F, 1.0F);
            ImGui::SliderFloat("DOF strength", &fx_dof_strength, 0.0F, 1.0F);
            ImGui::SliderFloat("Light shafts", &fx_shafts_strength, 0.0F, 1.5F);
            ImGui::SliderFloat("SSR strength", &fx_ssr_strength, 0.0F, 1.0F);
            ImGui::SliderFloat("Motion blur", &fx_motion_blur, 0.0F, 1.0F);
            ImGui::SliderFloat("TAA amount", &fx_taa_amount, 0.0F, 0.97F);
            ImGui::SliderFloat("Clouds coverage", &fx_clouds_coverage, 0.0F, 1.0F);
            ImGui::TextDisabled("TAA: camera-velocity reprojection + 3x3 neighbourhood clamp");
        }
        if (ImGui::CollapsingHeader("R3  Frame-graph + advanced post-fx"))
        {
            ImGui::TextDisabled("Live composite stack:");
            ImGui::BulletText("AO  (depth + G-Buffer-normal 8-ring scan)");
            ImGui::BulletText("SSR (24-step world-space ray-march)");
            ImGui::BulletText("DOF (8-tap bokeh, focus = cam target)");
            ImGui::BulletText("Light shafts (16-tap Mitchell god rays)");
            ImGui::BulletText("Motion blur (velocity G-Buffer + camera fallback)");
            ImGui::BulletText("TAA (history ping-pong + Halton(2,3) jitter)");
            ImGui::BulletText("Atmo fog + aerial perspective + sun in-scatter");
            ImGui::BulletText("Vignette + film grain + ChromAB");
            ImGui::TextDisabled("All tunable via R3 Composite post-fx (live) panel.");
        }
        if (ImGui::CollapsingHeader("R4  GI (ReSTIR / DDGI / NRC)"))
        {
            ImGui::TextDisabled("Library API live: parity smokes pass.");
            ImGui::BulletText("hello_restir   - DI + GI reservoir math");
            ImGui::BulletText("hello_ddgi     - probe-volume trilinear weights");
            ImGui::BulletText("hello_nrc      - CpuReferenceMlp SGD convergence");
            ImGui::TextDisabled("GPU pipeline wiring queued - needs RT compute pipe.");
            ImGui::TextDisabled("Run samples/lib_smokes/hello_{restir,ddgi,nrc}.exe");
        }
        if (ImGui::CollapsingHeader("R5  Volumetrics"))
        {
            ImGui::TextDisabled("Composite-inline (cheap) and lib-level (CPU smoke):");
            ImGui::BulletText("Sun in-scatter fog (HG g=0.6) - live in composite");
            ImGui::BulletText("fBm sky cloud overlay - live in composite");
            ImGui::BulletText("hello_volumetric_fog - Wronski 2014 froxel grid (CPU)");
            ImGui::BulletText("hello_volumetric_clouds - Schneider 2017 march (CPU)");
            ImGui::TextDisabled("3D froxel GPU compute path queued.");
        }
        if (ImGui::CollapsingHeader("R8  HDR10 display output"))
        {
            ImGui::Checkbox("HDR10 request (composite op 4 ready; needs HDR display)", &fx_hdr10_request);
            ImGui::TextDisabled("Tonemap operator 4 = ST.2084 PQ encode (Rec.2020).");
            ImGui::TextDisabled("Swapchain colour-space already exposed via");
            ImGui::TextDisabled("rhi::ColorSpace::kHdr10St2084; activate by setting");
            ImGui::TextDisabled("rd.swapchain.colour_space at startup + restarting.");
        }
        ImGui::End();

        // ---- Counters ----
        draw_counters_panel(counters, dt, frame_idx);

        // ---- Random viz ----
        draw_random_panel(hist_uniform, hist_normal);

        // ---- Audio ----
        ImGui::Begin("Audio");
        if (audio_muted)
            ImGui::TextColored(ImVec4(1, 0.5F, 0.3F, 1), "MUTED");
        else
            ImGui::TextColored(ImVec4(0.4F, 1, 0.4F, 1), "LIVE");
        ImGui::Text("Mixer -> Comp -> Reverb -> LowPass -> Limiter");
        ImGui::Separator();
        ImGui::Text("peak (last buf)      %.3f", static_cast<double>(audio_peak_window));
        ImGui::Text("comp gain reduction  %.2f dB", static_cast<double>(audio_comp_db_window));
        ImGui::Text("limiter min gain     %.4f", static_cast<double>(audio_limiter_gain_min));
        if (!audio_meter_history.empty())
        {
            std::vector<float> vv(audio_meter_history.begin(), audio_meter_history.end());
            ImGui::PlotLines(
                "##peak_hist",
                vv.data(),
                static_cast<int>(vv.size()),
                0,
                "peak history",
                0.0F,
                1.0F,
                ImVec2(0, 60)
            );
        }
        // Phase 139 - last 5 s of DSP output dump.
        ImGui::Separator();
        ImGui::TextDisabled("No live audio backend wired in this sample -");
        ImGui::TextDisabled("DSP chain ticks in memory. Save WAV to hear it.");
        if (ImGui::Button("Save Last 5 s as hello_engine_out.wav"))
        {
            // Compose contiguous buffer from ring (oldest ??' newest).
            std::vector<std::int16_t> samples;
            samples.reserve(kAudioRingFrames);
            std::size_t start = audio_ring_write;
            std::size_t n = (audio_total_written < kAudioRingFrames) ? static_cast<std::size_t>(audio_total_written)
                                                                     : kAudioRingFrames;
            if (audio_total_written < kAudioRingFrames)
                start = 0;
            for (std::size_t i = 0; i < n; ++i)
            {
                samples.push_back(audio_ring[(start + i) % kAudioRingFrames]);
            }
            // Minimal WAV header (mono s16) - same encoder shape as
            // hello_audio_chain / hello_audio_synth.
            const std::uint32_t data_bytes = static_cast<std::uint32_t>(samples.size() * sizeof(std::int16_t));
            const std::uint32_t fmt_size = 16;
            const std::uint32_t riff_size = 4u + 8u + fmt_size + 8u + data_bytes;
            std::vector<std::byte> bytes;
            bytes.reserve(8u + riff_size);
            auto push_tag = [&](const char (&t)[5])
            {
                for (int i = 0; i < 4; ++i)
                    bytes.push_back(static_cast<std::byte>(t[i]));
            };
            auto push_le = [&](std::uint64_t v, int n_bytes)
            {
                for (int i = 0; i < n_bytes; ++i)
                {
                    const auto shift = static_cast<unsigned>(i) * 8u;
                    bytes.push_back(std::byte { static_cast<unsigned char>((v >> shift) & 0xFFu) });
                }
            };
            push_tag("RIFF");
            push_le(riff_size, 4);
            push_tag("WAVE");
            push_tag("fmt ");
            push_le(fmt_size, 4);
            push_le(1u, 2);                          // PCM
            push_le(1u, 2);                          // mono
            push_le(kAudioSampleRate, 4);
            push_le(kAudioSampleRate * 1u * 2u, 4);  // byte rate
            push_le(2u, 2);                          // block align
            push_le(16u, 2);                         // bits per sample
            push_tag("data");
            push_le(data_bytes, 4);
            bytes.insert(
                bytes.end(),
                reinterpret_cast<const std::byte*>(samples.data()),
                reinterpret_cast<const std::byte*>(samples.data() + samples.size())
            );
            std::ofstream f { "hello_engine_out.wav", std::ios::binary | std::ios::trunc };
            if (f)
            {
                f.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
                log_push("[audio] wrote hello_engine_out.wav (" + std::to_string(bytes.size()) + " B)");
            }
            else
            {
                log_push("[audio] WAV write failed (ofstream)");
            }
        }
        ImGui::End();

        // ---- Net Sim ----
        ImGui::Begin("Net Sim");
        ImGui::TextColored(
            net_enabled ? ImVec4(0.4F, 1, 0.4F, 1) : ImVec4(1, 0.5F, 0.3F, 1),
            "%s",
            net_enabled ? "RUNNING" : "PAUSED"
        );
        ImGui::Text("server tick 60 Hz | throttle 30 pkt/s | 10%% loss | 30-90 ms latency");
        ImGui::Separator();
        ImGui::Text("sent    %u", net_sent);
        ImGui::Text(
            "recv    %u   (delivery %.1f%%)",
            net_recv,
            net_sent == 0 ? 0.0 : 100.0 * static_cast<double>(net_recv) / static_cast<double>(net_sent)
        );
        ImGui::Text("drop    %u", net_drop);
        ImGui::Text(
            "raw     %llu B  /  wire %llu B   (%.1f%% wire/raw)",
            static_cast<unsigned long long>(net_raw_bytes),
            static_cast<unsigned long long>(net_wire_bytes),
            net_raw_bytes == 0 ? 0.0 : 100.0 * static_cast<double>(net_wire_bytes) / static_cast<double>(net_raw_bytes)
        );
        ImGui::Text(
            "RTT     %.2f ms   jitter %.2f ms",
            static_cast<double>(net_rtt.current_rtt_us()) / 1000.0,
            static_cast<double>(net_rtt.jitter_us()) / 1000.0
        );
        ImGui::Text("snapshots buffered: %zu", net_snapbuf.size());
        ImGui::End();

        // ---- Streamer (Phase 150) ----
        ImGui::Begin("Streamer");
        ImGui::TextDisabled("Demo of cd::asset::AsyncStreamer - a background worker");
        ImGui::TextDisabled("that processes async asset-load requests with priority");
        ImGui::TextDisabled("and failure handling. Buttons enqueue simulated loads.");
        ImGui::Separator();
        ImGui::Text(
            "worker: %s   pending %zu",
            streamer.is_running() ? "RUNNING" : "STOPPED",
            streamer.pending_count()
        );
        ImGui::Text(
            "completed %u   failed %u",
            streamer_completed.load(std::memory_order_relaxed),
            streamer_failed.load(std::memory_order_relaxed)
        );
        ImGui::Separator();
        if (ImGui::Button("Enqueue (low prio)"))
            streamer_enqueue(0);
        ImGui::SameLine();
        if (ImGui::Button("Enqueue (high prio)"))
            streamer_enqueue(100);
        ImGui::SameLine();
        if (ImGui::Button("Enqueue 8 burst"))
        {
            for (int i = 0; i < 8; ++i)
                streamer_enqueue(i * 10);
        }
        ImGui::Separator();
        // Recent-tracked rows: id, state.
        for (auto it = streamer_tracked.rbegin(); it != streamer_tracked.rend(); ++it)
        {
            const auto st = streamer.state_of(*it);
            const char* lbl = st == cd::asset::StreamState::kComplete   ? "COMPLETE"
                              : st == cd::asset::StreamState::kInflight ? "INFLIGHT"
                              : st == cd::asset::StreamState::kFailed   ? "FAILED"
                                                                        : "PENDING";
            const ImVec4 col = st == cd::asset::StreamState::kComplete   ? ImVec4(0.4F, 1.0F, 0.4F, 1)
                               : st == cd::asset::StreamState::kInflight ? ImVec4(1.0F, 0.85F, 0.3F, 1)
                               : st == cd::asset::StreamState::kFailed   ? ImVec4(1.0F, 0.4F, 0.4F, 1)
                                                                         : ImVec4(0.7F, 0.7F, 0.7F, 1);
            ImGui::TextColored(col, "id %llu  %s", static_cast<unsigned long long>(it->value()), lbl);
        }
        ImGui::End();

        // ---- Outliner (gap #18 cd::world_container preview) ----
        // Read-only world-container tree (top) + clickable entity +
        // light list (bottom). B14 closes 'Outliner clicks don't
        // select' — entries below are Selectable and now drive the
        // selected/selected_kind/selected_light state.
        ImGui::Begin("Outliner");
        ImGui::TextDisabled("Scene entities + lights (click to select):");
        for (std::size_t i = 0; i < entities.size(); ++i)
        {
            const bool is_sel = (selected_kind == SelKind::kEntity && selected == static_cast<int>(i));
            const std::string label = entities[i].name + "##outl_e" + std::to_string(i);
            if (ImGui::Selectable(label.c_str(), is_sel))
            {
                selected = static_cast<int>(i);
                selected_kind = SelKind::kEntity;
            }
        }
        for (std::size_t i = 0; i < lights.size(); ++i)
        {
            const bool is_sel = (selected_kind == SelKind::kLight && selected == static_cast<int>(i));
            const std::string label = "[light] " + lights[i].name + "##outl_l" + std::to_string(i);
            if (ImGui::Selectable(label.c_str(), is_sel))
            {
                selected = static_cast<int>(i);
                selected_kind = SelKind::kLight;
            }
        }
        ImGui::Separator();
        ImGui::TextDisabled("World container (read-only):");
        if (ImGui::TreeNodeEx(cd_world.name().data(), ImGuiTreeNodeFlags_DefaultOpen))
        {
            auto* proj = cd_world.project();
            if (proj == nullptr)
            {
                ImGui::TextDisabled("(no project)");
            }
            else
            {
                std::string proj_lbl { proj->name() };
                if (ImGui::TreeNodeEx((proj_lbl + "##proj").c_str(), ImGuiTreeNodeFlags_DefaultOpen))
                {
                    for (std::size_t li = 0; li < proj->level_count(); ++li)
                    {
                        auto* lvl = proj->level(li);
                        if (lvl == nullptr)
                            continue;
                        std::string lvl_lbl { lvl->name() };
                        const auto& b = lvl->bounds();
                        if (ImGui::TreeNodeEx(
                                (lvl_lbl + "##l" + std::to_string(li)).c_str(),
                                ImGuiTreeNodeFlags_DefaultOpen
                            ))
                        {
                            ImGui::TextDisabled(
                                "bounds  [%.1f, %.1f, %.1f] -> [%.1f, %.1f, %.1f]",
                                static_cast<double>(b.min.x),
                                static_cast<double>(b.min.y),
                                static_cast<double>(b.min.z),
                                static_cast<double>(b.max.x),
                                static_cast<double>(b.max.y),
                                static_cast<double>(b.max.z)
                            );
                            for (std::size_t yi = 0; yi < lvl->layer_count(); ++yi)
                            {
                                auto* ly = lvl->layer(yi);
                                if (ly == nullptr)
                                    continue;
                                std::string ly_lbl { ly->name() };
                                const bool active = (yi == lvl->active_layer());
                                if (active)
                                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0F, 0.85F, 0.0F, 1.0F));
                                ImGui::Bullet();
                                ImGui::Text(
                                    "%s%s%s%s",
                                    ly_lbl.c_str(),
                                    active ? " (active)" : "",
                                    ly->locked() ? " [locked]" : "",
                                    !ly->visible() ? " [hidden]" : ""
                                );
                                if (active)
                                    ImGui::PopStyleColor();
                            }
                            ImGui::TreePop();
                        }
                    }
                    ImGui::TreePop();
                }
            }
            ImGui::TreePop();
        }
        ImGui::End();

        // ---- Lights (Phase 171/172 - cd::light system) ----
        ImGui::Begin("Lights");
        ImGui::TextDisabled("cd::light - Frostbite + Filament model");
        ImGui::Separator();

        // Per-frame: refresh CCT??'RGB, then assign every enabled light
        // into the cluster grid for the stats line.
        cluster_grid.clear();
        std::uint32_t enabled_count = 0;
        std::uint32_t cluster_hits = 0;
        for (std::size_t i = 0; i < lights.size(); ++i)
        {
            auto& row = lights[i];
            if (row.kelvin > 0.0F)
                row.light.color = cd::light::cct_to_linear_rgb(row.kelvin);
            if (row.enabled)
            {
                ++enabled_count;
                cluster_hits += cluster_grid.assign(static_cast<std::uint32_t>(i), row.light, row.light.position);
            }
        }

        ImGui::Text("enabled %u / %zu     cluster assignments %u", enabled_count, lights.size(), cluster_hits);
        ImGui::Text(
            "grid: %ux%ux%u  near %.1f  far %.1f",
            cluster_desc.tiles_x,
            cluster_desc.tiles_y,
            cluster_desc.slices_z,
            static_cast<double>(cluster_desc.near_z),
            static_cast<double>(cluster_desc.far_z)
        );
        ImGui::Separator();

        for (std::size_t i = 0; i < lights.size(); ++i)
        {
            auto& row = lights[i];
            ImGui::PushID(static_cast<int>(i));
            // Click on row name selects the light (so Inspector + gizmo see it).
            const bool row_sel = (selected_kind == SelKind::kLight && selected == static_cast<int>(i));
            if (row_sel)
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0F, 0.85F, 0.0F, 1.0F));
            if (ImGui::Selectable(
                    (row_sel ? std::string { "> " } + row.name : row.name).c_str(),
                    row_sel,
                    ImGuiSelectableFlags_AllowOverlap
                ))
            {
                selected = static_cast<int>(i);
                selected_kind = SelKind::kLight;
            }
            if (row_sel)
                ImGui::PopStyleColor();
            ImGui::SameLine();
            ImGui::Checkbox("##en", &row.enabled);

            // Color preview swatch - what the CCT actually produces.
            const ImVec4 col { row.light.color.x, row.light.color.y, row.light.color.z, 1.0F };
            ImGui::SameLine();
            ImGui::ColorButton(
                "##swatch",
                col,
                ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoPicker,
                ImVec2(24, 14)
            );

            // Type badge.
            const char* type_str = row.light.type == cd::light::LightType::kDirectional ? "DIR "
                                   : row.light.type == cd::light::LightType::kPoint     ? "POINT"
                                   : row.light.type == cd::light::LightType::kSpot      ? "SPOT"
                                   : row.light.type == cd::light::LightType::kRectArea  ? "RECT"
                                                                                        : "DISK";
            ImGui::SameLine();
            ImGui::TextDisabled("[%s]", type_str);

            // CCT + intensity sliders. CCT-driven palette is the default,
            // but a raw RGB picker is available when the user wants an
            // arbitrary tint. Setting RGB sets kelvin to 0 so the per-
            // frame CCT->RGB rebake won't overwrite the manual choice.
            ImGui::SliderFloat("CCT (K)", &row.kelvin, 0.0F, 15000.0F, "%.0f K");
            {
                float rgb[3] { row.light.color.x, row.light.color.y, row.light.color.z };
                if (ImGui::ColorEdit3("colour (RGB)", rgb, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_Float))
                {
                    row.light.color = { rgb[0], rgb[1], rgb[2] };
                    row.kelvin = 0.0F;
                }
            }
            const char* unit = row.light.type == cd::light::LightType::kDirectional ? "lx" : "lm";
            ImGui::SliderFloat(
                "intensity",
                &row.light.intensity,
                0.0F,
                200000.0F,
                ("%.0f " + std::string { unit }).c_str()
            );
            if (row.light.type == cd::light::LightType::kPoint || row.light.type == cd::light::LightType::kSpot)
            {
                ImGui::SliderFloat("range", &row.light.range, 0.5F, 50.0F, "%.1f m");

                // Live attenuation preview at 1m, 5m, range/2.
                const float a1 = cd::light::distance_attenuation(1.0F, row.light.range);
                const float a5 = cd::light::distance_attenuation(5.0F, row.light.range);
                const float ah = cd::light::distance_attenuation(row.light.range * 0.5F, row.light.range);
                ImGui::TextDisabled(
                    "atten 1m=%.3f  5m=%.4f  r/2=%.3f",
                    static_cast<double>(a1),
                    static_cast<double>(a5),
                    static_cast<double>(ah)
                );
            }
            // W8-G: spot cone angles (inner = full bright, outer = falloff
            // edge). Stored on the Light as cos(angle); we display as
            // degrees for artist readability and clamp inner <= outer.
            if (row.light.type == cd::light::LightType::kSpot)
            {
                float inner_deg = std::acos(std::clamp(row.light.cos_inner_cone, -1.0F, 1.0F)) * (180.0F / 3.14159265F);
                float outer_deg = std::acos(std::clamp(row.light.cos_outer_cone, -1.0F, 1.0F)) * (180.0F / 3.14159265F);
                bool changed = false;
                if (ImGui::SliderFloat("inner cone (deg)", &inner_deg, 0.5F, 89.0F, "%.1f"))
                    changed = true;
                if (ImGui::SliderFloat("outer cone (deg)", &outer_deg, 0.5F, 89.5F, "%.1f"))
                    changed = true;
                if (changed)
                {
                    if (inner_deg > outer_deg - 0.5F)
                        inner_deg = outer_deg - 0.5F;
                    if (inner_deg < 0.5F)
                        inner_deg = 0.5F;
                    row.light.cos_inner_cone = std::cos(inner_deg * (3.14159265F / 180.0F));
                    row.light.cos_outer_cone = std::cos(outer_deg * (3.14159265F / 180.0F));
                    const float denom = row.light.cos_inner_cone - row.light.cos_outer_cone;
                    row.light.inv_cone_range = denom > 1e-5F ? 1.0F / denom : 0.0F;
                }
                ImGui::TextDisabled("full cone = 2x outer = %.0f deg", static_cast<double>(outer_deg * 2.0F));
            }
            // Direction control for any light type that has a meaningful
            // forward axis (everything except omnidirectional point). User
            // feedback: "isiklara yun veremiyorum" - give them a slider.
            // Sliders are raw xyz in [-1, 1]; renormalized after edit so
            // |dir| == 1 holds for the shading + shadow code that reads it.
            if (row.light.type == cd::light::LightType::kDirectional || row.light.type == cd::light::LightType::kSpot ||
                row.light.type == cd::light::LightType::kRectArea || row.light.type == cd::light::LightType::kDiskArea)
            {
                float dir[3] { row.light.direction.x, row.light.direction.y, row.light.direction.z };
                if (ImGui::SliderFloat3("dir xyz", dir, -1.0F, 1.0F, "%.2f"))
                {
                    const float L = std::sqrt(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
                    if (L > 1e-4F)
                    {
                        row.light.direction.x = dir[0] / L;
                        row.light.direction.y = dir[1] / L;
                        row.light.direction.z = dir[2] / L;
                    }
                }
                if (row.light.type == cd::light::LightType::kDirectional)
                {
                    ImGui::TextDisabled(
                        "sun pointing %s",
                        row.light.direction.y < 0.0F ? "DOWN (casts shadow)" : "UP (no shadow)"
                    );
                }
                // W8-R: one-click "flip normal" for area lights so the
                // user doesn't have to fight the gizmo when the rect's
                // emissive face points the wrong way. Inverts both the
                // direction and the area_tangent so the basis stays
                // consistent (tangent stays perpendicular to direction
                // after the flip).
                if (row.light.type == cd::light::LightType::kRectArea ||
                    row.light.type == cd::light::LightType::kDiskArea)
                {
                    if (ImGui::Button("Flip normal"))
                    {
                        row.light.direction.x = -row.light.direction.x;
                        row.light.direction.y = -row.light.direction.y;
                        row.light.direction.z = -row.light.direction.z;
                        row.light.area_tangent.x = -row.light.area_tangent.x;
                        row.light.area_tangent.y = -row.light.area_tangent.y;
                        row.light.area_tangent.z = -row.light.area_tangent.z;
                    }
                    ImGui::SameLine();
                    // W8-V: one-click aim-at-origin so translating the
                    // rect doesn't leave its emit normal stale. After
                    // moving the rect via the gizmo the user usually
                    // wants it to face the scene; this button does the
                    // rotation in one click and re-derives a tangent
                    // perpendicular to the new normal.
                    if (ImGui::Button("Aim at origin"))
                    {
                        cd::math::Vec3f nn { -row.light.position.x, -row.light.position.y, -row.light.position.z };
                        const float nl = std::sqrt(nn.x * nn.x + nn.y * nn.y + nn.z * nn.z);
                        if (nl > 1e-5F)
                        {
                            nn.x /= nl;
                            nn.y /= nl;
                            nn.z /= nl;
                            row.light.direction = nn;
                            cd::math::Vec3f tt;
                            if (nn.z < -0.9999F)
                            {
                                tt = { 0.0F, -1.0F, 0.0F };
                            }
                            else
                            {
                                const float fa = 1.0F / (1.0F + nn.z);
                                tt = { 1.0F - nn.x * nn.x * fa, -nn.x * nn.y * fa, -nn.x };
                            }
                            const float tl = std::sqrt(tt.x * tt.x + tt.y * tt.y + tt.z * tt.z);
                            if (tl > 1e-5F)
                            {
                                tt.x /= tl;
                                tt.y /= tl;
                                tt.z /= tl;
                                row.light.area_tangent = tt;
                            }
                        }
                    }
                    ImGui::SameLine();
                    ImGui::TextDisabled("(emit side = +normal)");
                }
            }
            ImGui::PopID();
            if (i + 1 < lights.size())
                ImGui::Separator();
        }
        ImGui::End();

        // ---- History ----
        draw_history_panel(history, log);

        // ---- Phase 151 - selection outline (ImGui overlay) ----
        // We use the kWireframe style: project the selected entity's
        // world position onto the screen, then draw a circle around it
        // via ImGui's foreground draw list. Cheap, no extra GPU pass,
        // and demonstrates SelectionOutline state end-to-end.
        if (selected_kind == SelKind::kEntity && selected >= 0 && selected < static_cast<int>(entities.size()))
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
            const float vw = static_cast<float>(frame.extent.width);
            const float vh = static_cast<float>(frame.extent.height);
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
                if (clip[3] <= 0.0F)
                    continue;  // behind camera
                const float ndc_x = clip[0] / clip[3];
                const float ndc_y = clip[1] / clip[3];
                const float sx = (ndc_x * 0.5F + 0.5F) * vw;
                const float sy = (1.0F - (ndc_y * 0.5F + 0.5F)) * vh;
                // Radius shrinks with distance.
                const float radius = std::max(8.0F, 60.0F / std::max(0.5F, clip[3] * 0.25F));
                dl->AddCircle(ImVec2(sx, sy), radius, col, 32, outline.thickness * 1.5F);
                // Crosshair tick marks for emphasis.
                dl->AddLine(ImVec2(sx - radius - 6.0F, sy), ImVec2(sx - radius + 6.0F, sy), col, outline.thickness);
                dl->AddLine(ImVec2(sx + radius - 6.0F, sy), ImVec2(sx + radius + 6.0F, sy), col, outline.thickness);
                dl->AddLine(ImVec2(sx, sy - radius - 6.0F), ImVec2(sx, sy - radius + 6.0F), col, outline.thickness);
                dl->AddLine(ImVec2(sx, sy + radius - 6.0F), ImVec2(sx, sy + radius + 6.0F), col, outline.thickness);
            }
        }

        // ---- World grid (floor) ----
        // Moved into the floor fragment shader (analytic XZ grid with
        // fwidth-based line width). That respects the depth buffer so
        // the grid no longer shows through entities - user-flagged
        // "grid objeler arasindan gozukmemeli". The floor mesh draw
        // above sets tint[3] = 2.0 to enable that shader branch.

        // ---- Phase D - Light source markers (world-space overlay) ----
        // Each enabled light gets a small visual in the viewport so
        // the user can SEE where the lights are placed.
        // - Directional: a yellow line from sky toward target (sun ray)
        // - Point: filled circle in light color + range ring
        // - Spot:  filled circle at apex + cone wireframe (4 lines to far disk)
        // - Rect area: 4 corners outlined in light color
        {
            const float vw = static_cast<float>(frame.extent.width);
            const float vh = static_cast<float>(frame.extent.height);
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
                if (c[3] <= 0.0F)
                    return ImVec2(-1.0F, -1.0F);
                return ImVec2((c[0] / c[3] * 0.5F + 0.5F) * vw, (1.0F - (c[1] / c[3] * 0.5F + 0.5F)) * vh);
            };
            for (std::size_t li = 0; li < lights.size(); ++li)
            {
                const auto& lrow = lights[li];
                if (!lrow.enabled)
                    continue;
                const auto& L = lrow.light;
                const bool sel = (selected_kind == SelKind::kLight && selected == static_cast<int>(li));
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
                                    const float t0 = static_cast<float>(i) / 24.0F * 6.2831853F;
                                    const float t1 = static_cast<float>(i + 1) / 24.0F * 6.2831853F;
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
                                const float t = static_cast<float>(i) / static_cast<float>(kEdges) * 6.2831853F;
                                const float ct = std::cos(t), st = std::sin(t);
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
                                const auto pb = project(rim[static_cast<std::size_t>(i + 1)]);
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
                        const float hw = L.area_width * 0.5F, hh = L.area_height * 0.5F;
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
                if (selected >= static_cast<int>(entities.size()))
                    return nullptr;
                if (auto* lt = scene.local(entities[static_cast<std::size_t>(selected)].handle))
                    return &lt->value.position;
                return nullptr;
            }
            if (selected_kind == SelKind::kLight)
            {
                if (selected >= static_cast<int>(lights.size()))
                    return nullptr;
                auto& L = lights[static_cast<std::size_t>(selected)].light;
                if (L.type == cd::light::LightType::kDirectional)
                    return nullptr;
                return &L.position;
            }
            return nullptr;
        };

        if (!gizmo_visible || gizmo_target_pos() == nullptr)
        {
            gizmo_was_hovered = false;
        }
        if (gizmo_visible && gizmo_target_pos() != nullptr)
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
                const float vw = static_cast<float>(frame.extent.width);
                const float vh = static_cast<float>(frame.extent.height);
                auto project = [&](const cd::math::Vec3f& p) -> ImVec2
                {
                    const cd::math::Vec4f wp { p.x, p.y, p.z, 1.0F };
                    cd::math::Vec4f c {};
                    for (std::size_t r = 0; r < 4; ++r)
                        c[r] = vp[0][r] * wp[0] + vp[1][r] * wp[1] + vp[2][r] * wp[2] + vp[3][r] * wp[3];
                    if (c[3] <= 0.0F)
                        return ImVec2(-1.0F, -1.0F);
                    return ImVec2((c[0] / c[3] * 0.5F + 0.5F) * vw, (1.0F - (c[1] / c[3] * 0.5F + 0.5F)) * vh);
                };
                const auto& tgt = gizmo.target();
                constexpr float kAxisLen = 1.5F;
                const ImVec2 p_org = project(tgt);
                const ImVec2 p_x = project({ tgt.x + kAxisLen, tgt.y, tgt.z });
                const ImVec2 p_y = project({ tgt.x, tgt.y + kAxisLen, tgt.z });
                const ImVec2 p_z = project({ tgt.x, tgt.y, tgt.z + kAxisLen });

                if (p_org.x >= 0.0F)
                {
                    auto* dl = ImGui::GetForegroundDrawList();
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
                        return (gizmo.hover() == a || gizmo.active_axis() == a) ? 5.0F : 3.0F;
                    };

                    dl->AddLine(p_org, p_x, cx, thick(cd::editor::GizmoAxis::kX));
                    dl->AddLine(p_org, p_y, cy, thick(cd::editor::GizmoAxis::kY));
                    dl->AddLine(p_org, p_z, cz, thick(cd::editor::GizmoAxis::kZ));
                    // Arrowheads (filled triangles).
                    auto arrowhead = [&](ImVec2 from, ImVec2 to, ImU32 col)
                    {
                        const float dx = to.x - from.x, dy = to.y - from.y;
                        const float len = std::sqrt(dx * dx + dy * dy);
                        if (len < 1e-3F)
                            return;
                        const float nx = dx / len, ny = dy / len;
                        const float sx = -ny, sy = nx;
                        constexpr float kHead = 10.0F;
                        const ImVec2 a = to;
                        const ImVec2 b { to.x - nx * kHead + sx * 5.0F, to.y - ny * kHead + sy * 5.0F };
                        const ImVec2 c { to.x - nx * kHead - sx * 5.0F, to.y - ny * kHead - sy * 5.0F };
                        dl->AddTriangleFilled(a, b, c, col);
                    };
                    // Mode-specific tip decoration:
                    //   translate ??' arrowheads
                    //   rotate    ??' small circles at tips
                    //   scale     ??' small filled cubes at tips
                    if (gizmo_mode == GizmoMode::kTranslate)
                    {
                        arrowhead(p_org, p_x, cx);
                        arrowhead(p_org, p_y, cy);
                        arrowhead(p_org, p_z, cz);
                    }
                    else if (gizmo_mode == GizmoMode::kRotate)
                    {
                        // Draw the standard 3 rotation rings on each
                        // world-axis plane. Each ring is the projection
                        // of a unit-radius circle (scaled by kAxisLen)
                        // in the plane perpendicular to its color axis.
                        constexpr int kRingSeg = 48;
                        constexpr float kRingRad = 1.5F;
                        auto draw_ring = [&](cd::math::Vec3f u, cd::math::Vec3f v, ImU32 c, float t)
                        {
                            for (int i = 0; i < kRingSeg; ++i)
                            {
                                const float a = static_cast<float>(i) / kRingSeg * 6.2831853F;
                                const float b = static_cast<float>(i + 1) / kRingSeg * 6.2831853F;
                                const float ca0 = std::cos(a), sa0 = std::sin(a);
                                const float cb0 = std::cos(b), sb0 = std::sin(b);
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
                        // Ring around X axis lives in (Y, Z) plane.
                        draw_ring({ 0, 1, 0 }, { 0, 0, 1 }, cx, th_x);
                        // Ring around Y axis lives in (X, Z) plane.
                        draw_ring({ 1, 0, 0 }, { 0, 0, 1 }, cy, th_y);
                        // Ring around Z axis lives in (X, Y) plane.
                        draw_ring({ 1, 0, 0 }, { 0, 1, 0 }, cz, th_z);
                    }
                    else  // kScale
                    {
                        const auto cube_at = [&](ImVec2 c, ImU32 col)
                        {
                            const ImVec2 a { c.x - 5, c.y - 5 };
                            const ImVec2 b { c.x + 5, c.y + 5 };
                            dl->AddRectFilled(a, b, col);
                        };
                        cube_at(p_x, cx);
                        cube_at(p_y, cy);
                        cube_at(p_z, cz);
                    }
                    // Mode label.
                    const char* mode_lbl = gizmo_mode == GizmoMode::kTranslate ? "T"
                                           : gizmo_mode == GizmoMode::kRotate  ? "R"
                                                                               : "S";
                    dl->AddText(
                        ImVec2(p_org.x + 8, p_org.y + 8),
                        ImGui::ColorConvertFloat4ToU32(ImVec4(1, 1, 1, 0.9F)),
                        mode_lbl
                    );

                    // Hover test. Translate/Scale modes measure mouse-to-
                    // axis-line distance (arrows). Rotate mode measures
                    // mouse-to-ring polyline distance (so the user grabs a
                    // ring, not an arrow - feedback "rotation islemini
                    // yeni koydugun cemberler userinden yapabilmek
                    // istiyorum").
                    const ImVec2 mp = ImGui::GetIO().MousePos;
                    auto dist_to_seg = [](ImVec2 a, ImVec2 b, ImVec2 p)
                    {
                        const float dx = b.x - a.x, dy = b.y - a.y;
                        const float L2 = dx * dx + dy * dy;
                        if (L2 < 1e-4F)
                            return std::sqrt((p.x - a.x) * (p.x - a.x) + (p.y - a.y) * (p.y - a.y));
                        const float t = std::clamp(((p.x - a.x) * dx + (p.y - a.y) * dy) / L2, 0.0F, 1.0F);
                        const float qx = a.x + t * dx, qy = a.y + t * dy;
                        return std::sqrt((p.x - qx) * (p.x - qx) + (p.y - qy) * (p.y - qy));
                    };
                    cd::editor::GizmoAxis best = cd::editor::GizmoAxis::kNone;
                    float best_d = gizmo.hover_tolerance_pixels;
                    if (gizmo_mode == GizmoMode::kRotate)
                    {
                        // Sample each ring at the same resolution we draw
                        // it (48 segments); compute min distance from
                        // mouse to the ring polyline. Cheap (3 ?- 48 = 144
                        // segments per frame at hover-test time).
                        constexpr int kHoverSeg = 48;
                        constexpr float kHoverRad = 1.5F;  // matches kRingRad above
                        auto ring_dist = [&](cd::math::Vec3f u, cd::math::Vec3f v) -> float
                        {
                            float min_d = std::numeric_limits<float>::infinity();
                            ImVec2 prev {};
                            bool prev_ok = false;
                            for (int i = 0; i <= kHoverSeg; ++i)
                            {
                                const float a = static_cast<float>(i) / kHoverSeg * 6.2831853F;
                                const float ca = std::cos(a), sa = std::sin(a);
                                const cd::math::Vec3f w { tgt.x + (u.x * ca + v.x * sa) * kHoverRad,
                                                          tgt.y + (u.y * ca + v.y * sa) * kHoverRad,
                                                          tgt.z + (u.z * ca + v.z * sa) * kHoverRad };
                                const auto pw = project(w);
                                if (pw.x >= 0.0F)
                                {
                                    if (prev_ok)
                                    {
                                        const float d = dist_to_seg(prev, pw, mp);
                                        if (d < min_d)
                                            min_d = d;
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
                        const float dx = ring_dist({ 0, 1, 0 }, { 0, 0, 1 });  // X-axis ring lives in YZ
                        const float dy = ring_dist({ 1, 0, 0 }, { 0, 0, 1 });  // Y-axis ring lives in XZ
                        const float dz = ring_dist({ 1, 0, 0 }, { 0, 1, 0 });  // Z-axis ring lives in XY
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
                        if (auto d = dist_to_seg(p_org, p_x, mp); d < best_d)
                        {
                            best_d = d;
                            best = cd::editor::GizmoAxis::kX;
                        }
                        if (auto d = dist_to_seg(p_org, p_y, mp); d < best_d)
                        {
                            best_d = d;
                            best = cd::editor::GizmoAxis::kY;
                        }
                        if (auto d = dist_to_seg(p_org, p_z, mp); d < best_d)
                        {
                            best_d = d;
                            best = cd::editor::GizmoAxis::kZ;
                        }
                    }
                    gizmo.set_hover(best);
                    gizmo_was_hovered = (best != cd::editor::GizmoAxis::kNone);

                    const bool over_imgui_ui = ImGui::GetIO().WantCaptureMouse && ImGui::IsAnyItemHovered();
                    // If the mouse is hovering an axis arrow AND a left-
                    // click is pending from the OS event loop, the gizmo
                    // wins over the 3D pick path - suppress the pick.
                    if (pending_pick && best != cd::editor::GizmoAxis::kNone)
                    {
                        pending_pick = false;
                    }
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
                        const float vw = static_cast<float>(window.width());
                        const float vh = static_cast<float>(window.height());
                        if (vw < 1 || vh < 1)
                            return std::nullopt;
                        // Camera basis (same path as pick).
                        cd::math::Vec3f fwd { cam.target.x - cam.eye.x,
                                              cam.target.y - cam.eye.y,
                                              cam.target.z - cam.eye.z };
                        const float fl = std::sqrt(fwd.x * fwd.x + fwd.y * fwd.y + fwd.z * fwd.z);
                        if (fl < 1e-5F)
                            return std::nullopt;
                        fwd.x /= fl;
                        fwd.y /= fl;
                        fwd.z /= fl;
                        cd::math::Vec3f wup { 0, 1, 0 };
                        cd::math::Vec3f rgt { fwd.y * wup.z - fwd.z * wup.y,
                                              fwd.z * wup.x - fwd.x * wup.z,
                                              fwd.x * wup.y - fwd.y * wup.x };
                        const float rl = std::sqrt(rgt.x * rgt.x + rgt.y * rgt.y + rgt.z * rgt.z);
                        if (rl < 1e-5F)
                            return std::nullopt;
                        rgt.x /= rl;
                        rgt.y /= rl;
                        rgt.z /= rl;
                        cd::math::Vec3f up_v { rgt.y * fwd.z - rgt.z * fwd.y,
                                               rgt.z * fwd.x - rgt.x * fwd.z,
                                               rgt.x * fwd.y - rgt.y * fwd.x };
                        const float ndc_x = (2.0F * mouse_pixel.x / vw) - 1.0F;
                        const float ndc_y = 1.0F - (2.0F * mouse_pixel.y / vh);
                        const float tan_half = std::tan(cam.fov_y * 0.5F);
                        const float sx = (vw / vh) * tan_half;
                        const float sy = tan_half;
                        cd::math::Vec3f rdir { fwd.x + rgt.x * ndc_x * sx + up_v.x * ndc_y * sy,
                                               fwd.y + rgt.y * ndc_x * sx + up_v.y * ndc_y * sy,
                                               fwd.z + rgt.z * ndc_x * sx + up_v.z * ndc_y * sy };
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
                        cd::math::Vec3f c1 { fwd.y * a.z - fwd.z * a.y,
                                             fwd.z * a.x - fwd.x * a.z,
                                             fwd.x * a.y - fwd.y * a.x };
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
                        gizmo_drag_anchor = mp;
                        gizmo_drag_world_start = *target_pos;
                        if (lt != nullptr)
                        {
                            gizmo_drag_scale_start = lt->value.scale;
                            gizmo_drag_rot_start = lt->value.rotation;
                        }
                        // Capture light start state for gaps #16/#17:
                        // R-mode rotates light.direction; S-mode scales
                        // light.range / area_width / area_height.
                        if (selected_kind == SelKind::kLight && selected >= 0 &&
                            selected < static_cast<int>(lights.size()))
                        {
                            const auto& L = lights[static_cast<std::size_t>(selected)].light;
                            light_drag_dir_start = L.direction;
                            light_drag_tangent_start = L.area_tangent;
                            light_drag_range_start = L.range;
                            light_drag_area_w_start = L.area_width;
                            light_drag_area_h_start = L.area_height;
                        }
                        // Capture the initial ray-plane axis offset
                        // so subsequent moves give delta = current -
                        // initial (no jump at click).
                        if (auto off = ray_axis_offset(best, mp, *target_pos); off.has_value())
                        {
                            gizmo_drag_initial_offset = *off;
                            gizmo_drag_use_ray_plane = true;
                        }
                        else
                        {
                            gizmo_drag_use_ray_plane = false;
                        }
                    }
                    if (gizmo.is_dragging())
                    {
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
                        if (gizmo_drag_use_ray_plane && gizmo_mode == GizmoMode::kTranslate)
                        {
                            if (auto off = ray_axis_offset(gizmo.active_axis(), mp, gizmo_drag_world_start);
                                off.has_value())
                            {
                                delta_world = *off - gizmo_drag_initial_offset;
                            }
                        }
                        if (ax_len_px > 1.0F)
                        {
                            // Screen-space path (rotate/scale, or
                            // ray-plane fallback). delta_world stays 0
                            // for translate when ray-plane worked.
                            const float nx = ax_dx / ax_len_px, ny = ax_dy / ax_len_px;
                            const float mouse_dx = mp.x - gizmo_drag_anchor.x;
                            const float mouse_dy = mp.y - gizmo_drag_anchor.y;
                            const float dot_px = mouse_dx * nx + mouse_dy * ny;
                            const float world_per_px = kAxisLen / ax_len_px;
                            if (!gizmo_drag_use_ray_plane || gizmo_mode != GizmoMode::kTranslate)
                            {
                                delta_world = dot_px * world_per_px;
                            }
                        }
                        if (ax_len_px > 1.0F || gizmo_drag_use_ray_plane)
                        {
                            // Only translate works for both entities and
                            // lights; rotate/scale need a transform record
                            // and are gated on lt != nullptr.
                            switch (gizmo_mode)
                            {
                                case GizmoMode::kTranslate:
                                {
                                    cd::math::Vec3f cur = gizmo_drag_world_start;
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
                                        cd::math::Vec3f cur = gizmo_drag_scale_start;
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
                                            default:
                                                break;
                                        }
                                        if (cur.x < 0.05F)
                                            cur.x = 0.05F;
                                        if (cur.y < 0.05F)
                                            cur.y = 0.05F;
                                        if (cur.z < 0.05F)
                                            cur.z = 0.05F;
                                        lt->value.scale = cur;
                                    }
                                    else if (selected_kind == SelKind::kLight && selected >= 0 &&
                                             selected < static_cast<int>(lights.size()))
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
                                            float r = light_drag_range_start * factor;
                                            if (r < 0.1F)
                                                r = 0.1F;
                                            if (r > 200.0F)
                                                r = 200.0F;
                                            L.range = r;
                                        }
                                        else if (L.type == cd::light::LightType::kRectArea)
                                        {
                                            float w = light_drag_area_w_start;
                                            float h = light_drag_area_h_start;
                                            if (axis == cd::editor::GizmoAxis::kX || axis == cd::editor::GizmoAxis::kZ)
                                                w *= factor;
                                            if (axis == cd::editor::GizmoAxis::kY || axis == cd::editor::GizmoAxis::kZ)
                                                h *= factor;
                                            L.area_width = std::clamp(w, 0.05F, 50.0F);
                                            L.area_height = std::clamp(h, 0.05F, 50.0F);
                                        }
                                        else if (L.type == cd::light::LightType::kDiskArea)
                                        {
                                            float w = light_drag_area_w_start * factor;
                                            L.area_width = std::clamp(w, 0.05F, 50.0F);
                                            L.area_height = L.area_width;  // radius
                                        }
                                    }
                                    break;
                                }
                                case GizmoMode::kRotate:
                                {
                                    // Compute the angle the mouse has swept around the
                                    // gizmo center since drag start (atan2 difference).
                                    const float anchor_dx = gizmo_drag_anchor.x - p_org.x;
                                    const float anchor_dy = gizmo_drag_anchor.y - p_org.y;
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
                                    while (ang > 3.1415926F)
                                        ang -= 6.2831853F;
                                    while (ang < -3.1415926F)
                                        ang += 6.2831853F;
                                    const float ca = std::cos(ang * 0.5F);
                                    const float sa = std::sin(ang * 0.5F);
                                    cd::math::Quatf q { 0, 0, 0, 1 };
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
                                    if (lt != nullptr)
                                    {
                                        const auto& a = q;
                                        const auto& b = gizmo_drag_rot_start;
                                        lt->value.rotation =
                                            cd::math::Quatf { a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
                                                              a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
                                                              a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
                                                              a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z };
                                    }
                                    else if (selected_kind == SelKind::kLight && selected >= 0 &&
                                             selected < static_cast<int>(lights.size()))
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
                                        const auto rd_dir = rotate_v(light_drag_dir_start);
                                        Lr.direction = norm_v(rd_dir);
                                        // Tangent rotates too (only meaningful
                                        // for area lights; harmless for spot /
                                        // point — direction-derived rendering
                                        // ignores tangent there).
                                        auto rt = rotate_v(light_drag_tangent_start);
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
                        }
                        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
                        {
                            const auto delta = gizmo.end_drag();
                            (void)delta;
                            switch (gizmo_mode)
                            {
                                case GizmoMode::kTranslate:
                                {
                                    const float dx = target_pos->x - gizmo_drag_world_start.x;
                                    const float dy = target_pos->y - gizmo_drag_world_start.y;
                                    const float dz = target_pos->z - gizmo_drag_world_start.z;
                                    if (std::abs(dx) + std::abs(dy) + std::abs(dz) > 1e-4F)
                                    {
                                        if (target_is_entity && lt != nullptr)
                                        {
                                            // Roll back live mutation + push undoable command.
                                            *target_pos = gizmo_drag_world_start;
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
                                            // Light translate - apply directly (no history wire yet).
                                            // W8-AL: removed W8-X auto-aim-at-origin after
                                            // translate. User reported "otomatik merkeze
                                            // odaklaniyor" — the magic auto-rotation was
                                            // annoying because it overrode their manual
                                            // rotation immediately after a move. The
                                            // "Aim at origin" panel button (W8-V) is still
                                            // available for one-click manual re-aim.
                                            log_push("[gizmo] light translate applied");
                                        }
                                    }
                                    break;
                                }
                                case GizmoMode::kScale:
                                    if (lt != nullptr)
                                        log_push("[gizmo] scale applied");
                                    break;
                                case GizmoMode::kRotate:
                                    if (lt != nullptr)
                                        log_push("[gizmo] rotate applied");
                                    break;
                            }
                        }
                    }
                }
            }
        }

        // ---- Palette popup ----
        if (palette_visible)
        {
            const float vw_p = static_cast<float>(frame.extent.width);
            const float pw = 520.0F, ph = 360.0F;
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

        // R3: end the HDR scene pass, transition HDR -> ShaderResource,
        // begin the composite render pass on the swapchain, draw the
        // fullscreen-triangle composite material (which samples HDR
        // and applies tonemap + saturation + gamma), then let ImGui
        // draw on top.
        cmd.end_render_pass();
        {
            // Four barriers: HDR + 3 G-Buffer colour targets -> ShaderResource.
            // Depth handled separately below - needs an intermediate
            // kDepthRead state for the velocity pass before flipping to
            // ShaderResource for composite.
            std::array<cd::rhi::TextureBarrier, 4> hb {
                cd::rhi::TextureBarrier { .texture = hdr_target.image,
                                         .from = cd::rhi::ResourceState::kColorAttachment,
                                         .to = cd::rhi::ResourceState::kShaderResource,
                                         .range = { 0, 1, 0, 1 } },
                cd::rhi::TextureBarrier { .texture = gbuf_normal.image,
                                         .from = cd::rhi::ResourceState::kColorAttachment,
                                         .to = cd::rhi::ResourceState::kShaderResource,
                                         .range = { 0, 1, 0, 1 } },
                cd::rhi::TextureBarrier { .texture = gbuf_albedo.image,
                                         .from = cd::rhi::ResourceState::kColorAttachment,
                                         .to = cd::rhi::ResourceState::kShaderResource,
                                         .range = { 0, 1, 0, 1 } },
                cd::rhi::TextureBarrier { .texture = gbuf_mr.image,
                                         .from = cd::rhi::ResourceState::kColorAttachment,
                                         .to = cd::rhi::ResourceState::kShaderResource,
                                         .range = { 0, 1, 0, 1 } }
            };
            cmd.barrier({}, hb);
        }

        // R3 phase 227 - velocity G-Buffer pass. Re-draws every entity
        // mesh using velocity_material; FS writes (curr_uv - prev_uv).
        // Depth attachment in kDepthRead so the velocity raster matches
        // the HDR pass's visible surface per pixel (no overdraw soup).
        {
            const cd::rhi::ResourceState vel_prev =
                (frame_idx == 0) ? cd::rhi::ResourceState::kUndefined : cd::rhi::ResourceState::kShaderResource;
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
                frame.extent
            };
            vel_rp.color_attachments = vel_ca;
            vel_rp.depth_stencil = &vel_depth;
            cmd.begin_render_pass(vel_rp);
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
            velocity_material.apply(cmd);
            // Per-entity draw: push prev/curr vp*model, draw the same
            // mesh the HDR pass used. CesiumMan path: prim entities
            // share kPrimBindings layout so the velocity VS reads
            // their position attribute without a re-bind shape change.
            for (auto& ent : entities)
            {
                const auto& mesh = mesh_for(ent.kind);
                if (!mesh.vb.is_valid())
                    continue;
                auto* lt = scene.local(ent.handle);
                if (lt == nullptr)
                    continue;
                cmd.bind_vertex_buffer(0, mesh.vb, 0);
                cmd.bind_index_buffer(mesh.ib, 0, cd::rhi::IndexType::kUInt16);

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

        // R3 - Bloom chain. 7 fullscreen-triangle passes against the
        // dedicated bloom mip chain (each pass owns one render target,
        // writes its full extent, and ends as kShaderResource so the
        // next pass can sample it). All 7 share the composite VS.
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
            rp.render_area = cd::rhi::Rect2D {
                { 0, 0 },
                dst.extent
            };
            rp.color_attachments = ca;
            rp.depth_stencil = nullptr;
            cmd.begin_render_pass(rp);
            cmd.set_viewport(
                cd::rhi::Viewport { 0.0F,
                                    0.0F,
                                    static_cast<float>(dst.extent.width),
                                    static_cast<float>(dst.extent.height),
                                    0.0F,
                                    1.0F }
            );
            cmd.set_scissor(
                cd::rhi::Rect2D {
                    { 0, 0 },
                    dst.extent
            }
            );
            mat.apply(cmd);
            inst.bind(cmd, 0);
            if (!push_bytes.empty())
            {
                cmd.push_constants(
                    mat.pipeline_layout(),
                    cd::rhi::ShaderStage::kFragment,
                    0,
                    static_cast<std::uint32_t>(push_bytes.size()),
                    push_bytes.data()
                );
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

        const bool bloom_first_frame = (frame_idx == 0);
        // 1) Prefilter: HDR -> mip0 (soft-knee threshold).
        {
            BloomPrefilterPush bpp {};
            bpp.params[0] = 1.10F;  // threshold (linear HDR units)
            bpp.params[1] = 0.50F;  // knee
            bpp.params[2] = 0.0F;
            bpp.params[3] = 0.0F;
            std::span<const std::byte> bytes { reinterpret_cast<const std::byte*>(&bpp), sizeof(bpp) };
            run_bloom_pass(
                bloom_prefilter_material,
                bloom_prefilter_inst,
                bloom_chain.mips[0],
                cd::rhi::LoadOp::kClear,
                bytes,
                bloom_first_frame
            );
        }
        // 2) Downsample chain: mip0 -> 1, 1 -> 2, 2 -> 3.
        for (std::uint32_t i = 0; i < 3; ++i)
        {
            run_bloom_pass(
                bloom_downsample_material,
                bloom_down_insts[i],
                bloom_chain.mips[i + 1],
                cd::rhi::LoadOp::kClear,
                {},
                bloom_first_frame
            );
        }
        // 3) Upsample chain: mip3 -> 2, 2 -> 1, 1 -> 0 (additive blend).
        //    Load op must be Load to preserve the prior pass's output we're
        //    adding onto. radius 1.0 / intensity 1.0 (full contribution).
        for (std::uint32_t i = 0; i < 3; ++i)
        {
            const std::uint32_t dst_index = 3U - 1U - i;  // 2, 1, 0
            BloomUpsamplePush bup {};
            bup.params[0] = 1.0F;                         // radius (px scale)
            bup.params[1] = 1.0F;                         // intensity per level
            bup.params[2] = 0.0F;
            bup.params[3] = 0.0F;
            std::span<const std::byte> bytes { reinterpret_cast<const std::byte*>(&bup), sizeof(bup) };
            run_bloom_pass(
                bloom_upsample_material,
                bloom_up_insts[i],
                bloom_chain.mips[dst_index],
                cd::rhi::LoadOp::kLoad,
                bytes,
                bloom_first_frame
            );
        }

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
            cd::rhi::ColorAttachmentInfo { .view = frame.swapchain_image_view,
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
            frame.extent
        };
        swap_rp.color_attachments = swap_attach;
        swap_rp.depth_stencil = nullptr;
        cmd.begin_render_pass(swap_rp);
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
        composite_material.apply(cmd);
        composite_insts[read_idx].bind(cmd, 0);
        CompositePush cp {};
        cp.fx[0] = static_cast<float>(tonemap_op);
        cp.fx[1] = fx_exposure;
        cp.fx[2] = fx_saturation_boost;
        cp.fx[3] = fx_bloom_post;
        cp.ao[0] = fx_ao_strength;
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
        cp.dof[0] = fx_dof_strength;
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
        cp.sun_col[3] = fx_clouds_coverage;
        for (const auto& lrow : lights)
        {
            if (!lrow.enabled)
                continue;
            if (lrow.light.type != cd::light::LightType::kDirectional)
                continue;
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
            const float aspect_l = static_cast<float>(frame.extent.width) / static_cast<float>(frame.extent.height);
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
            cp.shafts[2] = fx_shafts_strength * edge_fade;
            // W4-E: gentler decay so shafts visibly reach across the
            // frame instead of dying within ~25% of UV distance from
            // sun. Was 3.5; 1.6 keeps shafts readable at the corners.
            cp.shafts[3] = 1.6F;  // decay (per UV distance)
            cp.sun_col[0] = lrow.light.color.x;
            cp.sun_col[1] = lrow.light.color.y;
            cp.sun_col[2] = lrow.light.color.z;
            // Preserve clouds_coverage (already set above before the loop).
            break;
        }
        // Atmospheric fog (uniform exp-haze) + aerial perspective (sky
        // horizon tint with distance). Reuses the existing UI sliders
        // so the composite is now the *one* home for these effects.
        cp.atmo[0] = fx_fog_density;
        cp.atmo[1] = fx_aerial_perspective;
        cp.atmo[2] = fx_vignette_strength;
        cp.atmo[3] = fx_film_grain;
        cp.lens[0] = fx_chromab_strength;
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
            const float aspect_l = static_cast<float>(frame.extent.width) / static_cast<float>(frame.extent.height);
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
            cp.cam_fwd[3] = (frame_idx > 0) ? fx_taa_amount : 0.0F;
            cp.cam_pos[0] = cam.eye.x;
            cp.cam_pos[1] = cam.eye.y;
            // W6-B: w slot carries the composite's anim-time (seconds
            // since the frame loop started) so post-fx that need a
            // monotonic clock — e.g. the volumetric-cloud drift — read
            // it without an extra push-constant slot or a global state
            // buffer. Use the frame-loop epoch instead of steady_clock
            // since-epoch so the noise stays in a sane numeric range.
            cp.cam_pos[2] = cam.eye.z;
            cp.cam_pos[3] = std::chrono::duration<float>(clock::now() - frame_loop_start).count();
        }
        // SSR - wired from the existing UI slider; defaults to 0 (off).
        cp.ssr[0] = fx_ssr_strength;
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
            cp.prev_cam_fwd[3] = fx_motion_blur;
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

        // ---- ImGui pass (on swapchain, after composite) ----
        ctx.render(cmd);
        cmd.end_render_pass();

        auto end_r = renderer.end_frame();
        if (!end_r.has_value())
        {
            if (end_r.error().code == static_cast<std::uint32_t>(cd::render::render_errors::Code::kSwapchainOutOfDate))
            {
                needs_rebuild = true;
                continue;
            }
            return 11;
        }
        ++frame_idx;
        counters.set("frame", frame_idx);
    }

    renderer.wait_idle();
    streamer.stop();

    // ---- Cleanup ----
    destroy_mesh(device, cube_mesh);
    destroy_mesh(device, sphere_mesh);
    destroy_mesh(device, cone_mesh);
    destroy_mesh(device, cyl_mesh);
    destroy_mesh(device, torus_mesh);
    destroy_mesh(device, knot_mesh);
    destroy_mesh(device, floor_mesh);
    depth.destroy(device);
    hdr_target.destroy(device);
    gbuf_normal.destroy(device);
    gbuf_albedo.destroy(device);
    gbuf_mr.destroy(device);
    gbuf_velocity.destroy(device);
    for (auto& h : history_targets)
        h.destroy(device);
    bloom_chain.destroy(device);
    // Faz 1.6 CSM resources.
    shadow_target.destroy(device);
    device.destroy_sampler(shadow_sampler);
    device.destroy_buffer(shadow_ubo);
    device.destroy_buffer(lights_ubo);
    device.destroy_buffer(inst_mat_ssbo);  // W8-BC
    if (albedo_tex.view.is_valid())
        device.destroy_texture_view(albedo_tex.view);
    if (albedo_tex.image.is_valid())
        device.destroy_texture(albedo_tex.image);
    device.destroy_sampler(albedo_sampler);
    device.destroy_sampler(ibl_sampler);  // bug-hunt: was leaked
    // R1 IBL textures + views.
    if (gpu_spec_cube.view.is_valid())
        device.destroy_texture_view(gpu_spec_cube.view);
    if (gpu_spec_cube.image.is_valid())
        device.destroy_texture(gpu_spec_cube.image);
    if (gpu_diff_cube.view.is_valid())
        device.destroy_texture_view(gpu_diff_cube.view);
    if (gpu_diff_cube.image.is_valid())
        device.destroy_texture(gpu_diff_cube.image);
    if (gpu_brdf_lut.view.is_valid())
        device.destroy_texture_view(gpu_brdf_lut.view);
    if (gpu_brdf_lut.image.is_valid())
        device.destroy_texture(gpu_brdf_lut.image);
    // R2 textures.
    if (normal_tex.view.is_valid())
        device.destroy_texture_view(normal_tex.view);
    if (normal_tex.image.is_valid())
        device.destroy_texture(normal_tex.image);
    if (mr_tex.view.is_valid())
        device.destroy_texture_view(mr_tex.view);
    if (mr_tex.image.is_valid())
        device.destroy_texture(mr_tex.image);
    // Faz 1.7 RT resources - wait_idle so any in-flight cmd buffers
    // that referenced these structures are guaranteed done, then
    // tear down the TLAS queue + every BLAS.
    device.wait_idle();
    if (current_tlas.is_valid())
        device.destroy_acceleration_structure(current_tlas);
    while (!tlas_destroy_queue.empty())
    {
        device.destroy_acceleration_structure(tlas_destroy_queue.front().h);
        tlas_destroy_queue.pop_front();
    }
    for (auto h : { blas_cube, blas_sphere, blas_cone, blas_cyl, blas_torus, blas_floor, blas_gltf })
        if (h.is_valid())
            device.destroy_acceleration_structure(h);
    if (gltf_mesh.vb.is_valid())
        destroy_mesh(device, gltf_mesh);
    std::printf("hello_engine: clean exit (%u frames).\n", frame_idx);
    return 0;
}
