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
#include <functional>
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
#include "HelloEngineFx.hpp"
#include "HelloSkinned.hpp"
#include "HelloTlasRing.hpp"
#include "HelloTlasRebuild.hpp"
#include "HelloSkinnedAnim.hpp"
#include "HelloAudio.hpp"
#include "HelloIbl.hpp"
#include "HelloAppState.hpp"
#include "HelloRenderTargets.hpp"
#include "HelloMaterials.hpp"
#include "HelloGltf.hpp"
#include "HelloMeshes.hpp"
#include "HelloPicker.hpp"
#include "HelloPalette.hpp"


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
// Phase 300 / Marathon Run 8 sub-N2D: SelKind + LightRow lifted from main()
// scope to anon namespace so the Outliner / Lights / selection-overlay
// helpers can take them in their signatures. No semantic change - these
// are pure data types and were already aggregate-style; moving them up
// just lets the extracted UI helpers reference them by type name.
// =============================================================================

// Selection kind - entities and lights are both pickable.
enum class SelKind : std::uint8_t
{
    kEntity = 0,
    kLight = 1
};

// One row of the Lights panel: a Light record plus the inspector-side
// metadata (display name, enable bit, CCT slider value).
struct LightRow
{
    std::string name;
    cd::light::Light light;
    bool enabled { true };
    float kelvin { 6500.0F };  // mirrors light.color_kelvin
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
// Phase 298 / Marathon Run 8 sub-N2B: Audio / Net Sim / Streamer UI panel
// helpers extracted from main(). Audio + Streamer take a std::function<>
// callback for log_push / streamer_enqueue because the main()-scope
// originals are stateful lambdas closing over locals (log deque, streamer
// + tracked vector). std::function adds one virtual call per panel-frame -
// negligible against ImGui draw cost - and avoids restructuring the
// closures into namespace-level free helpers, which would force a larger
// refactor for marginal gain.
// =============================================================================

// ---- draw_audio_panel -----------------------------------------------------
inline void draw_audio_panel(bool audio_muted,
                             float audio_peak_window,
                             float audio_comp_db_window,
                             float audio_limiter_gain_min,
                             const std::deque<float>& audio_meter_history,
                             const std::vector<std::int16_t>& audio_ring,
                             std::size_t audio_ring_write,
                             std::uint64_t audio_total_written,
                             std::size_t kAudioRingFrames,
                             const std::function<void(std::string)>& log_push)
{
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
        push_le(static_cast<std::uint64_t>(kAudioSampleRate) * 1U * 2U, 4);  // byte rate
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
}

// ---- draw_net_sim_panel ---------------------------------------------------
inline void draw_net_sim_panel(bool net_enabled,
                               std::uint32_t net_sent,
                               std::uint32_t net_recv,
                               std::uint32_t net_drop,
                               std::uint64_t net_raw_bytes,
                               std::uint64_t net_wire_bytes,
                               const cd::net::LatencyStats& net_rtt,
                               const cd::net::SnapshotBuffer<float>& net_snapbuf)
{
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
}

// ---- draw_streamer_panel --------------------------------------------------
inline void draw_streamer_panel(cd::asset::AsyncStreamer& streamer,
                                const std::atomic<std::uint32_t>& streamer_completed,
                                const std::atomic<std::uint32_t>& streamer_failed,
                                const std::vector<cd::asset::AssetId>& streamer_tracked,
                                const std::function<void(std::int32_t)>& streamer_enqueue)
{
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
}

// =============================================================================
// Phase 299 / Marathon Run 8 sub-N2C: Scene tree + Inspector UI panels
// extracted. Inspector is the largest single panel (drag-edit Transform with
// EditHistory drag-release commit semantics for Position / Rotation / Scale,
// plus material tint). The static drag-pre captures inside each
// DragFloat3 stay function-local, preserving original lifetime.
// =============================================================================

// ---- draw_scene_tree_panel ------------------------------------------------
inline void draw_scene_tree_panel(const std::vector<SceneEntity>& entities, int& selected)
{
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
}

// ---- draw_inspector_panel -------------------------------------------------
inline void draw_inspector_panel(std::vector<SceneEntity>& entities,
                                 int selected,
                                 cd::scene::Scene& scene,
                                 cd::editor::EditHistory& history,
                                 const std::function<void(std::string)>& log_push)
{
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
}

// =============================================================================
// Phase 300 / Marathon Run 8 sub-N2D: Outliner + Lights UI panels extracted.
// Lights is the largest single-panel extract in the marathon so far (~225
// lines). The per-frame CCT->RGB rebuild + ClusterGrid::assign side-effect
// loop moves into the helper alongside the UI itself - that loop is the
// data path the Lights panel exposes and decoupling them would just push
// shared state into the parameter list. SelKind + LightRow lifted to anon
// namespace in step 1 so the helpers can take them by type name.
// =============================================================================

// ---- draw_outliner_panel --------------------------------------------------
inline void draw_outliner_panel(const std::vector<SceneEntity>& entities,
                                const std::vector<LightRow>& lights,
                                int& selected,
                                SelKind& selected_kind,
                                const cd::world_container::World& cd_world)
{
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
    // bugprone-suspicious-stringview-data-usage: name() returns a string_view
    // that is not guaranteed to be null-terminated, so feed it through
    // std::string before handing the C-string to ImGui.
    const std::string cd_world_label { cd_world.name() };
    if (ImGui::TreeNodeEx(cd_world_label.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
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
}

// ---- draw_lights_panel ----------------------------------------------------
inline void draw_lights_panel(std::vector<LightRow>& lights,
                              int& selected,
                              SelKind& selected_kind,
                              cd::light::ClusterGrid& cluster_grid,
                              const cd::light::ClusterGridDesc& cluster_desc)
{
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
}

// =============================================================================
// Phase 301 / Marathon Run 8 sub-N2E: world-space overlay helpers extracted.
// Selection outline (entity-pick ring) + light source markers (per-light
// gizmos in the viewport: sun ray / point dot / spot cone / area rect with
// emit-normal arrow) both lift cleanly because they only project world to
// screen using the existing vp matrix + the renderer FrameContext extent,
// touch the SelKind/entities/lights state we already lifted in N2D, and
// draw via ImGui's GetBackgroundDrawList(). No shader/RHI side effects.
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
        const float vw = static_cast<float>(extent.width);
        const float vh = static_cast<float>(extent.height);
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
}

// ---- draw_light_markers_overlay -------------------------------------------
// Each enabled light gets a small visual in the viewport so the user can
// SEE where the lights are placed.
//   Directional: a yellow line from sky toward target (sun ray)
//   Point:       filled circle in light color + range ring
//   Spot:        filled circle at apex + cone wireframe (4 lines to far disk)
//   Rect area:   4 corners outlined in light color + emit-normal arrow
inline void draw_light_markers_overlay(const std::vector<LightRow>& lights,
                                       int selected,
                                       SelKind selected_kind,
                                       const cd::math::Mat4f& vp,
                                       cd::rhi::Extent2D extent)
{
    const float vw = static_cast<float>(extent.width);
    const float vh = static_cast<float>(extent.height);
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
        const float vw_p = static_cast<float>(extent.width);
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

// ---- draw_r_showcase_panel ------------------------------------------------
// Unified R1..R8 realism-roadmap toggle / slider surface. Lifted out of
// the main render loop in phase 305 (Marathon Run 9 sub-N3); all 26 fx
// floats + 2 ints + 5 bools now live behind a single HelloEngineFx ref
// instead of being stack vars in main(). The sun-direction sub-panel
// also needs the lights vector (first entry = directional sun) so we
// can renormalise + push intensity. log_push echoes preset clicks into
// the Edit History panel.
inline void draw_r_showcase_panel(cd_sample::HelloEngineFx& fx,
                                  std::vector<LightRow>& lights,
                                  const std::function<void(std::string)>& log_push)
{
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
            if (ImGui::RadioButton(labels[i], fx.view_mode == i))
                fx.view_mode = i;
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
        ImGui::SliderFloat("Clearcoat", &fx.clearcoat_strength, 0.0F, 1.0F);
        ImGui::SliderFloat("Sheen", &fx.sheen_strength, 0.0F, 1.0F);
        ImGui::SliderFloat("SSS (Burley)", &fx.sss_strength, 0.0F, 1.0F);
    }
    if (ImGui::CollapsingHeader("R7  Camera composition"))
    {
        ImGui::SliderFloat("Vignette", &fx.vignette_strength, 0.0F, 1.0F);
        ImGui::SliderFloat("ChromAberration", &fx.chromab_strength, 0.0F, 1.0F);
        ImGui::SliderFloat("Film grain", &fx.film_grain, 0.0F, 1.0F);
    }
    if (ImGui::CollapsingHeader("R4-FX  Inline scene post-fx (legacy)"))
    {
        ImGui::TextDisabled("DEPRECATED - composite owns the real versions.");
        ImGui::TextDisabled("Sliders disabled. Use R3 Composite post-fx panel.");
        ImGui::BeginDisabled();
        ImGui::SliderFloat("GTAO inline", &fx.gtao_strength, 0.0F, 1.0F);
        ImGui::SliderFloat("Bloom inline", &fx.bloom_strength, 0.0F, 1.0F);
        ImGui::SliderFloat("SMAA inline", &fx.smaa_strength, 0.0F, 1.0F);
        ImGui::SliderFloat("Height fog", &fx.fog_density, 0.0F, 1.0F);
        ImGui::SliderFloat("Aerial persp", &fx.aerial_perspective, 0.0F, 1.0F);
        ImGui::EndDisabled();
    }
    if (ImGui::CollapsingHeader("R3  Composite post-fx (live)", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::TextDisabled("single composite pass - AO/DOF/shafts/bloom/atmo");
        // W6-E: preset buttons — quick A/B between known-good visual
        // setups so the user doesn't have to remember every default.
        if (ImGui::Button("Defaults"))
        {
            fx.exposure = 3.0F;
            fx.saturation_boost = 1.50F;
            fx.bloom_post = 0.04F;
            fx.ao_strength = 0.55F;
            fx.dof_strength = 0.0F;
            fx.shafts_strength = 0.75F;
            fx.ssr_strength = 0.5F;
            fx.motion_blur = 0.0F;
            fx.taa_amount = 0.0F;
            fx.clouds_coverage = 0.0F;
            fx.fog_density = 0.0F;
            fx.aerial_perspective = 0.0F;
            fx.chromab_strength = 0.0F;
            fx.film_grain = 0.0F;
            fx.vignette_strength = 0.25F;
            log_push("[fx] Reset all composite knobs to defaults");
        }
        ImGui::SameLine();
        if (ImGui::Button("Cinematic"))
        {
            fx.exposure = 2.5F;
            fx.saturation_boost = 1.65F;
            fx.bloom_post = 0.08F;
            fx.ao_strength = 0.65F;
            fx.dof_strength = 0.35F;
            fx.shafts_strength = 0.85F;
            fx.ssr_strength = 0.55F;
            fx.motion_blur = 0.30F;
            fx.taa_amount = 0.80F;
            fx.clouds_coverage = 0.45F;
            fx.fog_density = 0.20F;
            fx.aerial_perspective = 0.50F;
            fx.chromab_strength = 0.25F;
            fx.film_grain = 0.15F;
            fx.vignette_strength = 0.40F;
            fx.tonemap_op = 2;  // Hable
            log_push("[fx] Cinematic preset");
        }
        ImGui::SameLine();
        if (ImGui::Button("Performance"))
        {
            fx.exposure = 1.5F;
            fx.saturation_boost = 1.20F;
            fx.bloom_post = 0.0F;
            fx.ao_strength = 0.0F;
            fx.dof_strength = 0.0F;
            fx.shafts_strength = 0.0F;
            fx.ssr_strength = 0.0F;
            fx.motion_blur = 0.0F;
            fx.taa_amount = 0.0F;
            fx.clouds_coverage = 0.0F;
            fx.fog_density = 0.0F;
            fx.aerial_perspective = 0.0F;
            fx.chromab_strength = 0.0F;
            fx.film_grain = 0.0F;
            fx.vignette_strength = 0.0F;
            fx.tonemap_op = 0;  // Narkowicz (cheapest)
            log_push("[fx] Performance preset (all post-fx off)");
        }
        ImGui::SameLine();
        if (ImGui::Button("HDR Demo"))
        {
            fx.exposure = 1.0F;
            fx.saturation_boost = 1.40F;
            fx.bloom_post = 0.12F;
            fx.ao_strength = 0.55F;
            fx.shafts_strength = 0.90F;
            fx.clouds_coverage = 0.30F;
            fx.fog_density = 0.0F;
            fx.chromab_strength = 0.15F;
            fx.vignette_strength = 0.30F;
            fx.tonemap_op = 3;  // AGX — best for wide DR
            log_push("[fx] HDR demo preset (AGX tonemap + wide DR)");
        }
        ImGui::SliderFloat("Exposure", &fx.exposure, 0.1F, 10.0F);
        ImGui::SliderFloat("Saturation boost", &fx.saturation_boost, 0.5F, 2.5F);
        ImGui::SliderFloat("Bloom strength", &fx.bloom_post, 0.0F, 0.30F);
        ImGui::SliderFloat("AO strength", &fx.ao_strength, 0.0F, 1.0F);
        ImGui::SliderFloat("DOF strength", &fx.dof_strength, 0.0F, 1.0F);
        ImGui::SliderFloat("Light shafts", &fx.shafts_strength, 0.0F, 1.5F);
        ImGui::SliderFloat("SSR strength", &fx.ssr_strength, 0.0F, 1.0F);
        ImGui::SliderFloat("Motion blur", &fx.motion_blur, 0.0F, 1.0F);
        ImGui::SliderFloat("TAA amount", &fx.taa_amount, 0.0F, 0.97F);
        ImGui::SliderFloat("Clouds coverage", &fx.clouds_coverage, 0.0F, 1.0F);
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
        ImGui::Checkbox("HDR10 request (composite op 4 ready; needs HDR display)", &fx.hdr10_request);
        ImGui::TextDisabled("Tonemap operator 4 = ST.2084 PQ encode (Rec.2020).");
        ImGui::TextDisabled("Swapchain colour-space already exposed via");
        ImGui::TextDisabled("rhi::ColorSpace::kHdr10St2084; activate by setting");
        ImGui::TextDisabled("rd.swapchain.colour_space at startup + restarting.");
    }
    ImGui::End();

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
        if (ubo.count >= cd::hello_engine::kMaxLights)
            break;
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

// ---- GizmoState ------------------------------------------------------------
// hello_engine-local state for the editor gizmo overlay (axis-translation
// + W8 rotate/scale modes + light-specific drag-start capture). Lifted out
// of main() in phase 313 (Marathon Run 10 N5-prep) so the overlay extract
// in N5 only needs one GizmoState& argument instead of 12+ refs.
//
// cd::editor::AxisGizmo is a separate object kept in main() because it owns
// its own published state surface (hover / active_axis / set_target / etc.)
// and we don't want to wrap it.
//
// gizmo_state.was_hovered crosses frames - the pick path runs earlier in the
// frame than this overlay so the previous frame's hover memo gates
// entity-pick when the user is starting a gizmo drag. One-frame lag is
// invisible at 60+ FPS.
enum class GizmoMode : std::uint8_t
{
    kTranslate = 0,
    kRotate = 1,
    kScale = 2
};

struct GizmoState
{
    bool visible { true };
    GizmoMode mode { GizmoMode::kTranslate };
    ImVec2 drag_anchor { 0, 0 };
    cd::math::Vec3f drag_world_start {};
    cd::math::Vec3f drag_scale_start { 1.0F, 1.0F, 1.0F };
    cd::math::Quatf drag_rot_start {};
    cd::math::Vec3f light_drag_dir_start { 0.0F, -1.0F, 0.0F };
    cd::math::Vec3f light_drag_tangent_start { 1.0F, 0.0F, 0.0F };
    float light_drag_range_start { 0.0F };
    float light_drag_area_w_start { 1.0F };
    float light_drag_area_h_start { 1.0F };
    float drag_initial_offset { 0.0F };
    bool drag_use_ray_plane { false };
    bool was_hovered { false };
};

// PrevCamBasis - camera basis snapshot used by composite for motion-blur
// reprojection. Populated AFTER each composite invoke so the next frame's
// reprojection sees t-1. First frame: prev = current (zero velocity).
struct PrevCamBasis
{
    cd::math::Vec3f right { 1.0F, 0.0F, 0.0F };
    cd::math::Vec3f up { 0.0F, 1.0F, 0.0F };
    cd::math::Vec3f fwd { 0.0F, 0.0F, -1.0F };
    cd::math::Vec3f pos { 0.0F, 0.0F, 0.0F };
    float half_w { 1.0F };
    float half_h { 1.0F };
    bool valid { false };
};

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
                                  const cd_sample::HelloEngineFx& fx,
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
inline void draw_sky_pass(cd::rhi::ICommandBuffer& cmd,
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
inline void draw_planar_shadows(cd::rhi::ICommandBuffer& cmd,
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
    if (!(sun.strength > 1e-4F && sun.dir.y < -1e-3F))
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
                                 const MeshFor& mesh_for)
{
    cd::math::Vec3f sd = sun.has_sun ? sun.dir : cd::math::Vec3f { -0.4F, -0.9F, -0.2F };
    {
        const float sd_len = std::sqrt(sd.x * sd.x + sd.y * sd.y + sd.z * sd.z);
        if (sd_len > 1e-4F) { sd.x /= sd_len; sd.y /= sd_len; sd.z /= sd_len; }
        else { sd = { 0.0F, -1.0F, 0.0F }; }
    }
    const cd::math::Vec3f eye { -sd.x * 30.0F, -sd.y * 30.0F, -sd.z * 30.0F };
    const cd::math::Vec3f tgt { 0.0F, 0.0F, 0.0F };
    const cd::math::Vec3f up =
        (std::fabs(sd.y) > 0.99F) ? cd::math::Vec3f { 0.0F, 0.0F, 1.0F } : cd::math::Vec3f { 0.0F, 1.0F, 0.0F };
    const auto light_view = cd::math::look_at(eye, tgt, up);
    const auto light_proj = cd::math::ortho(-25.0F, 25.0F, -25.0F, 25.0F, 0.1F, 60.0F);
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
            cd::concurrency::parallel_for(std::size_t { 0 }, entities.size(), [&](std::size_t i) {
                const auto& ent = entities[i];
                const auto& mesh = mesh_for(ent.kind);
                if (!mesh.vb.is_valid()) return;
                auto* lt = scene.local(ent.handle);
                if (lt == nullptr) return;
                const auto model = cd::math::to_mat4(lt->value);
                csm_light_mvp[i] = light_vp * model;
                csm_valid[i] = 1u;
            });
            for (std::size_t i = 0; i < entities.size(); ++i)
            {
                if (csm_valid[i] == 0u) continue;
                const auto& ent = entities[i];
                const auto& mesh = mesh_for(ent.kind);
                cmd.bind_vertex_buffer(0, mesh.vb, 0);
                cmd.bind_index_buffer(mesh.ib, 0, cd::rhi::IndexType::kUInt16);
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
            const float vw = static_cast<float>(extent.width);
            const float vh = static_cast<float>(extent.height);
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
                if (gizmo_state.mode == GizmoMode::kTranslate)
                {
                    arrowhead(p_org, p_x, cx);
                    arrowhead(p_org, p_y, cy);
                    arrowhead(p_org, p_z, cz);
                }
                else if (gizmo_state.mode == GizmoMode::kRotate)
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
                const char* mode_lbl = gizmo_state.mode == GizmoMode::kTranslate ? "T"
                                       : gizmo_state.mode == GizmoMode::kRotate  ? "R"
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
                if (gizmo_state.mode == GizmoMode::kRotate)
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
                gizmo_state.was_hovered = (best != cd::editor::GizmoAxis::kNone);

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
                        selected < static_cast<int>(lights.size()))
                    {
                        const auto& L = lights[static_cast<std::size_t>(selected)].light;
                        gizmo_state.light_drag_dir_start = L.direction;
                        gizmo_state.light_drag_tangent_start = L.area_tangent;
                        gizmo_state.light_drag_range_start = L.range;
                        gizmo_state.light_drag_area_w_start = L.area_width;
                        gizmo_state.light_drag_area_h_start = L.area_height;
                    }
                    // Capture the initial ray-plane axis offset
                    // so subsequent moves give delta = current -
                    // initial (no jump at click).
                    if (auto off = ray_axis_offset(best, mp, *target_pos); off.has_value())
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
                    if (gizmo_state.drag_use_ray_plane && gizmo_state.mode == GizmoMode::kTranslate)
                    {
                        if (auto off = ray_axis_offset(gizmo.active_axis(), mp, gizmo_state.drag_world_start);
                            off.has_value())
                        {
                            delta_world = *off - gizmo_state.drag_initial_offset;
                        }
                    }
                    if (ax_len_px > 1.0F)
                    {
                        // Screen-space path (rotate/scale, or
                        // ray-plane fallback). delta_world stays 0
                        // for translate when ray-plane worked.
                        const float nx = ax_dx / ax_len_px, ny = ax_dy / ax_len_px;
                        const float mouse_dx = mp.x - gizmo_state.drag_anchor.x;
                        const float mouse_dy = mp.y - gizmo_state.drag_anchor.y;
                        const float dot_px = mouse_dx * nx + mouse_dy * ny;
                        const float world_per_px = kAxisLen / ax_len_px;
                        if (!gizmo_state.drag_use_ray_plane || gizmo_state.mode != GizmoMode::kTranslate)
                        {
                            delta_world = dot_px * world_per_px;
                        }
                    }
                    if (ax_len_px > 1.0F || gizmo_state.drag_use_ray_plane)
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
                                        float r = gizmo_state.light_drag_range_start * factor;
                                        if (r < 0.1F)
                                            r = 0.1F;
                                        if (r > 200.0F)
                                            r = 200.0F;
                                        L.range = r;
                                    }
                                    else if (L.type == cd::light::LightType::kRectArea)
                                    {
                                        float w = gizmo_state.light_drag_area_w_start;
                                        float h = gizmo_state.light_drag_area_h_start;
                                        if (axis == cd::editor::GizmoAxis::kX || axis == cd::editor::GizmoAxis::kZ)
                                            w *= factor;
                                        if (axis == cd::editor::GizmoAxis::kY || axis == cd::editor::GizmoAxis::kZ)
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
                                    const auto& b = gizmo_state.drag_rot_start;
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
                                  const MeshFor& mesh_for)
{
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
inline void run_bloom_chain(cd::rhi::ICommandBuffer& cmd,
                            std::uint32_t frame_idx,
                            BloomMipChain& bloom_chain,
                            cd::material::Material& bloom_prefilter_material,
                            cd::material::MaterialInstance& bloom_prefilter_inst,
                            cd::material::Material& bloom_downsample_material,
                            std::array<cd::material::MaterialInstance, 3>& bloom_down_insts,
                            cd::material::Material& bloom_upsample_material,
                            std::array<cd::material::MaterialInstance, 3>& bloom_up_insts)
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

    const bool bloom_first_frame = (frame_idx == 0);
    // 1) Prefilter: HDR -> mip0 (soft-knee threshold).
    {
        BloomPrefilterPush bpp {};
        bpp.params[0] = 1.10F;  // threshold (linear HDR units)
        bpp.params[1] = 0.50F;  // knee
        bpp.params[2] = 0.0F;
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
                                 const cd_sample::HelloEngineFx& fx,
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
        cp.sun_col[0] = lrow.light.color.x;
        cp.sun_col[1] = lrow.light.color.y;
        cp.sun_col[2] = lrow.light.color.z;
        // Preserve clouds_coverage (already set above before the loop).
        break;
    }
    // Atmospheric fog (uniform exp-haze) + aerial perspective (sky
    // horizon tint with distance). Reuses the existing UI sliders
    // so the composite is now the *one* home for these effects.
    cp.atmo[0] = fx.fog_density;
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
// entity passes need. cd::post_taa::jitter_offset(frame_idx, 8) is the
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
    rp.render_area = cd::rhi::Rect2D { { 0, 0 }, extent };
    rp.color_attachments = color_attach;
    rp.depth_stencil = &depth_attach;
    cmd.begin_render_pass(rp);
    cmd.set_viewport(cd::rhi::Viewport { 0.0F, 0.0F,
                                         static_cast<float>(extent.width),
                                         static_cast<float>(extent.height),
                                         0.0F, 1.0F });
    cmd.set_scissor(cd::rhi::Rect2D { { 0, 0 }, extent });

    HdrSceneFrame out {};
    out.aspect = static_cast<float>(extent.width) / static_cast<float>(extent.height);
    out.vp_unjittered = cd::camera::view_projection(cam, out.aspect);

    const cd::math::Vec2f jitter_px =
        (taa_amount > 0.001F) ? cd::post_taa::jitter_offset(frame_idx, 8U) : cd::math::Vec2f { 0.0F, 0.0F };
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
template <typename MeshFor>
inline void draw_floor_and_entities(cd::rhi::ICommandBuffer& cmd,
                                    const GpuMesh& floor_mesh,
                                    float floor_y,
                                    const cd::math::Mat4f& vp,
                                    const cd_sample::HelloEngineFx& fx,
                                    const SunLight& sun,
                                    const cd::camera::Camera& cam,
                                    std::vector<SceneEntity>& entities,
                                    const cd::scene::Scene& scene,
                                    bool has_gltf_texture,
                                    cd::material::Material& prim_material,
                                    cd::core::CounterTable& counters,
                                    const MeshFor& mesh_for)
{
    // ---- Floor (large flat quad) ----
    // Faz 1.5 - real geometry on which the planar-shadow pass can
    // project caster silhouettes. Floor sits at y = floor_y so the
    // front-row primitives (which extend ??0.5 m around y=0) just
    // touch it.
            {
        cmd.bind_vertex_buffer(0, floor_mesh.vb, 0);
        cmd.bind_index_buffer(floor_mesh.ib, 0, cd::rhi::IndexType::kUInt16);
        cd::math::Mat4f floor_model = cd::math::Mat4f::identity();
        floor_model[3][1] = floor_y;  // translate quad to y = floor_y
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
        fp.tint[3] = 2.0F;  // FS sentinel: enables analytic XZ grid overlay
        fill_prim_push_shared(fp, fx, sun, cam);
        // Floor overrides: opt out of texture path + GTAO crease darkening
        // (flat normal -> dFdx/dFdy=0); keep bloom on bright grid lines.
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
            fill_prim_push_shared(pp, fx, sun, cam);
            // ECS override: per-entity texture-path flag in fx_params[1]
            pp.fx_params[1] = (!ent.is_pbr && ent.kind == PrimitiveKind::kGltf && has_gltf_texture) ? 1.0F : 0.0F;
            if (ent.is_pbr)
            {
                pp.fx_params4[0] = ent.metallic;
                pp.fx_params4[1] = ent.roughness;
                pp.fx_params4[2] = 0.0F;
            }
            else
            {
                pp.fx_params4[0] = fx.clearcoat_strength;
                pp.fx_params4[1] = fx.sheen_strength;
                pp.fx_params4[2] = fx.sss_strength;
            }
            pp.fx_params4[3] = static_cast<float>(fx.view_mode);
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

    // ---- Render targets (depth + HDR + 4 G-buffer + 2 TAA history) ----
    // Bundled into cd_sample::RenderTargets in HelloRenderTargets.hpp
    // (Marathon Run 13 phase N15a). Format constants live there at
    // namespace scope and are re-exported via local using-aliases below
    // so the downstream pipeline / material descs read them unchanged.
    using cd_sample::kDepthFormat;
    using cd_sample::kHdrFormat;
    using cd_sample::kNormalFormat;
    using cd_sample::kAlbedoFormat;
    using cd_sample::kMrFormat;
    using cd_sample::kVelocityFormat;
    using cd_sample::kHistoryFormat;
    cd_sample::RenderTargets rts {};
    if (int rc = cd_sample::create_render_targets(
            device, { window.width(), window.height() }, rts);
        rc != 0)
    {
        return rc;
    }
    auto& depth          = rts.depth;
    auto& hdr_target     = rts.hdr;
    auto& gbuf_normal    = rts.gbuf_normal;
    auto& gbuf_albedo    = rts.gbuf_albedo;
    auto& gbuf_mr        = rts.gbuf_mr;
    auto& gbuf_velocity  = rts.gbuf_velocity;
    auto& history_targets = rts.history;
    bool depth_initialised_on_gpu = false;

    // ---- Materials (sky / composite / bloom x3 / prim / velocity / shadow) ----
    // Bundled into cd_sample::MaterialBundle in HelloMaterials.hpp
    // (Marathon Run 13 phase N15b).  Local references rebind the bundle
    // fields under the historical material names so downstream code
    // (MaterialInstance::create, .apply, draw_*) compiles unchanged.
    auto mat_r = cd_sample::spawn_materials(device, compiler.get());
    if (!mat_r.has_value())
    {
        return mat_r.error().exit_code;
    }
    auto& materials = *mat_r;
    auto& sky_material              = materials.sky;
    auto& composite_material        = materials.composite;
    auto& bloom_prefilter_material  = materials.bloom_prefilter;
    auto& bloom_downsample_material = materials.bloom_downsample;
    auto& bloom_upsample_material   = materials.bloom_upsample;
    auto& prim_material             = materials.prim;
    [[maybe_unused]] auto& velocity_material = materials.velocity;
    auto& shadow_material           = materials.shadow;

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
    // X1C boot JobGraph + the GPU upload + sampler creation moved to
    // cd_sample::bake_ibl_cpu + cd_sample::upload_ibl_gpu in
    // HelloIbl.hpp (Marathon Run 11 phase N12). Same W8-AW chrome-
    // mirror quality parameters (env 128, spec base 128 / 6 mips /
    // 32 samples, diff 16 / 16 samples, BRDF 64x64 / 256 samples) and
    // same X1C parallelism. The sun unit vector defaults match the
    // pre-extract kIblSunDirToward = (0.3, 0.9, 0.2) value.
    constexpr cd::math::Vec3f kIblSunDirToward { 0.3F, 0.9F, 0.2F };
    const cd::math::Vec3f kIblSunUnit =
        cd_sample::normalize_dir(kIblSunDirToward);
    auto ibl_cpu = cd_sample::bake_ibl_cpu(kIblSunUnit);
    if (!ibl_cpu.ok)
        return 23;
    auto ibl_gpu_r = cd_sample::upload_ibl_gpu(device, ibl_cpu);
    if (!ibl_gpu_r.has_value())
        return ibl_gpu_r.error();
    auto ibl_gpu = *ibl_gpu_r;
    auto& earth_albedo_cpu  = ibl_cpu.earth_albedo;
    auto& earth_normal_cpu  = ibl_cpu.earth_normal;
    auto& earth_mr_cpu      = ibl_cpu.earth_mr;
    constexpr std::uint32_t kTexSize    = cd_sample::kHelloIblEarthAlbedoSize;
    constexpr std::uint32_t kNormalSize = cd_sample::kHelloIblEarthNormalSize;
    constexpr std::uint32_t kMrSize     = cd_sample::kHelloIblEarthMrSize;
    const auto& gpu_spec_cube = ibl_gpu.gpu_spec_cube;
    const auto& gpu_diff_cube = ibl_gpu.gpu_diff_cube;
    const auto& gpu_brdf_lut  = ibl_gpu.gpu_brdf_lut;
    const auto ibl_sampler    = ibl_gpu.ibl_sampler;

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

    // ---- Meshes + glTF + per-mesh BLAS (Marathon Run 16 phase NF1 -> HelloMeshes.hpp) ----
    auto upload_albedo_fn = [&device](const std::uint8_t* rgba, std::uint32_t w, std::uint32_t h)
        -> std::pair<cd::rhi::TextureHandle, cd::rhi::TextureViewHandle>
    {
        auto tex = create_texture_rgba8(device, rgba, w, h);
        return { tex.image, tex.view };
    };
    cd_sample::AlbedoSlot meshes_albedo_slot {
        .image_io = &albedo_tex.image,
        .view_io  = &albedo_tex.view,
        .sampler  = albedo_sampler,
        .upload   = upload_albedo_fn,
    };
    auto meshes = cd_sample::boot_meshes(device, meshes_albedo_slot, prim_inst);
    if (meshes.build_exit_code != 0)
        return meshes.build_exit_code;
    if (meshes.has_gltf_texture)
        has_gltf_texture = true;
    auto& cube_mesh        = meshes.cube;
    auto& sphere_mesh      = meshes.sphere;
    auto& cone_mesh        = meshes.cone;
    auto& cyl_mesh         = meshes.cyl;
    auto& torus_mesh       = meshes.torus;
    auto& floor_mesh       = meshes.floor;
    auto& knot_mesh        = meshes.knot;
    auto& gltf_mesh        = meshes.gltf;
    auto& gltf_loaded_name = meshes.gltf_loaded_name;
    auto& skinned          = meshes.skinned;
    auto& blas_cube        = meshes.blas_cube;
    auto& blas_sphere      = meshes.blas_sphere;
    auto& blas_cone        = meshes.blas_cone;
    auto& blas_cyl         = meshes.blas_cyl;
    auto& blas_torus       = meshes.blas_torus;
    auto& blas_floor       = meshes.blas_floor;
    auto& blas_gltf        = meshes.blas_gltf;
    auto mesh_for = [&](PrimitiveKind k) -> const GpuMesh&
    {
        switch (k)
        {
            case PrimitiveKind::kSphere:   return sphere_mesh;
            case PrimitiveKind::kCone:     return cone_mesh;
            case PrimitiveKind::kCylinder: return cyl_mesh;
            case PrimitiveKind::kTorus:    return torus_mesh;
            case PrimitiveKind::kGltf:     return gltf_mesh.vb.is_valid() ? gltf_mesh : knot_mesh;
            default:                       return cube_mesh;
        }
    };
    auto blas_for_kind = [&](PrimitiveKind k) -> cd::rhi::AccelStructureHandle
    {
        switch (k)
        {
            case PrimitiveKind::kSphere:   return blas_sphere;
            case PrimitiveKind::kCone:     return blas_cone;
            case PrimitiveKind::kCylinder: return blas_cyl;
            case PrimitiveKind::kTorus:    return blas_torus;
            case PrimitiveKind::kGltf:     return blas_gltf;
            default:                       return blas_cube;
        }
    };
    cd::rhi::AccelStructureHandle current_tlas {};
    std::deque<cd_sample::DeferredTlas> tlas_destroy_queue;

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
    SelKind selected_kind = SelKind::kEntity;

    // Phase 151 - selection-outline state. Style defaults to
    // kWireframe (the cheapest of the three documented techniques
    // and the one we draw as an ImGui foreground overlay below).
    cd::editor::SelectionOutline outline;
    outline.style = cd::editor::OutlineStyle::kWireframe;

    // Phase 152 - axis-translation gizmo + UI bookkeeping. State lifted to
    // GizmoState aggregate in phase 313 (Marathon Run 10 N5-prep).
    cd::editor::AxisGizmo gizmo;
    GizmoState gizmo_state {};

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

    // ---- Free-look camera + pick state (Run 12 phase N13) ----
    // Bundled into cd_sample::SampleAppState so the WASD/look/zoom handler
    // and the 3D click-to-pick handler can be extracted out of main() in
    // follow-up phases (A6 picker, A7 FPS camera) without 20 reference
    // captures. Name-aliases below keep the existing inline call sites
    // (input handler, camera tick, picker) working without a mechanical
    // rename pass — same pattern HelloAudio used for the audio chain
    // in Marathon Run 11 phase N11.
    cd_sample::SampleAppState app_state;
    auto& cam_right_drag = app_state.free_look.right_drag;
    auto& cam_yaw        = app_state.free_look.yaw;
    auto& cam_pitch      = app_state.free_look.pitch;
    auto& cam_dist       = app_state.free_look.dist;
    auto& last_mouse_x   = app_state.free_look.last_mouse_x;
    auto& last_mouse_y   = app_state.free_look.last_mouse_y;
    auto& has_last_mouse = app_state.free_look.has_last_mouse;
    auto& key_w          = app_state.free_look.key_w;
    auto& key_a          = app_state.free_look.key_a;
    auto& key_s          = app_state.free_look.key_s;
    auto& key_d          = app_state.free_look.key_d;
    auto& key_q          = app_state.free_look.key_q;
    auto& key_e          = app_state.free_look.key_e;
    auto& key_shift      = app_state.free_look.key_shift;
    auto& key_ctrl       = app_state.free_look.key_ctrl;
    auto& cam_manual_mode = app_state.free_look.manual_mode;
    auto& pending_pick   = app_state.pick.pending;
    auto& pick_x         = app_state.pick.x;
    auto& pick_y         = app_state.pick.y;
    using cd_sample::kCamMoveSpeed;
    using cd_sample::kCamLookSpeed;


    // ---- Audio chain (continuous tick) ----
    // The DSP-chain + meter / ring / WASAPI live-playback boot block
    // moved to cd_sample::AudioState + init_audio in HelloAudio.hpp
    // (Marathon Run 11 phase N11). Name-aliases follow so the rest of
    // main() and the existing draw_audio_panel call continue to read
    // audio_bus / audio_muted / audio_ring / audio_t / etc. without a
    // mechanical rename pass.
    cd_sample::AudioState audio_state;
    cd_sample::init_audio(audio_state, square_wave, burst_noise);
    auto& audio_bus              = audio_state.bus;
    auto& comp                   = audio_state.comp;
    auto& reverb                 = audio_state.reverb;
    auto& lowpass                = audio_state.lowpass;
    auto& limiter                = audio_state.limiter;
    auto& audio_t                = audio_state.sample_t;
    auto& audio_muted            = audio_state.muted;
    auto& audio_peak_window      = audio_state.peak_window;
    auto& audio_comp_db_window   = audio_state.comp_db_window;
    auto& audio_limiter_gain_min = audio_state.limiter_gain_min;
    auto& audio_meter_history    = audio_state.meter_history;
    auto& audio_ring             = audio_state.ring;
    auto& audio_ring_write       = audio_state.ring_write;
    auto& audio_total_written    = audio_state.total_written;
    auto& audio_backend          = audio_state.backend;
    auto& live_voice             = audio_state.live_voice;
    auto& audio_live_ok          = audio_state.live_ok;
    using cd_sample::kAudioRingFrames;  // resolve bare refs in audio tick / panel call

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
    // each draw. fx.tonemap_op: 0=Narkowicz, 1=Hill, 2=Hable, 3=AGX.
    // Default = Hable (Uncharted 2). AGX desaturates the LDR-range
    // shading the sample produces; Hable preserves tints on the
    // front primitives + back metallic spheres. AGX still wins on
    // HDR-heavy frames - switch via palette ('Tonemap: AGX').
    cd_sample::HelloEngineFx fx {};
    // Defaults documented in HelloEngineFx.hpp (Hable tonemap, 0.55 AO,
    // 0.75 light shafts — matches Marathon Run 5..8 visual baseline).
    // Tonemap + BRDF/FX queued commands lifted to HelloPalette.hpp (N19).
    cd_sample::register_fx_palette_commands(palette, fx, log_push);
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
    // Composite tonemap/HDR knobs - own the entire post-fx settle here.
    // W4-E: bumped default 0.35 -> 0.75 so light shafts are obviously
    // visible on first run. User reported they were hard to read at the
    // previous default.

    PrevCamBasis prev_cam_basis {};

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
    // Debug view modes: 0 final, 1 albedo, 2 world normal, 3 MR map,
    // 4 AO, 5 normal-mapped surface normal, 6 vertex UVs.
    // (BRDF / FX queued toggles registered above via register_fx_palette_commands.)
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
    palette.register_command(
        110,
        "GI: Toggle ReSTIR DI (queued v1.7)",
        [&]
        {
            fx.restir_di_on = !fx.restir_di_on;
            log_push(fx.restir_di_on ? "[gi] ReSTIR DI queued (v1.7 needs RT compute pipe)" : "[gi] ReSTIR DI off");
        }
    );
    palette.register_command(
        111,
        "GI: Toggle ReSTIR GI (queued v1.7)",
        [&]
        {
            fx.restir_gi_on = !fx.restir_gi_on;
            log_push(fx.restir_gi_on ? "[gi] ReSTIR GI queued (v1.7 needs RT compute pipe)" : "[gi] ReSTIR GI off");
        }
    );
    palette.register_command(
        112,
        "GI: Toggle DDGI probe update (queued v1.7)",
        [&]
        {
            fx.ddgi_on = !fx.ddgi_on;
            log_push(fx.ddgi_on ? "[gi] DDGI queued (v1.7 needs probe-volume RT)" : "[gi] DDGI off");
        }
    );
    palette.register_command(
        113,
        "GI: Toggle NRC (TinyCudaNN backend, queued v1.7)",
        [&]
        {
            fx.nrc_on = !fx.nrc_on;
            log_push(fx.nrc_on ? "[gi] NRC queued (v1.7 needs CUDA inference path)" : "[gi] NRC off");
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
    palette.register_command(
        80,
        "FX: Toggle GTAO (inline approx)",
        [&]
        {
            fx.gtao_strength = (fx.gtao_strength > 0.001F) ? 0.0F : 0.65F;
            log_push(fx.gtao_strength > 0.001F ? "[fx] GTAO on" : "[fx] GTAO off");
        }
    );
    palette.register_command(
        81,
        "FX: Toggle Bloom (inline approx)",
        [&]
        {
            fx.bloom_strength = (fx.bloom_strength > 0.001F) ? 0.0F : 0.55F;
            log_push(fx.bloom_strength > 0.001F ? "[fx] Bloom on" : "[fx] Bloom off");
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
            fx.smaa_strength = (fx.smaa_strength > 0.001F) ? 0.0F : 0.55F;
            log_push(fx.smaa_strength > 0.001F ? "[fx] SMAA on" : "[fx] SMAA off");
        }
    );
    palette.register_command(
        85,
        "FX: Toggle Motion Blur",
        [&]
        {
            fx.motion_blur = (fx.motion_blur > 0.001F) ? 0.0F : 0.5F;
            log_push(
                fx.motion_blur > 0.001F ? "[fx] MotionBlur on (composite camera-velocity)" : "[fx] MotionBlur off"
            );
        }
    );
    palette.register_command(
        86,
        "FX: Toggle TAA",
        [&]
        {
            fx.taa_amount = (fx.taa_amount > 0.001F) ? 0.0F : 0.85F;
            log_push(fx.taa_amount > 0.001F ? "[fx] TAA on (history + Halton jitter)" : "[fx] TAA off");
        }
    );
    palette.register_command(
        87,
        "FX: Toggle DOF",
        [&]
        {
            fx.dof_strength = (fx.dof_strength > 0.001F) ? 0.0F : 0.5F;
            log_push(fx.dof_strength > 0.001F ? "[fx] DOF on (composite bokeh)" : "[fx] DOF off");
        }
    );
    palette.register_command(
        88,
        "FX: Toggle HDR10 (queued)",
        [&]
        {
            fx.hdr10_request = !fx.hdr10_request;
            log_push(fx.hdr10_request ? "[fx] HDR10 request queued (swapchain rework)" : "[fx] HDR10 off");
        }
    );
    palette.register_command(
        90,
        "FX: Toggle Height Fog (inline exp)",
        [&]
        {
            fx.fog_density = (fx.fog_density > 0.001F) ? 0.0F : 0.6F;
            log_push(fx.fog_density > 0.001F ? "[fx] Height fog on" : "[fx] Height fog off");
        }
    );
    palette.register_command(
        91,
        "FX: Toggle Aerial Perspective (inline)",
        [&]
        {
            fx.aerial_perspective = (fx.aerial_perspective > 0.001F) ? 0.0F : 0.7F;
            log_push(fx.aerial_perspective > 0.001F ? "[fx] Aerial perspective on" : "[fx] Aerial perspective off");
        }
    );
    palette.register_command(
        92,
        "FX: Toggle Clouds",
        [&]
        {
            fx.clouds_coverage = (fx.clouds_coverage > 0.001F) ? 0.0F : 0.55F;
            log_push(fx.clouds_coverage > 0.001F ? "[fx] Clouds on (composite fBm sky overlay)" : "[fx] Clouds off");
        }
    );
    palette.register_command(
        93,
        "FX: Toggle Light Shafts",
        [&]
        {
            fx.shafts_strength = (fx.shafts_strength > 0.001F) ? 0.0F : 0.5F;
            log_push(
                fx.shafts_strength > 0.001F ? "[fx] Light shafts on (Mitchell 2007 god rays)" : "[fx] Light shafts off"
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
            gizmo_state.visible = !gizmo_state.visible;
            log_push(std::string("[gizmo] visible=") + (gizmo_state.visible ? "true" : "false"));
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
                gizmo_state.mode = static_cast<GizmoMode>((static_cast<std::uint8_t>(gizmo_state.mode) + 1u) % 3u);
                const char* mode_str = gizmo_state.mode == GizmoMode::kTranslate ? "TRANSLATE"
                                       : gizmo_state.mode == GizmoMode::kRotate  ? "ROTATE"
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
            rts.destroy(device);
            if (cd_sample::create_render_targets(
                    device, { window.width(), window.height() }, rts) != 0)
            {
                continue;
            }
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
        // The CPU-LBS skinning logic lives in
        // cd_sample::update_skinned_animation (HelloSkinnedAnim.hpp).
        // Returns true if the skinned path ran; false (asset has no skin
        // data) drops into the W4-F fallback turntable below.
        if (!cd_sample::update_skinned_animation(skinned, device,
                                                 gltf_mesh.vb, dt))
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

        // ---- Update scene camera (Run 12 phase A7 / N14) ----
        // Per-frame camera tick extracted to cd_sample::update_free_look_camera
        // in HelloAppState.hpp. Resolves right-mouse-look + WASD/QE direct
        // drive vs scene_cam auto-orbit branch. Same bit-for-bit behaviour as
        // the pre-extract inline block, including the function-local-static
        // `wasd_was_active_prev` latch.
        cd_sample::update_free_look_camera(app_state.free_look, cam, scene_cam, dt);

        // ---- 3D click-to-pick (Marathon Run 14 phase N18) ----
        // Lifted to cd_sample::pick_entity_or_light in HelloPicker.hpp.
        // Same ray-build (FPS basis vs orbit basis), same sphere tests,
        // same light-vs-entity priority.
        if (pending_pick && gizmo_state.was_hovered)
        {
            pending_pick = false;
        }
        if (pending_pick)
        {
            pending_pick = false;
            const float vw = static_cast<float>(window.width());
            const float vh = static_cast<float>(window.height());
            if (vw > 0 && vh > 0)
            {
                const bool wasd_active = key_w || key_a || key_s || key_d || key_q || key_e;
                std::vector<cd_sample::EntityHit> ehits;
                ehits.reserve(entities.size());
                for (const auto& e : entities)
                    ehits.push_back({ e.handle, e.name });
                std::vector<cd_sample::LightHit> lhits;
                lhits.reserve(lights.size());
                for (const auto& l : lights)
                    lhits.push_back({ l.light.position, l.light.type, l.name });
                cd_sample::PickInputs pin {
                    .ndc_x           = (2.0F * pick_x / vw) - 1.0F,
                    .ndc_y           = 1.0F - (2.0F * pick_y / vh),
                    .aspect          = vw / vh,
                    .cam             = cam,
                    .cam_pitch       = cam_pitch,
                    .cam_yaw         = cam_yaw,
                    .wasd_active     = wasd_active,
                    .cam_right_drag  = cam_right_drag,
                    .gizmo_hovered   = false,  // outer if already handled gizmo hover
                };
                const auto pr = cd_sample::pick_entity_or_light(pin, ehits, lhits, scene);
                if (pr.kind == cd_sample::PickKind::kLight)
                {
                    selected = pr.index;
                    selected_kind = SelKind::kLight;
                    log_push(pr.log);
                }
                else if (pr.kind == cd_sample::PickKind::kEntity)
                {
                    selected = pr.index;
                    selected_kind = SelKind::kEntity;
                    log_push(pr.log);
                }
                else if (selected >= 0)
                {
                    log_push("[pick] cleared selection");
                    selected = -1;
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

        // ---- Faz 1.7 per-frame TLAS rebuild + depth ring barrier ----
        // Extracted to cd_sample::rebuild_tlas_and_transition_depth in
        // HelloTlasRebuild.hpp (Marathon Run 11 phase N9). Same descriptor
        // writes, same destroy-queue 3-frame margin, same parallel_for
        // entity scatter + serial compaction, same boot vs resume depth
        // transition. The five callables thread the EntityT / PrimitiveKind
        // anonymous-namespace types into the template without dragging
        // them out of main.cpp.
        cd_sample::rebuild_tlas_and_transition_depth(
            device,
            cmd,
            frame_idx,
            tlas_destroy_queue,
            current_tlas,
            prim_inst,
            std::span<const SceneEntity>(entities),
            blas_for_kind,
            [](const SceneEntity& e) -> cd::math::Vec3f { return e.tint; },
            [](const SceneEntity& e) -> PrimitiveKind { return e.kind; },
            [&](const SceneEntity& e) -> std::optional<cd::math::Mat4f>
            {
                auto* lt = scene.local(e.handle);
                if (lt == nullptr)
                    return std::nullopt;
                return cd::math::to_mat4(lt->value);
            },
            blas_floor,
            blas_gltf,
            skinned.valid,
            inst_mat_ssbo,
            depth.image,
            depth_initialised_on_gpu);

        // ---- Sun resolve (shadow + sky + floor + entity passes need it) ----
        const SunLight sun = resolve_sun_light(lights);

        // ---- Shadow map pass (Faz 1.6 CSM) ----
        draw_shadow_map_pass(cmd, sun, device, shadow_ubo, shadow_target,
                             shadow_initialised_on_gpu, shadow_material,
                             kShadowMapSize, entities, scene, mesh_for);

        // R3 HDR + G-Buffer scene pass open + Halton jitter VP.
        const auto hdr_frame = begin_hdr_scene_pass(cmd, frame_idx, hdr_target,
                                                    gbuf_normal, gbuf_albedo, gbuf_mr,
                                                    depth, frame.extent, cam, fx.taa_amount);
        const float aspect = hdr_frame.aspect;
        const cd::math::Mat4f vp_unjittered = hdr_frame.vp_unjittered;
        const cd::math::Mat4f vp = hdr_frame.vp;

        // ---- Sky pass ----
        draw_sky_pass(cmd, cam, aspect, sun, sky_material);

        // W8-AR: dedicated PBR-grid draw block REMOVED. The 16 PBR demo
        // spheres are ECS entities now (boot block, search "is_pbr =
        // true") and are drawn by the unified entity loop below alongside
        // every other primitive. One render path, one shader, one shadow
        // pass — PBR is just a per-entity attribute (SceneEntity::is_pbr).

        // ---- Multi-light UBO fill (gap #2) ----
        upload_multi_light_ubo(device, lights_ubo, lights, counters);

        prim_material.apply(cmd);
        prim_inst.bind(cmd, 0);  // Faz 1.6 CSM + Faz 1.9 light UBO

        // ---- Floor + ECS entity primitives row ----
        constexpr float kFloorY = -0.55F;
        constexpr float kShadowLift = 0.01F;
        draw_floor_and_entities(cmd, floor_mesh, kFloorY, vp, fx, sun, cam,
                                entities, scene, has_gltf_texture,
                                prim_material, counters, mesh_for);

        // ---- Planar projective shadows (Faz 1.5) ----
        draw_planar_shadows(cmd, sun, kFloorY, kShadowLift, entities,
                            scene, vp, prim_material, counters, mesh_for);

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
        draw_scene_tree_panel(entities, selected);

        // ---- Inspector ----
        draw_inspector_panel(entities, selected, scene, history, log_push);

        // ---- R-Showcase panel (unified R1..R8 toggles) ----
        draw_r_showcase_panel(fx, lights, log_push);

        // ---- Counters ----
        draw_counters_panel(counters, dt, frame_idx);

        // ---- Random viz ----
        draw_random_panel(hist_uniform, hist_normal);

        // ---- Audio ----
        draw_audio_panel(audio_muted, audio_peak_window, audio_comp_db_window, audio_limiter_gain_min, audio_meter_history, audio_ring, audio_ring_write, audio_total_written, kAudioRingFrames, log_push);

        // ---- Net Sim ----
        draw_net_sim_panel(net_enabled, net_sent, net_recv, net_drop, net_raw_bytes, net_wire_bytes, net_rtt, net_snapbuf);

        // ---- Streamer (Phase 150) ----
        draw_streamer_panel(streamer, streamer_completed, streamer_failed, streamer_tracked, streamer_enqueue);

        // ---- Outliner (gap #18 cd::world_container preview) ----
        draw_outliner_panel(entities, lights, selected, selected_kind, cd_world);

        // ---- Lights (Phase 171/172 - cd::light system) ----
        draw_lights_panel(lights, selected, selected_kind, cluster_grid, cluster_desc);

        // ---- History ----
        draw_history_panel(history, log);

        // ---- Phase 151 - selection outline (ImGui overlay) ----
        draw_selection_outline_overlay(entities, selected, selected_kind, outline, scene, vp, frame.extent);

        // ---- World grid (floor) ----
        // Moved into the floor fragment shader (analytic XZ grid with
        // fwidth-based line width). That respects the depth buffer so
        // the grid no longer shows through entities - user-flagged
        // "grid objeler arasindan gozukmemeli". The floor mesh draw
        // above sets tint[3] = 2.0 to enable that shader branch.

        // ---- Phase D - Light source markers (world-space overlay) ----
        draw_light_markers_overlay(lights, selected, selected_kind, vp, frame.extent);

        // ---- Phase 152 - axis-translation gizmo (ImGui overlay) ----
        update_and_draw_gizmo(gizmo, gizmo_state, pending_pick, selected, selected_kind,
                              entities, lights, scene, history, log_push,
                              vp, cam, window, frame.extent);

        // ---- Palette popup ----
        draw_command_palette_popup(palette, palette_visible, palette_query, frame.extent);

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

        // R3 phase 227 - velocity G-Buffer pass.
        velocity_gbuffer_pass(cmd, frame_idx, gbuf_velocity, depth, frame.extent,
                              velocity_material, entities, scene,
                              prev_vp_unjittered, vp_unjittered, mesh_for);

        // R3 - Bloom chain (7 passes: 1 prefilter + 3 down + 3 up).
        run_bloom_chain(cmd, frame_idx, bloom_chain,
                        bloom_prefilter_material, bloom_prefilter_inst,
                        bloom_downsample_material, bloom_down_insts,
                        bloom_upsample_material, bloom_up_insts);

        // ---- Composite + frame-feedback snapshot (R3) ----
        begin_composite_pass(cmd, frame_idx, frame.swapchain_image_view, frame.extent,
                             history_targets, history_states,
                             composite_material, composite_insts,
                             fx, lights, cam, frame_loop_start,
                             prev_cam_basis, prev_vp_unjittered, prev_vp_valid,
                             vp_unjittered);

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

    destroy_mesh(device, floor_mesh);
    rts.destroy(device);
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
    cd_sample::destroy_meshes(device, meshes);
    std::printf("hello_engine: clean exit (%u frames).\n", frame_idx);
    return 0;
}
