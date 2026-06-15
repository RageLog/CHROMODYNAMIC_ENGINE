// =============================================================================
// HelloDdgi.hpp
// -----------------------------------------------------------------------------
// phase1146 (FEATURE-TIER capstone) — hello_engine-local aggregate that wires
// the cd::ddgi::FullPipeline (Majercik et al. 2019 Dynamic Diffuse Global
// Illumination) from the CPU-only R4-GI panel demo into a REAL per-frame GPU
// dispatch. The cd::ddgi library is complete; this aggregate owns the
// FullPipeline instance + the boot / bind / per-frame-execute glue so main()
// stays slim.
//
// Per-frame contract (driven by main.cpp on_frame, guarded by fx.ddgi_on &&
// tlas.is_valid()):
//   1. boot()   — once, after the first TLAS is built: init the FullPipeline
//                 against the device + a Sponza-aligned probe grid.
//   2. bind()   — after the render targets are (re)created (boot + resize):
//                 point the sample pass at the HDR scene image (output, ADD),
//                 the scene depth (world-pos reconstruct) and the G-buffer
//                 normal.
//   3. execute_frame() — every frame DDGI is on: refresh inv_vp + sun light,
//                 bind the live TLAS, transition the images, and record the
//                 four-pass DDGI chain so the indirect bounce ADDS into HDR
//                 before the composite pass reads it.
//
// DEFAULT-OFF discipline: nothing here runs unless main() calls execute_frame,
// which it gates on fx.ddgi_on (default false). The golden fixture path keeps
// DDGI off so the chrome golden stays byte-identical.
// =============================================================================
#pragma once

#include <cd/ddgi/FullPipeline.hpp>
#include <cd/math/Matrix.hpp>
#include <cd/math/Vector.hpp>
#include <cd/rhi/Barriers.hpp>
#include <cd/rhi/Enums.hpp>
#include <cd/rhi/ICommandBuffer.hpp>
#include <cd/rhi/IDevice.hpp>

#include <array>
#include <cstdint>

namespace cd_sample
{

// phase1146 (VERIFY step 4): force-on hook for capture-based proof that DDGI
// is NOT a no-op. Set by the `--ddgi-force-on` CLI flag (parsed in main()).
// Keeps DDGI out of the default + golden-fixture path (which never sets it):
// only an explicit capture run flips it. ORed into fx.ddgi_on in on_frame.
inline bool g_ddgi_force_on = false;

/// Owns the GPU-side DDGI FullPipeline + the per-frame wiring state.
struct HelloDdgi
{
    cd::ddgi::FullPipeline pipeline {};
    bool                   inited   { false };
    bool                   bound    { false };
    std::uint32_t          vp_w     { 0 };
    std::uint32_t          vp_h     { 0 };

    // ---- P5 — Sponza-aligned probe grid -----------------------------------
    // Origin + spacing chosen so the 8x4x8 lattice blankets the visible Sponza
    // nave (floor at y~0, columns up to y~6, nave ~[-10,10] x, depth ~[-5,15]).
    // 64 rays/probe + 0.97 hysteresis ≈ 33-frame temporal convergence
    // (Majercik 2019 §4 starter config).
    [[nodiscard]] static cd::ddgi::DispatchPassDesc sponza_desc()
    {
        cd::ddgi::DispatchPassDesc d {};
        d.grid.origin   = { -10.0F, 0.5F, -5.0F };
        d.grid.spacing  = { 2.5F, 1.5F, 2.5F };
        d.grid.probes_x = 8;
        d.grid.probes_y = 4;
        d.grid.probes_z = 8;
        d.settings.rays_per_probe = 64;
        d.settings.hysteresis     = 0.97F;
        d.settings.max_distance   = 30.0F;
        d.sky_color[0]  = 0.20F;
        d.sky_color[1]  = 0.25F;
        d.sky_color[2]  = 0.40F;
        d.needs_tlas    = true;          // real ray-query trace path
        d.probe_face_size = 8;
        return d;
    }

    // ---- P2 — boot: init the FullPipeline ---------------------------------
    [[nodiscard]] bool boot(cd::rhi::IDevice& device)
    {
        if (inited) return true;
        auto r = pipeline.init(device, sponza_desc());
        inited = r.has_value();
        return inited;
    }

    // ---- P3 — bind: point the sample pass at HDR + depth + normal ----------
    // Call after the render targets are (re)created. `output_view` is the HDR
    // scene image (indirect ADDS into it); `depth_view` is the scene depth
    // (world-pos reconstruct); `normal_view` is the G-buffer world normal.
    [[nodiscard]] bool bind(cd::rhi::IDevice&          device,
                            cd::rhi::TextureViewHandle output_view,
                            cd::rhi::TextureViewHandle depth_view,
                            cd::rhi::TextureViewHandle normal_view,
                            std::uint32_t              w,
                            std::uint32_t              h)
    {
        if (!inited) return false;
        auto r = pipeline.bind_sample_resources(device, output_view, depth_view,
                                                normal_view, w, h);
        bound = r.has_value();
        vp_w  = w;
        vp_h  = h;
        return bound;
    }

    // ---- P4 — execute one frame -------------------------------------------
    // Records the four-pass DDGI chain so the indirect bounce ADDS into the
    // HDR scene image. Caller has just closed the HDR scene render pass and
    // transitioned hdr/normal to kShaderResource and depth to kShaderResource
    // (see main.cpp). We:
    //   * upload inv_vp (sample pass) + sun (trace pass),
    //   * bind the live TLAS,
    //   * transition hdr -> UA, normal -> UA (depth stays kShaderResource),
    //   * execute(), then
    //   * transition hdr back -> kShaderResource for the composite read.
    void execute_frame(cd::rhi::IDevice&             device,
                       cd::rhi::ICommandBuffer&      cmd,
                       cd::rhi::TextureHandle        hdr_image,
                       cd::rhi::TextureHandle        normal_image,
                       cd::rhi::AccelStructureHandle tlas,
                       const cd::math::Mat4f&        vp,
                       const cd::math::Vec3f&        sun_dir,
                       const cd::math::Vec3f&        sun_col,
                       std::uint32_t                 frame_index)
    {
        if (!inited || !bound || !tlas.is_valid()) return;

        (void)pipeline.set_inv_vp(device, cd::math::inverse(vp));
        (void)pipeline.set_sun_light(device, sun_dir, sun_col, /*ambient=*/0.06F);
        (void)pipeline.pass().bind_tlas(device, tlas);

        // hdr + normal: kShaderResource -> kUnorderedAccess (sample pass
        // read-modify-writes hdr, reads normal). depth stays kShaderResource
        // (sampled). The pass-owned ray images + atlases start UNDEFINED on
        // the first execute and are held in kUnorderedAccess thereafter; we
        // transition them UNDEFINED -> kUnorderedAccess every frame (a
        // self-transition is legal and the validation layer accepts the
        // UNDEFINED -> UA discard each frame for these scratch atlases).
        {
            std::array<cd::rhi::TextureBarrier, 6> to_ua {
                cd::rhi::TextureBarrier { .texture = hdr_image,
                    .from = cd::rhi::ResourceState::kShaderResource,
                    .to   = cd::rhi::ResourceState::kUnorderedAccess,
                    .range = { 0, 1, 0, 1 } },
                cd::rhi::TextureBarrier { .texture = normal_image,
                    .from = cd::rhi::ResourceState::kShaderResource,
                    .to   = cd::rhi::ResourceState::kUnorderedAccess,
                    .range = { 0, 1, 0, 1 } },
                cd::rhi::TextureBarrier { .texture = pipeline.pass().ray_radiance(),
                    .from = ray_state_, .to = cd::rhi::ResourceState::kUnorderedAccess,
                    .range = { 0, 1, 0, 1 } },
                cd::rhi::TextureBarrier { .texture = pipeline.pass().ray_dir_dist(),
                    .from = ray_state_, .to = cd::rhi::ResourceState::kUnorderedAccess,
                    .range = { 0, 1, 0, 1 } },
                cd::rhi::TextureBarrier { .texture = pipeline.pass().irradiance_atlas(),
                    .from = atlas_state_, .to = cd::rhi::ResourceState::kUnorderedAccess,
                    .range = { 0, 1, 0, 1 } },
                cd::rhi::TextureBarrier { .texture = pipeline.pass().visibility_atlas(),
                    .from = atlas_state_, .to = cd::rhi::ResourceState::kUnorderedAccess,
                    .range = { 0, 1, 0, 1 } },
            };
            cmd.barrier({}, to_ua);
            ray_state_   = cd::rhi::ResourceState::kUnorderedAccess;
            atlas_state_ = cd::rhi::ResourceState::kUnorderedAccess;
        }

        (void)pipeline.execute(cmd, vp, tlas, frame_index);

        // hdr back -> kShaderResource for the composite read. normal is not
        // read by composite as a UA again, but it was kShaderResource before
        // DDGI; restore it so the post-DDGI barrier graph matches what the
        // composite + velocity passes expect.
        {
            std::array<cd::rhi::TextureBarrier, 2> to_sr {
                cd::rhi::TextureBarrier { .texture = hdr_image,
                    .from = cd::rhi::ResourceState::kUnorderedAccess,
                    .to   = cd::rhi::ResourceState::kShaderResource,
                    .range = { 0, 1, 0, 1 } },
                cd::rhi::TextureBarrier { .texture = normal_image,
                    .from = cd::rhi::ResourceState::kUnorderedAccess,
                    .to   = cd::rhi::ResourceState::kShaderResource,
                    .range = { 0, 1, 0, 1 } },
            };
            cmd.barrier({}, to_sr);
        }
    }

    void shutdown(cd::rhi::IDevice& device) noexcept
    {
        if (inited) pipeline.shutdown(device);
        inited = false;
        bound  = false;
        ray_state_   = cd::rhi::ResourceState::kUndefined;
        atlas_state_ = cd::rhi::ResourceState::kUndefined;
    }

private:
    // Per-frame resource-state tracking for the pass-owned scratch images so
    // the first execute discards from UNDEFINED and subsequent frames sync
    // from kUnorderedAccess (no redundant UNDEFINED discard that would drop
    // the temporally-accumulated atlas contents).
    cd::rhi::ResourceState ray_state_   { cd::rhi::ResourceState::kUndefined };
    cd::rhi::ResourceState atlas_state_ { cd::rhi::ResourceState::kUndefined };
};

}  // namespace cd_sample
