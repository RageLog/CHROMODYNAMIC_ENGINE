// =============================================================================
// HelloViewportDemos.hpp
// -----------------------------------------------------------------------------
// phase1076/1077 — HelloFrameLoop extraction, batches 1+2: ALL 18
// sphere-template 3D viewport demo blocks (phases 1005-1029) moved
// verbatim out of
// HelloEngineApp::on_frame. Each helper is a template over the
// EngineState type (defined in main.cpp) and the SunLight +
// fill_prim_push_shared types/callables (also main.cpp-local), so the
// header needs none of those definitions — the established
// draw_planar_shadows<> / draw_shadow_map_pass<> extraction pattern.
//
// Contract: every helper draws through s.materials.prim with PrimPush
// constants (HelloLighting.hpp) on the already-begun HDR scene pass;
// callers bind the prim pipeline before the demo run and the bodies
// re-bind their own vertex/index buffers per mesh.
// =============================================================================
#pragma once

#include "HelloLighting.hpp"

#include <cd/camera/Camera.hpp>
#include <cd/camera/Frustum.hpp>
#include <cd/decal/Decal.hpp>
#include <cd/light/Light.hpp>
#include <cd/math/Matrix.hpp>
#include <cd/math/Vector.hpp>
#include <cd/rhi/ICommandBuffer.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace cd_sample {

using cd::hello_engine::PrimPush;

template <typename StateT, typename SunT, typename FillFn>
inline void draw_ddgi_probe_overlay_3d(StateT& s,
                   cd::rhi::ICommandBuffer& cmd,
                   const cd::math::Mat4f& vp,
                   const SunT& sun,
                   FillFn&& fill_prim_push_shared)
{
        // phase1005-3d-viewport-ddgi-probe-overlay: when toggled on
        // (R-Showcase R4 GI "Show DDGI probes in 3D viewport"), emit
        // one small sphere per probe in an 8x4x8 grid centred at the
        // origin with 1 m spacing. Each probe is a normal prim draw
        // through s.materials.prim so it inherits lighting + shadows
        // + composite without a custom shader. Tint is a per-probe
        // hash so adjacent probes read distinctly (Godot SDFGI mode).
        if (s.fx.ddgi_show_probes_3d)
        {
            constexpr std::uint32_t kProbesX = 8;
            constexpr std::uint32_t kProbesY = 4;
            constexpr std::uint32_t kProbesZ = 8;
            constexpr float kSpacing = 1.0F;
            constexpr float kRadius  = 0.15F;
            constexpr cd::math::Vec3f kOrigin {
                -(static_cast<float>(kProbesX - 1) * 0.5F) * kSpacing,
                0.5F,
                -(static_cast<float>(kProbesZ - 1) * 0.5F) * kSpacing };
            const auto& probe_mesh = s.meshes.sphere;
            if (probe_mesh.vb.is_valid())
            {
                cmd.bind_vertex_buffer(0, probe_mesh.vb, 0);
                cmd.bind_index_buffer(probe_mesh.ib, 0, probe_mesh.index_type);
                for (std::uint32_t pz = 0; pz < kProbesZ; ++pz)
                for (std::uint32_t py = 0; py < kProbesY; ++py)
                for (std::uint32_t px = 0; px < kProbesX; ++px)
                {
                    const cd::math::Vec3f pos {
                        kOrigin.x + static_cast<float>(px) * kSpacing,
                        kOrigin.y + static_cast<float>(py) * kSpacing,
                        kOrigin.z + static_cast<float>(pz) * kSpacing };
                    // phase1017-ddgi-tint-semantics (Run 28 item #3):
                    // user feedback — 256 hash-coloured spheres read as
                    // NOISE, not information. Replace the index hash
                    // with a Y-layer altitude ramp (deep blue at floor
                    // layer -> green at mid -> warm amber at top) so the
                    // tint now answers "which probe LAYER am I looking
                    // at" at a glance — the same vertical-slice reading
                    // RTXGI's probe debug view gives.
                    const float layer_t = (kProbesY > 1)
                        ? static_cast<float>(py) / static_cast<float>(kProbesY - 1)
                        : 0.0F;
                    const float r = 0.15F + 0.80F * layer_t;
                    const float g = 0.35F + 0.45F * (1.0F - std::abs(layer_t - 0.5F) * 2.0F);
                    const float b = 0.90F - 0.75F * layer_t;
                    PrimPush pp {};
                    cd::math::Mat4f model { cd::math::Mat4f::identity() };
                    model[0][0] = kRadius;
                    model[1][1] = kRadius;
                    model[2][2] = kRadius;
                    model[3][0] = pos.x;
                    model[3][1] = pos.y;
                    model[3][2] = pos.z;
                    pp.model = model;
                    pp.mvp = vp * model;
                    pp.tint[0] = r;
                    pp.tint[1] = g;
                    pp.tint[2] = b;
                    pp.tint[3] = 1.0F;  // standard Lit path
                    fill_prim_push_shared(pp, s.fx, sun, s.cam);
                    pp.fx_params[1]  = 0.0F;  // no texture
                    pp.fx_params4[0] = 0.0F;  // dielectric
                    pp.fx_params4[1] = 0.7F;  // somewhat matte
                    cmd.push_constants(
                        s.materials.prim.pipeline_layout(),
                        cd::rhi::ShaderStage::kVertex | cd::rhi::ShaderStage::kFragment,
                        0, sizeof(pp), &pp);
                    cmd.draw_indexed(probe_mesh.index_count, 1, 0, 0, 0);
                    s.counters.increment("draws_ddgi_probe");
                }
            }
        }
}

template <typename StateT, typename SunT, typename FillFn>
inline void draw_frustum_cull_overlay_3d(StateT& s,
                   cd::rhi::ICommandBuffer& cmd,
                   const cd::math::Mat4f& vp,
                   const SunT& sun,
                   FillFn&& fill_prim_push_shared, float aspect)
{
        // phase1006-3d-viewport-frustum-cull-overlay: same draw pattern
        // as the DDGI probe overlay above but the per-cluster tint
        // encodes cd::camera::test_aabb result instead of an index hash.
        if (s.fx.frustum_cull_show_aabbs_3d)
        {
            cd::camera::Camera cull_cam {};
            cull_cam.eye    = s.cam.eye;
            cull_cam.target = s.cam.target;
            cull_cam.up     = s.cam.up;
            cull_cam.fov_y  = s.cam.fov_y;
            cull_cam.near_z = s.cam.near_z;
            cull_cam.far_z  = s.cam.far_z;
            const auto frustum = cd::camera::extract_frustum(cull_cam, aspect);
            const auto& dbg_mesh = s.meshes.sphere;
            if (dbg_mesh.vb.is_valid())
            {
                cmd.bind_vertex_buffer(0, dbg_mesh.vb, 0);
                cmd.bind_index_buffer(dbg_mesh.ib, 0, dbg_mesh.index_type);
                constexpr float kSpacing = 2.0F;
                constexpr float kExtent  = 0.5F;
                for (int gz = -1; gz <= 1; ++gz)
                for (int gy = -1; gy <= 1; ++gy)
                for (int gx = -1; gx <= 1; ++gx)
                {
                    const cd::math::Vec3f centre {
                        static_cast<float>(gx) * kSpacing,
                        static_cast<float>(gy) * kSpacing + 1.0F,
                        static_cast<float>(gz) * kSpacing };
                    const cd::math::Vec3f bmin {
                        centre.x - kExtent, centre.y - kExtent, centre.z - kExtent };
                    const cd::math::Vec3f bmax {
                        centre.x + kExtent, centre.y + kExtent, centre.z + kExtent };
                    const auto r = cd::camera::test_aabb(frustum, bmin, bmax);
                    cd::math::Vec3f tint;
                    switch (r)
                    {
                        case cd::camera::CullResult::kOutside:
                            tint = { 0.95F, 0.18F, 0.18F }; break;  // red
                        case cd::camera::CullResult::kIntersecting:
                            tint = { 0.95F, 0.85F, 0.18F }; break;  // yellow
                        case cd::camera::CullResult::kInside:
                            tint = { 0.20F, 0.95F, 0.30F }; break;  // green
                    }
                    // phase1018-frustum-aabb-corner-spheres (Run 28
                    // item #3): user feedback — a single centre sphere
                    // per cell reads as a 27-point dot cloud, not as
                    // BOXES. Render 8 smaller spheres at the AABB
                    // corners instead; the implied cube silhouette is
                    // unmistakable, and the cull-state tint stays on
                    // every corner. True wireframe edges remain queued
                    // for the cd::debug::LineRenderer lib.
                    constexpr float kCornerR = 0.10F;
                    for (int csz = 0; csz <= 1; ++csz)
                        for (int csy = 0; csy <= 1; ++csy)
                            for (int csx = 0; csx <= 1; ++csx)
                            {
                                const cd::math::Vec3f cpos {
                                    (csx != 0) ? bmax.x : bmin.x,
                                    (csy != 0) ? bmax.y : bmin.y,
                                    (csz != 0) ? bmax.z : bmin.z };
                                PrimPush pp {};
                                cd::math::Mat4f model { cd::math::Mat4f::identity() };
                                model[0][0] = kCornerR;
                                model[1][1] = kCornerR;
                                model[2][2] = kCornerR;
                                model[3][0] = cpos.x;
                                model[3][1] = cpos.y;
                                model[3][2] = cpos.z;
                                pp.model = model;
                                pp.mvp = vp * model;
                                pp.tint[0] = tint.x;
                                pp.tint[1] = tint.y;
                                pp.tint[2] = tint.z;
                                pp.tint[3] = 1.0F;  // standard Lit
                                fill_prim_push_shared(pp, s.fx, sun, s.cam);
                                pp.fx_params[1]  = 0.0F;
                                pp.fx_params4[0] = 0.0F;
                                pp.fx_params4[1] = 0.7F;
                                cmd.push_constants(
                                    s.materials.prim.pipeline_layout(),
                                    cd::rhi::ShaderStage::kVertex | cd::rhi::ShaderStage::kFragment,
                                    0, sizeof(pp), &pp);
                                cmd.draw_indexed(dbg_mesh.index_count, 1, 0, 0, 0);
                                s.counters.increment("draws_frustum_aabb");
                            }
                    // phase1032-line-upgrades: true 12-edge wireframe
                    // via cd::debug_line — the corner spheres carry
                    // the cull tint at distance, the edges make each
                    // cell read as a BOX up close.
                    s.debug_lines.add_aabb(
                        bmin, bmax, { tint.x, tint.y, tint.z, 1.0F });
                }
            }
        }
}

template <typename StateT, typename SunT, typename FillFn>
inline void draw_light_gizmo_overlay_3d(StateT& s,
                   cd::rhi::ICommandBuffer& cmd,
                   const cd::math::Mat4f& vp,
                   const SunT& sun,
                   FillFn&& fill_prim_push_shared)
{
        // phase1007-3d-viewport-light-position-gizmo: render a small
        // sphere at each non-directional light's world position,
        // tinted by the light's authored colour (either explicit RGB
        // or the kelvin → linear sRGB conversion). Directional lights
        // are skipped (no position). Useful for "where exactly is my
        // 2700K lantern in Sponza" without opening the gizmo manipulator.
        if (s.fx.lights_show_gizmos_3d)
        {
            const auto& dbg_mesh = s.meshes.sphere;
            if (dbg_mesh.vb.is_valid())
            {
                cmd.bind_vertex_buffer(0, dbg_mesh.vb, 0);
                cmd.bind_index_buffer(dbg_mesh.ib, 0, dbg_mesh.index_type);
                constexpr float kRadius = 0.18F;
                for (const auto& row : s.lights)
                {
                    if (!row.enabled) continue;
                    if (row.light.type == cd::light::LightType::kDirectional)
                        continue;
                    const auto pos = row.light.position;
                    cd::math::Vec3f tint = row.light.color;
                    if (row.light.color_kelvin > 100.0F)
                        tint = cd::light::cct_to_linear_rgb(row.light.color_kelvin);
                    PrimPush pp {};
                    cd::math::Mat4f model { cd::math::Mat4f::identity() };
                    model[0][0] = kRadius;
                    model[1][1] = kRadius;
                    model[2][2] = kRadius;
                    model[3][0] = pos.x;
                    model[3][1] = pos.y;
                    model[3][2] = pos.z;
                    pp.model = model;
                    pp.mvp = vp * model;
                    pp.tint[0] = tint.x;
                    pp.tint[1] = tint.y;
                    pp.tint[2] = tint.z;
                    pp.tint[3] = 1.0F;
                    fill_prim_push_shared(pp, s.fx, sun, s.cam);
                    pp.fx_params[1]  = 0.0F;
                    pp.fx_params4[0] = 0.0F;
                    pp.fx_params4[1] = 0.5F;
                    cmd.push_constants(
                        s.materials.prim.pipeline_layout(),
                        cd::rhi::ShaderStage::kVertex | cd::rhi::ShaderStage::kFragment,
                        0, sizeof(pp), &pp);
                    cmd.draw_indexed(dbg_mesh.index_count, 1, 0, 0, 0);
                    s.counters.increment("draws_light_gizmo");
                    // phase1009-3d-viewport-area-light-polygon-gizmo:
                    // for kRectArea / kDiskArea lights, also emit 4
                    // small spheres at the polygon corners so the user
                    // can SEE the rectangle / disk extent in the 3D
                    // scene (not just the centre proxy). For rectangles
                    // we use position ± tangent*(width/2) ± bitangent*
                    // (height/2). For disks we approximate with 4
                    // cardinal-direction points on the perimeter (tangent
                    // ±radius, bitangent ±radius). True wireframe outline
                    // is queued for the line renderer (researcher §12
                    // Tier-2). Same tint as the centre sphere.
                    const auto lt = row.light.type;
                    if (lt == cd::light::LightType::kRectArea ||
                        lt == cd::light::LightType::kDiskArea)
                    {
                        constexpr float kCornerRadius = 0.08F;
                        const float hw = (lt == cd::light::LightType::kRectArea)
                            ? (row.light.area_width  * 0.5F)
                            :  row.light.area_width;
                        const float hh = (lt == cd::light::LightType::kRectArea)
                            ? (row.light.area_height * 0.5F)
                            :  row.light.area_height;
                        // phase1032-line-upgrades: rect outline (4
                        // edges through the corners) or disk circle
                        // so the emitting SHAPE reads, not just its
                        // corner proxies.
                        if (lt == cd::light::LightType::kRectArea)
                        {
                            const auto corner = [&](float si, float sj) {
                                return cd::math::Vec3f {
                                    pos.x + si * hw * row.light.area_tangent.x
                                          + sj * hh * row.light.area_bitangent.x,
                                    pos.y + si * hw * row.light.area_tangent.y
                                          + sj * hh * row.light.area_bitangent.y,
                                    pos.z + si * hw * row.light.area_tangent.z
                                          + sj * hh * row.light.area_bitangent.z };
                            };
                            const std::array<cd::math::Vec3f, 5> outline {{
                                corner(-1.0F, -1.0F), corner(1.0F, -1.0F),
                                corner(1.0F, 1.0F),  corner(-1.0F, 1.0F),
                                corner(-1.0F, -1.0F) }};
                            s.debug_lines.add_polyline(
                                outline, { tint.x, tint.y, tint.z, 1.0F });
                        }
                        else
                        {
                            const cd::math::Vec3f disk_normal {
                                row.light.area_tangent.y * row.light.area_bitangent.z -
                                row.light.area_tangent.z * row.light.area_bitangent.y,
                                row.light.area_tangent.z * row.light.area_bitangent.x -
                                row.light.area_tangent.x * row.light.area_bitangent.z,
                                row.light.area_tangent.x * row.light.area_bitangent.y -
                                row.light.area_tangent.y * row.light.area_bitangent.x };
                            s.debug_lines.add_circle(
                                pos, disk_normal, hw, 24,
                                { tint.x, tint.y, tint.z, 1.0F });
                        }
                        for (int sj = -1; sj <= 1; sj += 2)
                        for (int si = -1; si <= 1; si += 2)
                        {
                            const cd::math::Vec3f cp {
                                pos.x + static_cast<float>(si) * hw * row.light.area_tangent.x
                                      + static_cast<float>(sj) * hh * row.light.area_bitangent.x,
                                pos.y + static_cast<float>(si) * hw * row.light.area_tangent.y
                                      + static_cast<float>(sj) * hh * row.light.area_bitangent.y,
                                pos.z + static_cast<float>(si) * hw * row.light.area_tangent.z
                                      + static_cast<float>(sj) * hh * row.light.area_bitangent.z };
                            PrimPush cp_pp {};
                            cd::math::Mat4f cp_model { cd::math::Mat4f::identity() };
                            cp_model[0][0] = kCornerRadius;
                            cp_model[1][1] = kCornerRadius;
                            cp_model[2][2] = kCornerRadius;
                            cp_model[3][0] = cp.x;
                            cp_model[3][1] = cp.y;
                            cp_model[3][2] = cp.z;
                            cp_pp.model = cp_model;
                            cp_pp.mvp = vp * cp_model;
                            cp_pp.tint[0] = tint.x;
                            cp_pp.tint[1] = tint.y;
                            cp_pp.tint[2] = tint.z;
                            cp_pp.tint[3] = 1.0F;
                            fill_prim_push_shared(cp_pp, s.fx, sun, s.cam);
                            cp_pp.fx_params[1]  = 0.0F;
                            cp_pp.fx_params4[0] = 0.0F;
                            cp_pp.fx_params4[1] = 0.5F;
                            cmd.push_constants(
                                s.materials.prim.pipeline_layout(),
                                cd::rhi::ShaderStage::kVertex | cd::rhi::ShaderStage::kFragment,
                                0, sizeof(cp_pp), &cp_pp);
                            cmd.draw_indexed(dbg_mesh.index_count, 1, 0, 0, 0);
                            s.counters.increment("draws_area_light_corner");
                        }
                    }
                }
            }
        }
}

template <typename StateT, typename SunT, typename FillFn>
inline void draw_decal_obb_overlay_3d(StateT& s,
                   cd::rhi::ICommandBuffer& cmd,
                   const cd::math::Mat4f& vp,
                   const SunT& sun,
                   FillFn&& fill_prim_push_shared)
{
        // phase1008-3d-viewport-decal-obb-gizmo: 4th application of the
        // canonical sphere-at-position pattern. Renders 1 sphere at the
        // decal centre (tinted green if the decal OBB intersects the
        // fixed scene AABB [-1,1]^3, red if it misses -- matches what
        // the "Run25 Decal Projector Probe" panel reports textually)
        // plus 8 small white spheres at the OBB corners. Lets the user
        // SEE in the 3D scene where the decal volume sits while
        // dragging the position / half-extents sliders.
        if (s.fx.decal_show_obb_3d)
        {
            const auto& dbg_mesh = s.meshes.sphere;
            if (dbg_mesh.vb.is_valid())
            {
                cmd.bind_vertex_buffer(0, dbg_mesh.vb, 0);
                cmd.bind_index_buffer(dbg_mesh.ib, 0, dbg_mesh.index_type);
                cd::decal::Decal probe {};
                probe.position     = { s.fx.decal_demo_position[0],
                                       s.fx.decal_demo_position[1],
                                       s.fx.decal_demo_position[2] };
                probe.half_extents = { s.fx.decal_demo_half_extents[0],
                                       s.fx.decal_demo_half_extents[1],
                                       s.fx.decal_demo_half_extents[2] };
                constexpr cd::math::Vec3f kAabbMin { -1.0F, -1.0F, -1.0F };
                constexpr cd::math::Vec3f kAabbMax {  1.0F,  1.0F,  1.0F };
                const bool aabb_hit =
                    cd::decal::decal_intersects_aabb(probe, kAabbMin, kAabbMax);
                const cd::math::Vec3f centre_tint = aabb_hit
                    ? cd::math::Vec3f { 0.20F, 0.95F, 0.30F }   // green
                    : cd::math::Vec3f { 0.95F, 0.18F, 0.18F };  // red
                struct DecalGizmoSphere
                {
                    cd::math::Vec3f pos;
                    cd::math::Vec3f tint;
                    float radius { 0.0F };
                };
                std::array<DecalGizmoSphere, 9> gizmo {};
                gizmo[0] = { probe.position, centre_tint, 0.18F };
                std::size_t idx = 1;
                for (int sz = -1; sz <= 1; sz += 2)
                    for (int sy = -1; sy <= 1; sy += 2)
                        for (int sx = -1; sx <= 1; sx += 2)
                        {
                            gizmo[idx].pos = {
                                probe.position.x + static_cast<float>(sx) * probe.half_extents.x,
                                probe.position.y + static_cast<float>(sy) * probe.half_extents.y,
                                probe.position.z + static_cast<float>(sz) * probe.half_extents.z };
                            gizmo[idx].tint   = { 0.95F, 0.95F, 0.95F };
                            gizmo[idx].radius = 0.10F;
                            ++idx;
                        }
                for (const auto& g : gizmo)
                {
                    PrimPush pp {};
                    cd::math::Mat4f model { cd::math::Mat4f::identity() };
                    model[0][0] = g.radius;
                    model[1][1] = g.radius;
                    model[2][2] = g.radius;
                    model[3][0] = g.pos.x;
                    model[3][1] = g.pos.y;
                    model[3][2] = g.pos.z;
                    pp.model = model;
                    pp.mvp = vp * model;
                    pp.tint[0] = g.tint.x;
                    pp.tint[1] = g.tint.y;
                    pp.tint[2] = g.tint.z;
                    pp.tint[3] = 1.0F;
                    fill_prim_push_shared(pp, s.fx, sun, s.cam);
                    pp.fx_params[1]  = 0.0F;
                    pp.fx_params4[0] = 0.0F;
                    pp.fx_params4[1] = 0.55F;
                    cmd.push_constants(
                        s.materials.prim.pipeline_layout(),
                        cd::rhi::ShaderStage::kVertex | cd::rhi::ShaderStage::kFragment,
                        0, sizeof(pp), &pp);
                    cmd.draw_indexed(dbg_mesh.index_count, 1, 0, 0, 0);
                    s.counters.increment("draws_decal_gizmo");
                }
                // phase1031-debug-line-gpu (clarity fix 5/5): the
                // corner spheres imply the OBB but the EDGES make it
                // unambiguous. cd::debug_line::LineBatch emits the 12
                // edges; the batch is flushed once at the end of the
                // HDR pass through the new kLineList material.
                s.debug_lines.add_obb(
                    probe.position, probe.right, probe.up,
                    probe.forward, probe.half_extents,
                    { centre_tint.x, centre_tint.y, centre_tint.z, 1.0F });
            }
        }
}

template <typename StateT, typename SunT, typename FillFn>
inline void draw_csm_cascade_overlay_3d(StateT& s,
                   cd::rhi::ICommandBuffer& cmd,
                   const cd::math::Mat4f& vp,
                   const SunT& sun,
                   FillFn&& fill_prim_push_shared)
{
        // phase1010-3d-viewport-csm-cascade-depth: 5th application of
        // the canonical sphere-at-position pattern. Renders 1 sphere
        // along the camera view-direction at the centre depth of each
        // of the 4 CSM cascades computed via Zhang 2006 Practical
        // Split Scheme with lambda=0.75 (Doom Eternal default).
        // Tinted by cascade index (red=near, yellow, green, blue=far)
        // so the user can SEE how the cascade boundaries distribute
        // along view depth. The split distance bars in the
        // "Run25 CSM Split Distances Probe" panel give the numbers;
        // this overlay gives the visual along the actual view ray.
        if (s.fx.csm_show_cascade_depth_3d)
        {
            const auto& dbg_mesh = s.meshes.sphere;
            if (dbg_mesh.vb.is_valid())
            {
                cmd.bind_vertex_buffer(0, dbg_mesh.vb, 0);
                cmd.bind_index_buffer(dbg_mesh.ib, 0, dbg_mesh.index_type);
                constexpr std::uint32_t kCascades = 4;
                const auto splits = cd::light::practical_split_distances(
                    s.cam.near_z, s.cam.far_z, kCascades, 0.75F);
                const cd::math::Vec3f view_vec {
                    s.cam.target.x - s.cam.eye.x,
                    s.cam.target.y - s.cam.eye.y,
                    s.cam.target.z - s.cam.eye.z };
                const float view_len = std::sqrt(
                    view_vec.x * view_vec.x +
                    view_vec.y * view_vec.y +
                    view_vec.z * view_vec.z);
                if (view_len > 1e-4F)
                {
                    const cd::math::Vec3f view_dir {
                        view_vec.x / view_len,
                        view_vec.y / view_len,
                        view_vec.z / view_len };
                    constexpr std::array<cd::math::Vec3f, 4> kCascadeTints {{
                        { 0.95F, 0.18F, 0.18F },  // red
                        { 0.95F, 0.85F, 0.18F },  // yellow
                        { 0.20F, 0.95F, 0.30F },  // green
                        { 0.30F, 0.45F, 0.95F } }};// blue
                    for (std::uint32_t ci = 0; ci < kCascades; ++ci)
                    {
                        const float n = splits[ci];
                        const float f = splits[ci + 1];
                        const float mid = 0.5F * (n + f);
                        // phase1018-csm-radius-encodes-slice-depth
                        // (Run 28 item #3): user feedback — 4 equal
                        // spheres show the cascade POSITIONS but not
                        // that each successive cascade covers a much
                        // LARGER depth range (Practical Split is
                        // log-weighted). Scale the sphere radius by
                        // the slice extent (normalised to far_z), so
                        // cascade 0 is a small marble and cascade 3 a
                        // large ball — radius now encodes shadow-map
                        // texel stretch per cascade.
                        const float slice_norm =
                            (f - n) / std::max(s.cam.far_z, 1.0F);
                        const float radius =
                            0.15F + 0.85F * std::sqrt(slice_norm);
                        const cd::math::Vec3f pos {
                            s.cam.eye.x + view_dir.x * mid,
                            s.cam.eye.y + view_dir.y * mid,
                            s.cam.eye.z + view_dir.z * mid };
                        const auto& tint = kCascadeTints[ci];
                        PrimPush pp {};
                        cd::math::Mat4f model { cd::math::Mat4f::identity() };
                        model[0][0] = radius;
                        model[1][1] = radius;
                        model[2][2] = radius;
                        model[3][0] = pos.x;
                        model[3][1] = pos.y;
                        model[3][2] = pos.z;
                        pp.model = model;
                        pp.mvp = vp * model;
                        pp.tint[0] = tint.x;
                        pp.tint[1] = tint.y;
                        pp.tint[2] = tint.z;
                        pp.tint[3] = 1.0F;
                        fill_prim_push_shared(pp, s.fx, sun, s.cam);
                        pp.fx_params[1]  = 0.0F;
                        pp.fx_params4[0] = 0.0F;
                        pp.fx_params4[1] = 0.5F;
                        cmd.push_constants(
                            s.materials.prim.pipeline_layout(),
                            cd::rhi::ShaderStage::kVertex | cd::rhi::ShaderStage::kFragment,
                            0, sizeof(pp), &pp);
                        cmd.draw_indexed(dbg_mesh.index_count, 1, 0, 0, 0);
                        s.counters.increment("draws_csm_cascade");
                        // phase1032-line-upgrades: boundary ring at
                        // the cascade FAR distance, perpendicular to
                        // the view ray — the ring marks where this
                        // cascade hands off to the next one, which
                        // the centre-depth sphere alone cannot show.
                        s.debug_lines.add_circle(
                            { s.cam.eye.x + view_dir.x * f,
                              s.cam.eye.y + view_dir.y * f,
                              s.cam.eye.z + view_dir.z * f },
                            view_dir, radius * 2.5F, 32,
                            { tint.x, tint.y, tint.z, 1.0F });
                    }
                }
            }
        }
}

template <typename StateT, typename SunT, typename FillFn>
inline void draw_cluster_heatmap_overlay_3d(StateT& s,
                   cd::rhi::ICommandBuffer& cmd,
                   const cd::math::Mat4f& vp,
                   const SunT& sun,
                   FillFn&& fill_prim_push_shared)
{
        // phase1011-3d-viewport-cluster-density-heatmap: 6th
        // application of the canonical sphere-at-position template.
        // 8x4x8 world-space grid centred at origin with 2 m spacing;
        // per-cell sphere tint encodes the number of scene lights
        // whose (position, range) sphere covers that cell. Mirrors
        // the production cluster-shading heat-map (Olsson 2012 +
        // DOOM 2016 talks) but in world space + with scene lights
        // (s.lights) instead of the viewport-aligned GPU grid.
        if (s.fx.cluster_show_density_3d)
        {
            const auto& dbg_mesh = s.meshes.sphere;
            if (dbg_mesh.vb.is_valid())
            {
                cmd.bind_vertex_buffer(0, dbg_mesh.vb, 0);
                cmd.bind_index_buffer(dbg_mesh.ib, 0, dbg_mesh.index_type);
                constexpr int kCellsX = 8;
                constexpr int kCellsY = 4;
                constexpr int kCellsZ = 8;
                constexpr float kSpacing = 2.0F;
                constexpr float kRadius  = 0.12F;
                constexpr cd::math::Vec3f kOrigin {
                    -(static_cast<float>(kCellsX - 1) * 0.5F) * kSpacing,
                    0.5F,
                    -(static_cast<float>(kCellsZ - 1) * 0.5F) * kSpacing };
                for (int cz = 0; cz < kCellsZ; ++cz)
                for (int cy = 0; cy < kCellsY; ++cy)
                for (int cx = 0; cx < kCellsX; ++cx)
                {
                    const cd::math::Vec3f centre {
                        kOrigin.x + static_cast<float>(cx) * kSpacing,
                        kOrigin.y + static_cast<float>(cy) * kSpacing,
                        kOrigin.z + static_cast<float>(cz) * kSpacing };
                    std::uint32_t hits = 0;
                    for (const auto& row : s.lights)
                    {
                        if (!row.enabled) continue;
                        if (row.light.type == cd::light::LightType::kDirectional)
                            continue;
                        const float dx = centre.x - row.light.position.x;
                        const float dy = centre.y - row.light.position.y;
                        const float dz = centre.z - row.light.position.z;
                        const float d2 = dx*dx + dy*dy + dz*dz;
                        const float r  = row.light.range;
                        if (d2 <= r * r) ++hits;
                    }
                    // phase1017-cluster-hide-empty (Run 28 item #3):
                    // user feedback — 200+ dim grey "0 lights" spheres
                    // drowned the handful of hot cells. Empty cells are
                    // now SKIPPED entirely; what remains is exactly the
                    // light-coverage hot-spot map (green=1, yellow=2,
                    // red=3+), matching how DOOM Eternal's cluster debug
                    // view only paints occupied clusters.
                    if (hits == 0) continue;
                    cd::math::Vec3f tint;
                    if      (hits == 1) tint = { 0.20F, 0.95F, 0.30F };  // green
                    else if (hits == 2) tint = { 0.95F, 0.85F, 0.18F };  // yellow
                    else                tint = { 0.95F, 0.20F, 0.18F };  // red
                    PrimPush pp {};
                    cd::math::Mat4f model { cd::math::Mat4f::identity() };
                    model[0][0] = kRadius;
                    model[1][1] = kRadius;
                    model[2][2] = kRadius;
                    model[3][0] = centre.x;
                    model[3][1] = centre.y;
                    model[3][2] = centre.z;
                    pp.model = model;
                    pp.mvp = vp * model;
                    pp.tint[0] = tint.x;
                    pp.tint[1] = tint.y;
                    pp.tint[2] = tint.z;
                    pp.tint[3] = 1.0F;
                    fill_prim_push_shared(pp, s.fx, sun, s.cam);
                    pp.fx_params[1]  = 0.0F;
                    pp.fx_params4[0] = 0.0F;
                    pp.fx_params4[1] = 0.6F;
                    cmd.push_constants(
                        s.materials.prim.pipeline_layout(),
                        cd::rhi::ShaderStage::kVertex | cd::rhi::ShaderStage::kFragment,
                        0, sizeof(pp), &pp);
                    cmd.draw_indexed(dbg_mesh.index_count, 1, 0, 0, 0);
                    s.counters.increment("draws_cluster_cell");
                }
            }
        }
}

template <typename StateT, typename SunT, typename FillFn>
inline void draw_bezier_curve_overlay_3d(StateT& s,
                   cd::rhi::ICommandBuffer& cmd,
                   const cd::math::Mat4f& vp,
                   const SunT& sun,
                   FillFn&& fill_prim_push_shared)
{
        // phase1012-3d-viewport-cubic-bezier-curve: 7th application
        // of the canonical sphere-at-position template. Renders 4
        // larger white spheres at the bezier control points + 32
        // smaller magenta spheres sampled along t in [0, 1] via
        // cd::math::CubicBezier::at. Lets the user SEE the curve
        // shape in the 3D scene while dragging the panel sliders.
        if (s.fx.bezier_show_curve_3d)
        {
            const auto& dbg_mesh = s.meshes.sphere;
            if (dbg_mesh.vb.is_valid())
            {
                cmd.bind_vertex_buffer(0, dbg_mesh.vb, 0);
                cmd.bind_index_buffer(dbg_mesh.ib, 0, dbg_mesh.index_type);
                cd::math::CubicBezier cb {};
                cb.p0 = { s.fx.bezier_p0[0], s.fx.bezier_p0[1], s.fx.bezier_p0[2] };
                cb.p1 = { s.fx.bezier_p1[0], s.fx.bezier_p1[1], s.fx.bezier_p1[2] };
                cb.p2 = { s.fx.bezier_p2[0], s.fx.bezier_p2[1], s.fx.bezier_p2[2] };
                cb.p3 = { s.fx.bezier_p3[0], s.fx.bezier_p3[1], s.fx.bezier_p3[2] };
                constexpr int kSamples = 32;
                constexpr float kCpRadius     = 0.18F;
                constexpr float kSampleRadius = 0.07F;
                const std::array<cd::math::Vec3f, 4> cps {{
                    cb.p0, cb.p1, cb.p2, cb.p3 }};
                for (const auto& cp : cps)
                {
                    PrimPush pp {};
                    cd::math::Mat4f model { cd::math::Mat4f::identity() };
                    model[0][0] = kCpRadius;
                    model[1][1] = kCpRadius;
                    model[2][2] = kCpRadius;
                    model[3][0] = cp.x;
                    model[3][1] = cp.y;
                    model[3][2] = cp.z;
                    pp.model = model;
                    pp.mvp = vp * model;
                    pp.tint[0] = 0.95F;
                    pp.tint[1] = 0.95F;
                    pp.tint[2] = 0.95F;
                    pp.tint[3] = 1.0F;
                    fill_prim_push_shared(pp, s.fx, sun, s.cam);
                    pp.fx_params[1]  = 0.0F;
                    pp.fx_params4[0] = 0.0F;
                    pp.fx_params4[1] = 0.45F;
                    cmd.push_constants(
                        s.materials.prim.pipeline_layout(),
                        cd::rhi::ShaderStage::kVertex | cd::rhi::ShaderStage::kFragment,
                        0, sizeof(pp), &pp);
                    cmd.draw_indexed(dbg_mesh.index_count, 1, 0, 0, 0);
                    s.counters.increment("draws_bezier_cp");
                }
                std::array<cd::math::Vec3f, kSamples> curve_pts {};
                for (int i = 0; i < kSamples; ++i)
                {
                    const float t = static_cast<float>(i) /
                                    static_cast<float>(kSamples - 1);
                    const auto p = cb.at(t);
                    curve_pts[static_cast<std::size_t>(i)] = p;
                    PrimPush pp {};
                    cd::math::Mat4f model { cd::math::Mat4f::identity() };
                    model[0][0] = kSampleRadius;
                    model[1][1] = kSampleRadius;
                    model[2][2] = kSampleRadius;
                    model[3][0] = p.x;
                    model[3][1] = p.y;
                    model[3][2] = p.z;
                    pp.model = model;
                    pp.mvp = vp * model;
                    pp.tint[0] = 0.95F;
                    pp.tint[1] = 0.18F;
                    pp.tint[2] = 0.78F;
                    pp.tint[3] = 1.0F;
                    fill_prim_push_shared(pp, s.fx, sun, s.cam);
                    pp.fx_params[1]  = 0.0F;
                    pp.fx_params4[0] = 0.0F;
                    pp.fx_params4[1] = 0.5F;
                    cmd.push_constants(
                        s.materials.prim.pipeline_layout(),
                        cd::rhi::ShaderStage::kVertex | cd::rhi::ShaderStage::kFragment,
                        0, sizeof(pp), &pp);
                    cmd.draw_indexed(dbg_mesh.index_count, 1, 0, 0, 0);
                    s.counters.increment("draws_bezier_sample");
                }
                // phase1032-line-upgrades: continuous curve polyline
                // (magenta) + the classic control polygon (dim grey)
                // P0-P1-P2-P3 so the hull-vs-curve relationship reads.
                s.debug_lines.add_polyline(
                    curve_pts, { 0.95F, 0.18F, 0.78F, 1.0F });
                const std::array<cd::math::Vec3f, 4> hull {{
                    cb.p0, cb.p1, cb.p2, cb.p3 }};
                s.debug_lines.add_polyline(
                    hull, { 0.55F, 0.55F, 0.58F, 1.0F });
            }
        }
}

template <typename StateT, typename SunT, typename FillFn>
inline void draw_gpu_particles_overlay_3d(StateT& s,
                   cd::rhi::ICommandBuffer& cmd,
                   const cd::math::Mat4f& vp,
                   const SunT& sun,
                   FillFn&& fill_prim_push_shared)
{
        // phase1013-3d-viewport-gpu-particles: 8th application of
        // the canonical sphere-at-position template. Maintains a
        // render-loop-local 64-particle pool driven every frame by
        // cd::gpu_particles::advance + compact_alive. Each alive
        // particle becomes one tiny sphere at its position, tinted
        // by remaining life (warm-yellow at life=1, deep-red as
        // it fades). When all particles die, the pool auto-
        // respawns so the animation never stops. Independent from
        // the "Step 1 dt" probe (which exercises a one-shot
        // discrete sim).
        if (s.fx.gpu_particles_show_3d)
        {
            constexpr std::size_t kPoolSize = 64;
            static std::array<cd::gpu_particles::Particle, kPoolSize> sp_pool {};
            static bool sp_init = false;
            const auto sp_seed = [&]() {
                for (std::size_t i = 0; i < kPoolSize; ++i)
                {
                    sp_pool[i].life = 1.0F;
                    sp_pool[i].max_life = 1.0F;
                    const auto fi = static_cast<float>(i);
                    sp_pool[i].position = { 0.0F, 1.0F, 0.0F };
                    sp_pool[i].velocity = {
                        std::sin(fi * 0.27F) * 2.5F,
                        2.5F + std::cos(fi * 0.13F) * 1.0F,
                        std::cos(fi * 0.31F) * 2.5F };
                }
            };
            if (!sp_init) { sp_seed(); sp_init = true; }
            constexpr cd::math::Vec3f kGravity { 0.0F, -6.0F, 0.0F };
            constexpr float kDt = 1.0F / 60.0F;
            cd::gpu_particles::advance(
                std::span<cd::gpu_particles::Particle>(sp_pool),
                kDt, kGravity);
            const auto alive = cd::gpu_particles::compact_alive(
                std::span<cd::gpu_particles::Particle>(sp_pool));
            if (alive == 0U) { sp_seed(); }
            const auto& dbg_mesh = s.meshes.sphere;
            if (dbg_mesh.vb.is_valid())
            {
                cmd.bind_vertex_buffer(0, dbg_mesh.vb, 0);
                cmd.bind_index_buffer(dbg_mesh.ib, 0, dbg_mesh.index_type);
                constexpr float kRadius = 0.06F;
                for (std::uint32_t i = 0; i < alive; ++i)
                {
                    const auto& pt = sp_pool[i];
                    const float life01 = (pt.max_life > 1e-4F)
                        ? std::clamp(pt.life / pt.max_life, 0.0F, 1.0F)
                        : 0.0F;
                    PrimPush pp {};
                    cd::math::Mat4f model { cd::math::Mat4f::identity() };
                    model[0][0] = kRadius;
                    model[1][1] = kRadius;
                    model[2][2] = kRadius;
                    model[3][0] = pt.position.x;
                    model[3][1] = pt.position.y;
                    model[3][2] = pt.position.z;
                    pp.model = model;
                    pp.mvp = vp * model;
                    pp.tint[0] = 0.95F * life01 + 0.95F * (1.0F - life01);
                    pp.tint[1] = 0.75F * life01 + 0.15F * (1.0F - life01);
                    pp.tint[2] = 0.18F * life01 + 0.05F * (1.0F - life01);
                    pp.tint[3] = 1.0F;
                    fill_prim_push_shared(pp, s.fx, sun, s.cam);
                    pp.fx_params[1]  = 0.0F;
                    pp.fx_params4[0] = 0.0F;
                    pp.fx_params4[1] = 0.5F;
                    cmd.push_constants(
                        s.materials.prim.pipeline_layout(),
                        cd::rhi::ShaderStage::kVertex | cd::rhi::ShaderStage::kFragment,
                        0, sizeof(pp), &pp);
                    cmd.draw_indexed(dbg_mesh.index_count, 1, 0, 0, 0);
                    s.counters.increment("draws_gpu_particle");
                }
            }
        }
}

template <typename StateT, typename SunT, typename FillFn>
inline void draw_quat_slerp_overlay_3d(StateT& s,
                   cd::rhi::ICommandBuffer& cmd,
                   const cd::math::Mat4f& vp,
                   const SunT& sun,
                   FillFn&& fill_prim_push_shared)
{
        // phase1019-3d-viewport-quat-slerp-triad: 9th application of
        // the canonical sphere-at-position template. Renders, anchored
        // at a fixed point above the origin:
        //   - 1 white sphere at the anchor (rotation centre);
        //   - 3 spheres at the slerp(A,B,t)-rotated X/Y/Z axis tips,
        //     tinted red/green/blue (standard axis colours) — the
        //     live orientation TRIAD;
        //   - 16 small spheres tracing the X-axis tip along
        //     slerp(A,B,ti) for ti in [0,1] — the great-circle arc,
        //     fading dark->bright with t so direction A->B reads.
        if (s.fx.quat_slerp_show_3d)
        {
            const auto& dbg_mesh = s.meshes.sphere;
            if (dbg_mesh.vb.is_valid())
            {
                cmd.bind_vertex_buffer(0, dbg_mesh.vb, 0);
                cmd.bind_index_buffer(dbg_mesh.ib, 0, dbg_mesh.index_type);
                constexpr cd::math::Vec3f kAnchor { 0.0F, 2.5F, 0.0F };
                constexpr float kAxisLen = 1.2F;
                const cd::math::Quatf qa {
                    s.fx.quat_slerp_a[0], s.fx.quat_slerp_a[1],
                    s.fx.quat_slerp_a[2], s.fx.quat_slerp_a[3] };
                const cd::math::Quatf qb {
                    s.fx.quat_slerp_b[0], s.fx.quat_slerp_b[1],
                    s.fx.quat_slerp_b[2], s.fx.quat_slerp_b[3] };
                struct TriadSphere
                {
                    cd::math::Vec3f pos;
                    cd::math::Vec3f tint;
                    float radius;
                };
                std::vector<TriadSphere> spheres;
                spheres.reserve(20);
                spheres.push_back({ kAnchor, { 0.95F, 0.95F, 0.95F }, 0.10F });
                const auto qt = cd::math::slerp(qa, qb, s.fx.quat_slerp_t);
                constexpr std::array<cd::math::Vec3f, 3> kAxes {{
                    { 1.0F, 0.0F, 0.0F },
                    { 0.0F, 1.0F, 0.0F },
                    { 0.0F, 0.0F, 1.0F } }};
                constexpr std::array<cd::math::Vec3f, 3> kAxisTints {{
                    { 0.95F, 0.18F, 0.18F },   // X = red
                    { 0.20F, 0.95F, 0.30F },   // Y = green
                    { 0.30F, 0.45F, 0.95F } }};// Z = blue
                for (std::size_t ai = 0; ai < 3; ++ai)
                {
                    const auto tip = cd::math::rotate(qt, kAxes[ai]);
                    spheres.push_back({
                        { kAnchor.x + tip.x * kAxisLen,
                          kAnchor.y + tip.y * kAxisLen,
                          kAnchor.z + tip.z * kAxisLen },
                        kAxisTints[ai], 0.12F });
                }
                constexpr int kArcSamples = 16;
                for (int si = 0; si < kArcSamples; ++si)
                {
                    const float ti = static_cast<float>(si) /
                                     static_cast<float>(kArcSamples - 1);
                    const auto qi = cd::math::slerp(qa, qb, ti);
                    const auto tip = cd::math::rotate(qi, kAxes[0]);
                    const float fade = 0.35F + 0.60F * ti;
                    spheres.push_back({
                        { kAnchor.x + tip.x * kAxisLen,
                          kAnchor.y + tip.y * kAxisLen,
                          kAnchor.z + tip.z * kAxisLen },
                        { fade, fade * 0.55F, fade * 0.85F }, 0.045F });
                }
                for (const auto& sp : spheres)
                {
                    PrimPush pp {};
                    cd::math::Mat4f model { cd::math::Mat4f::identity() };
                    model[0][0] = sp.radius;
                    model[1][1] = sp.radius;
                    model[2][2] = sp.radius;
                    model[3][0] = sp.pos.x;
                    model[3][1] = sp.pos.y;
                    model[3][2] = sp.pos.z;
                    pp.model = model;
                    pp.mvp = vp * model;
                    pp.tint[0] = sp.tint.x;
                    pp.tint[1] = sp.tint.y;
                    pp.tint[2] = sp.tint.z;
                    pp.tint[3] = 1.0F;
                    fill_prim_push_shared(pp, s.fx, sun, s.cam);
                    pp.fx_params[1]  = 0.0F;
                    pp.fx_params4[0] = 0.0F;
                    pp.fx_params4[1] = 0.5F;
                    cmd.push_constants(
                        s.materials.prim.pipeline_layout(),
                        cd::rhi::ShaderStage::kVertex | cd::rhi::ShaderStage::kFragment,
                        0, sizeof(pp), &pp);
                    cmd.draw_indexed(dbg_mesh.index_count, 1, 0, 0, 0);
                    s.counters.increment("draws_quat_triad");
                }
            }
        }
}

template <typename StateT, typename SunT, typename FillFn>
inline void draw_motion_vector_overlay_3d(StateT& s,
                   cd::rhi::ICommandBuffer& cmd,
                   const cd::math::Mat4f& vp,
                   const SunT& sun,
                   FillFn&& fill_prim_push_shared)
{
        // phase1021-3d-viewport-motion-vector: 10th application of the
        // canonical sphere-at-position template. Maps the clip-space
        // (x, y) plane onto a fixed 2x2 m world panel above the origin
        // (clip x -> world X, clip y -> world Y, panel centre at
        // (0, 2.5, -3)) and renders:
        //   - red sphere   = prev-frame clip position;
        //   - green sphere = curr-frame clip position;
        //   - 8 small fading spheres between them = the motion path.
        // The user drags the panel sliders and SEES the TAA/blur
        // motion vector as a physical arrow instead of 2 numbers.
        if (s.fx.mvec_show_3d)
        {
            const auto& dbg_mesh = s.meshes.sphere;
            if (dbg_mesh.vb.is_valid())
            {
                cmd.bind_vertex_buffer(0, dbg_mesh.vb, 0);
                cmd.bind_index_buffer(dbg_mesh.ib, 0, dbg_mesh.index_type);
                constexpr cd::math::Vec3f kPanelCentre { 0.0F, 2.5F, -3.0F };
                constexpr float kPanelHalf = 1.0F;  // clip [-1,1] -> ±1 m
                const auto clip_to_world = [&](float cx, float cy) {
                    return cd::math::Vec3f {
                        kPanelCentre.x + cx * kPanelHalf,
                        kPanelCentre.y + cy * kPanelHalf,
                        kPanelCentre.z };
                };
                struct MvecSphere
                {
                    cd::math::Vec3f pos;
                    cd::math::Vec3f tint;
                    float radius;
                };
                std::vector<MvecSphere> spheres;
                spheres.reserve(10);
                spheres.push_back({
                    clip_to_world(s.fx.mvec_prev[0], s.fx.mvec_prev[1]),
                    { 0.95F, 0.18F, 0.18F }, 0.10F });   // prev = red
                spheres.push_back({
                    clip_to_world(s.fx.mvec_curr[0], s.fx.mvec_curr[1]),
                    { 0.20F, 0.95F, 0.30F }, 0.10F });   // curr = green
                constexpr int kPathSamples = 8;
                for (int pi = 1; pi <= kPathSamples; ++pi)
                {
                    const float t = static_cast<float>(pi) /
                                    static_cast<float>(kPathSamples + 1);
                    const float cx = s.fx.mvec_prev[0] +
                        (s.fx.mvec_curr[0] - s.fx.mvec_prev[0]) * t;
                    const float cy = s.fx.mvec_prev[1] +
                        (s.fx.mvec_curr[1] - s.fx.mvec_prev[1]) * t;
                    const float fade = 0.35F + 0.55F * t;
                    spheres.push_back({
                        clip_to_world(cx, cy),
                        { fade, fade, fade * 0.4F }, 0.04F });
                }
                // phase1043-arrow-upgrade: solid prev->curr ARROW via
                // cd::debug_line — the motion vector finally reads as
                // a vector (direction + magnitude), not a dot trail.
                s.debug_lines.add_arrow(
                    clip_to_world(s.fx.mvec_prev[0], s.fx.mvec_prev[1]),
                    clip_to_world(s.fx.mvec_curr[0], s.fx.mvec_curr[1]),
                    { 0.95F, 0.85F, 0.25F, 1.0F });
                for (const auto& sp : spheres)
                {
                    PrimPush pp {};
                    cd::math::Mat4f model { cd::math::Mat4f::identity() };
                    model[0][0] = sp.radius;
                    model[1][1] = sp.radius;
                    model[2][2] = sp.radius;
                    model[3][0] = sp.pos.x;
                    model[3][1] = sp.pos.y;
                    model[3][2] = sp.pos.z;
                    pp.model = model;
                    pp.mvp = vp * model;
                    pp.tint[0] = sp.tint.x;
                    pp.tint[1] = sp.tint.y;
                    pp.tint[2] = sp.tint.z;
                    pp.tint[3] = 1.0F;
                    fill_prim_push_shared(pp, s.fx, sun, s.cam);
                    pp.fx_params[1]  = 0.0F;
                    pp.fx_params4[0] = 0.0F;
                    pp.fx_params4[1] = 0.5F;
                    cmd.push_constants(
                        s.materials.prim.pipeline_layout(),
                        cd::rhi::ShaderStage::kVertex | cd::rhi::ShaderStage::kFragment,
                        0, sizeof(pp), &pp);
                    cmd.draw_indexed(dbg_mesh.index_count, 1, 0, 0, 0);
                    s.counters.increment("draws_mvec");
                }
            }
        }
}

template <typename StateT, typename SunT, typename FillFn>
inline void draw_cubemap_globe_overlay_3d(StateT& s,
                   cd::rhi::ICommandBuffer& cmd,
                   const cd::math::Mat4f& vp,
                   const SunT& sun,
                   FillFn&& fill_prim_push_shared)
{
        // phase1022-3d-viewport-cubemap-globe: 11th application of the
        // canonical sphere-at-position template. Bakes (once) the same
        // tiny analytical sky cube the IBL probe panel uses, then
        // renders a globe of small spheres around a fixed anchor —
        // each tinted by sample_cubemap_dir() along its own outward
        // direction. The full cubemap content becomes a readable 3D
        // ball (blue cap = zenith, warm ring = horizon). One larger
        // sphere rides the globe at the user's sample direction.
        if (s.fx.cubemap_show_globe_3d)
        {
            static const cd::ibl::CubeMapRgbF s_globe_cm = []() {
                return cd::ibl::bake_sky_cube(16, [](cd::math::Vec3f d) -> cd::math::Vec3f {
                    const float ln = 1.0F / std::max(std::sqrt(
                        d.x * d.x + d.y * d.y + d.z * d.z), 1e-3F);
                    const cd::math::Vec3f n { d.x * ln, d.y * ln, d.z * ln };
                    const float t = std::clamp(n.y * 0.5F + 0.5F, 0.0F, 1.0F);
                    const cd::math::Vec3f horizon { 0.95F, 0.65F, 0.40F };
                    const cd::math::Vec3f zenith  { 0.30F, 0.55F, 0.95F };
                    return { horizon.x + (zenith.x - horizon.x) * t,
                             horizon.y + (zenith.y - horizon.y) * t,
                             horizon.z + (zenith.z - horizon.z) * t };
                });
            }();
            const auto& dbg_mesh = s.meshes.sphere;
            if (dbg_mesh.vb.is_valid())
            {
                cmd.bind_vertex_buffer(0, dbg_mesh.vb, 0);
                cmd.bind_index_buffer(dbg_mesh.ib, 0, dbg_mesh.index_type);
                constexpr cd::math::Vec3f kAnchor { 0.0F, 2.5F, 3.0F };
                constexpr float kGlobeR = 1.2F;
                const auto draw_globe_sphere =
                    [&](const cd::math::Vec3f& dir, float radius,
                        bool highlight) {
                    const float dl = std::max(std::sqrt(
                        dir.x * dir.x + dir.y * dir.y + dir.z * dir.z), 1e-3F);
                    const cd::math::Vec3f nd { dir.x / dl, dir.y / dl, dir.z / dl };
                    const auto col = cd::ibl::sample_cubemap_dir(s_globe_cm, nd);
                    PrimPush pp {};
                    cd::math::Mat4f model { cd::math::Mat4f::identity() };
                    model[0][0] = radius;
                    model[1][1] = radius;
                    model[2][2] = radius;
                    model[3][0] = kAnchor.x + nd.x * kGlobeR;
                    model[3][1] = kAnchor.y + nd.y * kGlobeR;
                    model[3][2] = kAnchor.z + nd.z * kGlobeR;
                    pp.model = model;
                    pp.mvp = vp * model;
                    // Highlight sphere keeps the sampled colour but
                    // boosted so it pops against the globe shell.
                    const float boost = highlight ? 1.35F : 1.0F;
                    pp.tint[0] = std::min(col.x * boost, 1.0F);
                    pp.tint[1] = std::min(col.y * boost, 1.0F);
                    pp.tint[2] = std::min(col.z * boost, 1.0F);
                    pp.tint[3] = 1.0F;
                    fill_prim_push_shared(pp, s.fx, sun, s.cam);
                    pp.fx_params[1]  = 0.0F;
                    pp.fx_params4[0] = 0.0F;
                    pp.fx_params4[1] = 0.6F;
                    cmd.push_constants(
                        s.materials.prim.pipeline_layout(),
                        cd::rhi::ShaderStage::kVertex | cd::rhi::ShaderStage::kFragment,
                        0, sizeof(pp), &pp);
                    cmd.draw_indexed(dbg_mesh.index_count, 1, 0, 0, 0);
                    s.counters.increment("draws_cubemap_globe");
                };
                // 6 elevation rings x 8 azimuth + 2 poles = 50 shell
                // spheres covering the full sphere of directions.
                constexpr int kRings = 6;
                constexpr int kAzims = 8;
                constexpr float kPi = std::numbers::pi_v<float>;
                for (int ri = 1; ri <= kRings; ++ri)
                {
                    const float el = kPi * static_cast<float>(ri) /
                                     static_cast<float>(kRings + 1) - kPi * 0.5F;
                    const float ce = std::cos(el);
                    for (int ai = 0; ai < kAzims; ++ai)
                    {
                        const float az = 2.0F * kPi * static_cast<float>(ai) /
                                         static_cast<float>(kAzims);
                        draw_globe_sphere(
                            { ce * std::cos(az), std::sin(el), ce * std::sin(az) },
                            0.07F, false);
                    }
                }
                draw_globe_sphere({ 0.0F,  1.0F, 0.0F }, 0.07F, false);
                draw_globe_sphere({ 0.0F, -1.0F, 0.0F }, 0.07F, false);
                draw_globe_sphere(
                    { s.fx.cubemap_sample_dir[0],
                      s.fx.cubemap_sample_dir[1],
                      s.fx.cubemap_sample_dir[2] },
                    0.16F, true);
            }
        }
}

template <typename StateT, typename SunT, typename FillFn>
inline void draw_vg_lod_overlay_3d(StateT& s,
                   cd::rhi::ICommandBuffer& cmd,
                   const cd::math::Mat4f& vp,
                   const SunT& sun,
                   FillFn&& fill_prim_push_shared)
{
        // phase1023-3d-viewport-vg-lod-frontier: 12th application of
        // the canonical sphere-at-position template. Rebuilds the
        // probe panel's synthetic 4-node DAG, runs pick_clusters with
        // the SAME fx slider values, and renders each node's bounds
        // sphere at a fixed anchor: bright green = in the picked LOD
        // frontier; dim grey = refined away (parent too coarse or
        // children picked instead). A white marker sphere shows the
        // virtual camera distance along -Z (clamped for visibility).
        if (s.fx.vg_show_lod_3d)
        {
            const auto& dbg_mesh = s.meshes.sphere;
            if (dbg_mesh.vb.is_valid())
            {
                cmd.bind_vertex_buffer(0, dbg_mesh.vb, 0);
                cmd.bind_index_buffer(dbg_mesh.ib, 0, dbg_mesh.index_type);
                std::array<cd::virtual_geometry::ClusterNode, 4> dag {};
                dag[0].bounds_sphere = { 0.0F, 0.0F, 0.0F, 1.0F };
                dag[0].self_error    = 0.5F;
                dag[0].parent_error  = 2.0F;
                dag[1].bounds_sphere = { 0.4F, 0.0F, 0.0F, 0.4F };
                dag[1].self_error    = 0.2F;
                dag[1].parent_error  = 0.5F;
                dag[2].bounds_sphere = { 0.0F, 0.4F, 0.0F, 0.4F };
                dag[2].self_error    = 0.2F;
                dag[2].parent_error  = 0.5F;
                dag[3].bounds_sphere = { 0.0F, 0.0F, 0.4F, 0.4F };
                dag[3].self_error    = 0.1F;
                dag[3].parent_error  = 0.2F;
                const cd::math::Vec3f vg_eye { 0.0F, 0.0F, s.fx.vg_lod_cam_z };
                constexpr float kHalfFov = 0.5236F;  // 60 deg horizontal
                const auto picks = cd::virtual_geometry::pick_clusters(
                    std::span<const cd::virtual_geometry::ClusterNode>(dag),
                    s.fx.vg_lod_threshold,
                    vg_eye, kHalfFov,
                    static_cast<std::uint32_t>(s.fx.vg_lod_vp_h));
                constexpr cd::math::Vec3f kAnchor { 3.5F, 2.0F, 0.0F };
                const auto draw_vg_sphere =
                    [&](const cd::math::Vec3f& pos, float radius,
                        const cd::math::Vec3f& tint) {
                    PrimPush pp {};
                    cd::math::Mat4f model { cd::math::Mat4f::identity() };
                    model[0][0] = radius;
                    model[1][1] = radius;
                    model[2][2] = radius;
                    model[3][0] = pos.x;
                    model[3][1] = pos.y;
                    model[3][2] = pos.z;
                    pp.model = model;
                    pp.mvp = vp * model;
                    pp.tint[0] = tint.x;
                    pp.tint[1] = tint.y;
                    pp.tint[2] = tint.z;
                    pp.tint[3] = 1.0F;
                    fill_prim_push_shared(pp, s.fx, sun, s.cam);
                    pp.fx_params[1]  = 0.0F;
                    pp.fx_params4[0] = 0.0F;
                    pp.fx_params4[1] = 0.6F;
                    cmd.push_constants(
                        s.materials.prim.pipeline_layout(),
                        cd::rhi::ShaderStage::kVertex | cd::rhi::ShaderStage::kFragment,
                        0, sizeof(pp), &pp);
                    cmd.draw_indexed(dbg_mesh.index_count, 1, 0, 0, 0);
                    s.counters.increment("draws_vg_lod");
                };
                for (std::size_t ni = 0; ni < dag.size(); ++ni)
                {
                    const bool picked = std::ranges::find(
                        picks, static_cast<std::uint32_t>(ni)) != picks.end();
                    const cd::math::Vec3f pos {
                        kAnchor.x + dag[ni].bounds_sphere.x,
                        kAnchor.y + dag[ni].bounds_sphere.y,
                        kAnchor.z + dag[ni].bounds_sphere.z };
                    const cd::math::Vec3f tint = picked
                        ? cd::math::Vec3f { 0.20F, 0.95F, 0.30F }   // frontier
                        : cd::math::Vec3f { 0.30F, 0.30F, 0.34F };  // refined away
                    draw_vg_sphere(pos, dag[ni].bounds_sphere.w * 0.35F, tint);
                }
                // Virtual camera marker along -Z from the anchor,
                // clamped so it stays in frame at cam_z up to 100.
                const float cam_off = std::min(s.fx.vg_lod_cam_z, 8.0F) * 0.4F;
                draw_vg_sphere(
                    { kAnchor.x, kAnchor.y, kAnchor.z + cam_off },
                    0.08F, { 0.95F, 0.95F, 0.95F });
            }
        }
}

template <typename StateT, typename SunT, typename FillFn>
inline void draw_noise_heightfield_overlay_3d(StateT& s,
                   cd::rhi::ICommandBuffer& cmd,
                   const cd::math::Mat4f& vp,
                   const SunT& sun,
                   FillFn&& fill_prim_push_shared)
{
        // phase1024-3d-viewport-noise-heightfield: 13th application of
        // the canonical sphere-at-position template. 16x16 sphere
        // carpet over a 3x3 m patch; each sphere's height AND
        // brightness encode fbm2_quintic_6oct(u, v, freq) — the 2D
        // noise texture the cloud shader (phase 853) and CPU bakers
        // consume becomes a watchable terrain patch. Dragging the
        // frequency slider in the Texture-Synth panel reshapes it live.
        if (s.fx.noise_show_field_3d)
        {
            const auto& dbg_mesh = s.meshes.sphere;
            if (dbg_mesh.vb.is_valid())
            {
                cmd.bind_vertex_buffer(0, dbg_mesh.vb, 0);
                cmd.bind_index_buffer(dbg_mesh.ib, 0, dbg_mesh.index_type);
                constexpr int kGrid = 16;
                constexpr float kPatch = 3.0F;
                constexpr cd::math::Vec3f kAnchor { -3.5F, 1.0F, 3.0F };
                constexpr float kHeightScale = 0.8F;
                constexpr float kRadius = 0.07F;
                for (int gy = 0; gy < kGrid; ++gy)
                for (int gx = 0; gx < kGrid; ++gx)
                {
                    const float u = static_cast<float>(gx) /
                                    static_cast<float>(kGrid - 1);
                    const float v = static_cast<float>(gy) /
                                    static_cast<float>(kGrid - 1);
                    const float n = cd::texture_synth::fbm2_quintic_6oct(
                        u, v, static_cast<float>(s.fx.noise_freq));
                    const cd::math::Vec3f pos {
                        kAnchor.x + (u - 0.5F) * kPatch,
                        kAnchor.y + n * kHeightScale,
                        kAnchor.z + (v - 0.5F) * kPatch };
                    const float shade = 0.20F + 0.75F * n;
                    PrimPush pp {};
                    cd::math::Mat4f model { cd::math::Mat4f::identity() };
                    model[0][0] = kRadius;
                    model[1][1] = kRadius;
                    model[2][2] = kRadius;
                    model[3][0] = pos.x;
                    model[3][1] = pos.y;
                    model[3][2] = pos.z;
                    pp.model = model;
                    pp.mvp = vp * model;
                    pp.tint[0] = shade;
                    pp.tint[1] = shade;
                    pp.tint[2] = shade;
                    pp.tint[3] = 1.0F;
                    fill_prim_push_shared(pp, s.fx, sun, s.cam);
                    pp.fx_params[1]  = 0.0F;
                    pp.fx_params4[0] = 0.0F;
                    pp.fx_params4[1] = 0.65F;
                    cmd.push_constants(
                        s.materials.prim.pipeline_layout(),
                        cd::rhi::ShaderStage::kVertex | cd::rhi::ShaderStage::kFragment,
                        0, sizeof(pp), &pp);
                    cmd.draw_indexed(dbg_mesh.index_count, 1, 0, 0, 0);
                    s.counters.increment("draws_noise_field");
                }
            }
        }
}

template <typename StateT, typename SunT, typename FillFn>
inline void draw_restir_reservoir_overlay_3d(StateT& s,
                   cd::rhi::ICommandBuffer& cmd,
                   const cd::math::Mat4f& vp,
                   const SunT& sun,
                   FillFn&& fill_prim_push_shared)
{
        // phase1025-3d-viewport-restir-reservoir: 14th application of
        // the canonical sphere-at-position template. Re-runs the ReSTIR
        // DI panel's deterministic WRS stream (same seed + budget via
        // fx) and renders the 8 light candidates as a sphere row:
        //   - brightness = candidate radiance, log2-scaled over the
        //     8.0 .. 0.0625 table range;
        //   - radius     = how often that candidate was streamed
        //     (uniform proposal -> roughly equal, jitter visible);
        //   - large warm sphere floating above a column = the WRS
        //     survivor. Same seed = same survivor (reproducible).
        if (s.fx.restir_show_3d)
        {
            const auto& dbg_mesh = s.meshes.sphere;
            if (dbg_mesh.vb.is_valid())
            {
                cmd.bind_vertex_buffer(0, dbg_mesh.vb, 0);
                cmd.bind_index_buffer(dbg_mesh.ib, 0, dbg_mesh.index_type);
                cd::restir_di::Reservoir res {};
                std::uint32_t rng = s.fx.restir_seed;
                const auto next_u32 = [&rng]() noexcept -> std::uint32_t
                {
                    rng = rng * 1664525U + 1013904223U;
                    return rng;
                };
                const auto next_unit = [&next_u32]() noexcept -> float
                {
                    return static_cast<float>(next_u32() & 0xFFFFFFU)
                         / static_cast<float>(0xFFFFFFU);
                };
                constexpr std::array<float, 8> kRad {
                    8.0F, 4.0F, 2.0F, 1.0F, 0.5F, 0.25F, 0.125F, 0.0625F };
                std::array<std::uint32_t, 8> hist {};
                for (int i = 0; i < s.fx.restir_samples; ++i)
                {
                    const auto li = static_cast<std::uint32_t>(next_u32() & 7U);
                    cd::restir_di::Sample smp {};
                    smp.light_index = li;
                    smp.radiance    = { kRad[li], kRad[li], kRad[li] };
                    smp.target_pdf  = kRad[li];
                    cd::restir_di::update(res, smp, smp.target_pdf, next_unit());
                    hist[li] += 1U;
                }
                const auto total = static_cast<float>(
                    std::max(s.fx.restir_samples, 1));
                constexpr cd::math::Vec3f kAnchor { 3.5F, 1.0F, 3.0F };
                constexpr float kColSpacing = 0.45F;
                const auto draw_restir_sphere =
                    [&](const cd::math::Vec3f& pos, float radius,
                        const cd::math::Vec3f& tint) {
                    PrimPush pp {};
                    cd::math::Mat4f model { cd::math::Mat4f::identity() };
                    model[0][0] = radius;
                    model[1][1] = radius;
                    model[2][2] = radius;
                    model[3][0] = pos.x;
                    model[3][1] = pos.y;
                    model[3][2] = pos.z;
                    pp.model = model;
                    pp.mvp = vp * model;
                    pp.tint[0] = tint.x;
                    pp.tint[1] = tint.y;
                    pp.tint[2] = tint.z;
                    pp.tint[3] = 1.0F;
                    fill_prim_push_shared(pp, s.fx, sun, s.cam);
                    pp.fx_params[1]  = 0.0F;
                    pp.fx_params4[0] = 0.0F;
                    pp.fx_params4[1] = 0.55F;
                    cmd.push_constants(
                        s.materials.prim.pipeline_layout(),
                        cd::rhi::ShaderStage::kVertex | cd::rhi::ShaderStage::kFragment,
                        0, sizeof(pp), &pp);
                    cmd.draw_indexed(dbg_mesh.index_count, 1, 0, 0, 0);
                    s.counters.increment("draws_restir");
                };
                for (std::size_t li = 0; li < 8; ++li)
                {
                    // log2 maps the 8.0 .. 0.0625 table onto [1, 0].
                    const float shade = 0.18F + 0.77F *
                        ((std::log2(kRad[li]) + 4.0F) / 7.0F);
                    const float radius = 0.06F + 0.10F *
                        (static_cast<float>(hist[li]) / total) * 8.0F * 0.5F;
                    draw_restir_sphere(
                        { kAnchor.x + static_cast<float>(li) * kColSpacing,
                          kAnchor.y, kAnchor.z },
                        radius, { shade, shade, shade });
                }
                const auto win = res.selected.light_index;
                if (win < 8U)
                {
                    draw_restir_sphere(
                        { kAnchor.x + static_cast<float>(win) * kColSpacing,
                          kAnchor.y + 0.45F, kAnchor.z },
                        0.14F, { 0.95F, 0.75F, 0.18F });
                }
            }
        }
}

template <typename StateT, typename SunT, typename FillFn>
inline void draw_earth_globe_overlay_3d(StateT& s,
                   cd::rhi::ICommandBuffer& cmd,
                   const cd::math::Mat4f& vp,
                   const SunT& sun,
                   FillFn&& fill_prim_push_shared)
{
        // phase1026-3d-viewport-earth-globe: 15th application of the
        // canonical sphere-at-position template. Renders a globe of
        // small spheres tinted by the SAME fbm2 + pole-falloff +
        // land/sea/snow ramp the Texture-Synth panel evaluates for a
        // single (u, v): green-brown continents above n=0.48, depth-
        // ramped blue oceans below, white caps where the pole falloff
        // crosses 0.18. The procedural planet the
        // bake_earth_albedo_rgba8 baker would produce becomes a
        // watchable 3D ball.
        if (s.fx.earth_show_globe_3d)
        {
            const auto& dbg_mesh = s.meshes.sphere;
            if (dbg_mesh.vb.is_valid())
            {
                cmd.bind_vertex_buffer(0, dbg_mesh.vb, 0);
                cmd.bind_index_buffer(dbg_mesh.ib, 0, dbg_mesh.index_type);
                constexpr cd::math::Vec3f kAnchor { -3.5F, 2.5F, -3.0F };
                constexpr float kGlobeR = 1.1F;
                constexpr int kRings = 8;
                constexpr int kAzims = 12;
                constexpr float kPi = std::numbers::pi_v<float>;
                const auto earth_color_at =
                    [](float u, float v) -> cd::math::Vec3f {
                    const float lat = (v - 0.5F) * std::numbers::pi_v<float>;
                    const float pole_falloff = std::cos(lat);
                    float n = cd::texture_synth::fbm2(u, v, 6.0F);
                    n = n * pole_falloff + 0.15F * (1.0F - pole_falloff);
                    cd::math::Vec3f earth {};
                    if (n > 0.48F)
                    {
                        const float t = std::clamp((n - 0.48F) / 0.52F, 0.0F, 1.0F);
                        const cd::math::Vec3f low  { 0.30F, 0.55F, 0.18F };
                        const cd::math::Vec3f mid  { 0.55F, 0.45F, 0.20F };
                        const cd::math::Vec3f high { 0.90F, 0.88F, 0.82F };
                        if (t < 0.5F)
                        {
                            const float k = t * 2.0F;
                            earth = { low.x + (mid.x - low.x) * k,
                                      low.y + (mid.y - low.y) * k,
                                      low.z + (mid.z - low.z) * k };
                        }
                        else
                        {
                            const float k = (t - 0.5F) * 2.0F;
                            earth = { mid.x + (high.x - mid.x) * k,
                                      mid.y + (high.y - mid.y) * k,
                                      mid.z + (high.z - mid.z) * k };
                        }
                    }
                    else
                    {
                        const float depth = std::clamp((0.48F - n) / 0.48F, 0.0F, 1.0F);
                        earth = { 0.08F + (0.20F - 0.08F) * (1.0F - depth),
                                  0.25F + (0.50F - 0.25F) * (1.0F - depth),
                                  0.50F + (0.78F - 0.50F) * (1.0F - depth) };
                    }
                    if (pole_falloff < 0.18F)
                        earth = { 0.92F, 0.94F, 0.97F };
                    return earth;
                };
                const auto draw_earth_sphere =
                    [&](float el, float az, float radius) {
                    const float ce = std::cos(el);
                    const cd::math::Vec3f nd {
                        ce * std::cos(az), std::sin(el), ce * std::sin(az) };
                    const float u = az / (2.0F * kPi);
                    const float v = el / kPi + 0.5F;
                    const auto col = earth_color_at(u, v);
                    PrimPush pp {};
                    cd::math::Mat4f model { cd::math::Mat4f::identity() };
                    model[0][0] = radius;
                    model[1][1] = radius;
                    model[2][2] = radius;
                    model[3][0] = kAnchor.x + nd.x * kGlobeR;
                    model[3][1] = kAnchor.y + nd.y * kGlobeR;
                    model[3][2] = kAnchor.z + nd.z * kGlobeR;
                    pp.model = model;
                    pp.mvp = vp * model;
                    pp.tint[0] = col.x;
                    pp.tint[1] = col.y;
                    pp.tint[2] = col.z;
                    pp.tint[3] = 1.0F;
                    fill_prim_push_shared(pp, s.fx, sun, s.cam);
                    pp.fx_params[1]  = 0.0F;
                    pp.fx_params4[0] = 0.0F;
                    pp.fx_params4[1] = 0.6F;
                    cmd.push_constants(
                        s.materials.prim.pipeline_layout(),
                        cd::rhi::ShaderStage::kVertex | cd::rhi::ShaderStage::kFragment,
                        0, sizeof(pp), &pp);
                    cmd.draw_indexed(dbg_mesh.index_count, 1, 0, 0, 0);
                    s.counters.increment("draws_earth_globe");
                };
                for (int ri = 1; ri <= kRings; ++ri)
                {
                    const float el = kPi * static_cast<float>(ri) /
                                     static_cast<float>(kRings + 1) - kPi * 0.5F;
                    for (int ai = 0; ai < kAzims; ++ai)
                    {
                        const float az = 2.0F * kPi * static_cast<float>(ai) /
                                         static_cast<float>(kAzims);
                        draw_earth_sphere(el, az, 0.085F);
                    }
                }
                draw_earth_sphere( kPi * 0.5F - 0.01F, 0.0F, 0.085F);
                draw_earth_sphere(-kPi * 0.5F + 0.01F, 0.0F, 0.085F);
            }
        }
}

template <typename StateT, typename SunT, typename FillFn>
inline void draw_camera_basis_overlay_3d(StateT& s,
                   cd::rhi::ICommandBuffer& cmd,
                   const cd::math::Mat4f& vp,
                   const SunT& sun,
                   FillFn&& fill_prim_push_shared)
{
        // phase1027-3d-viewport-camera-basis: 16th application of the
        // canonical sphere-at-position template. Derives the SAME
        // free-look basis the Camera Basis Probe panel computes
        // (yaw/pitch -> forward, right = forward x world-up, up =
        // right x forward) and renders it as a triad at the probe
        // position: white sphere at the position, red at +right,
        // green at +up, blue at +forward (axis tips at 0.9 m), plus
        // 3 mid-axis dots so each axis reads as a short arm rather
        // than a floating dot.
        if (s.fx.camera_basis_show_3d)
        {
            const auto& dbg_mesh = s.meshes.sphere;
            if (dbg_mesh.vb.is_valid())
            {
                cmd.bind_vertex_buffer(0, dbg_mesh.vb, 0);
                cmd.bind_index_buffer(dbg_mesh.ib, 0, dbg_mesh.index_type);
                const float yaw_r = s.fx.camera_basis_yaw_deg *
                                    std::numbers::pi_v<float> / 180.0F;
                const float pit_r = s.fx.camera_basis_pitch_deg *
                                    std::numbers::pi_v<float> / 180.0F;
                const cd::math::Vec3f fwd {
                    std::cos(pit_r) * std::sin(yaw_r),
                    std::sin(pit_r),
                   -std::cos(pit_r) * std::cos(yaw_r) };
                cd::math::Vec3f rgt {
                    fwd.y * 0.0F - fwd.z * 1.0F,
                    fwd.z * 0.0F - fwd.x * 0.0F,
                    fwd.x * 1.0F - fwd.y * 0.0F };
                const float rln = 1.0F / std::max(std::sqrt(
                    rgt.x * rgt.x + rgt.y * rgt.y + rgt.z * rgt.z), 1e-3F);
                rgt = { rgt.x * rln, rgt.y * rln, rgt.z * rln };
                const cd::math::Vec3f up_v {
                    rgt.y * fwd.z - rgt.z * fwd.y,
                    rgt.z * fwd.x - rgt.x * fwd.z,
                    rgt.x * fwd.y - rgt.y * fwd.x };
                const cd::math::Vec3f base {
                    s.fx.camera_basis_pos[0],
                    s.fx.camera_basis_pos[1],
                    s.fx.camera_basis_pos[2] };
                struct BasisSphere
                {
                    cd::math::Vec3f pos;
                    cd::math::Vec3f tint;
                    float radius { 0.0F };
                };
                constexpr float kArm = 0.9F;
                std::array<BasisSphere, 7> tri {};
                tri[0] = { base, { 0.95F, 0.95F, 0.95F }, 0.10F };
                const std::array<cd::math::Vec3f, 3> axes { rgt, up_v, fwd };
                const std::array<cd::math::Vec3f, 3> tints {{
                    { 0.95F, 0.18F, 0.18F },
                    { 0.20F, 0.95F, 0.30F },
                    { 0.30F, 0.45F, 0.95F } }};
                for (std::size_t ai = 0; ai < 3; ++ai)
                {
                    tri[1 + ai * 2] = {
                        { base.x + axes[ai].x * kArm * 0.5F,
                          base.y + axes[ai].y * kArm * 0.5F,
                          base.z + axes[ai].z * kArm * 0.5F },
                        tints[ai], 0.05F };
                    tri[2 + ai * 2] = {
                        { base.x + axes[ai].x * kArm,
                          base.y + axes[ai].y * kArm,
                          base.z + axes[ai].z * kArm },
                        tints[ai], 0.09F };
                }
                for (const auto& sp : tri)
                {
                    PrimPush pp {};
                    cd::math::Mat4f model { cd::math::Mat4f::identity() };
                    model[0][0] = sp.radius;
                    model[1][1] = sp.radius;
                    model[2][2] = sp.radius;
                    model[3][0] = sp.pos.x;
                    model[3][1] = sp.pos.y;
                    model[3][2] = sp.pos.z;
                    pp.model = model;
                    pp.mvp = vp * model;
                    pp.tint[0] = sp.tint.x;
                    pp.tint[1] = sp.tint.y;
                    pp.tint[2] = sp.tint.z;
                    pp.tint[3] = 1.0F;
                    fill_prim_push_shared(pp, s.fx, sun, s.cam);
                    pp.fx_params[1]  = 0.0F;
                    pp.fx_params4[0] = 0.0F;
                    pp.fx_params4[1] = 0.5F;
                    cmd.push_constants(
                        s.materials.prim.pipeline_layout(),
                        cd::rhi::ShaderStage::kVertex | cd::rhi::ShaderStage::kFragment,
                        0, sizeof(pp), &pp);
                    cmd.draw_indexed(dbg_mesh.index_count, 1, 0, 0, 0);
                    s.counters.increment("draws_camera_basis");
                }
                // phase1032-line-upgrades: solid axis arms base->tip
                // so the triad reads as a gizmo, not 3 floating dots.
                // phase1043-arrow-upgrade: arms became ARROWS — the
                // head disambiguates +axis from -axis at a glance.
                for (std::size_t ai = 0; ai < 3; ++ai)
                {
                    s.debug_lines.add_arrow(
                        base,
                        { base.x + axes[ai].x * kArm,
                          base.y + axes[ai].y * kArm,
                          base.z + axes[ai].z * kArm },
                        { tints[ai].x, tints[ai].y, tints[ai].z, 1.0F });
                }
            }
        }
}

template <typename StateT, typename SunT, typename FillFn>
inline void draw_cct_sweep_overlay_3d(StateT& s,
                   cd::rhi::ICommandBuffer& cmd,
                   const cd::math::Mat4f& vp,
                   const SunT& sun,
                   FillFn&& fill_prim_push_shared)
{
        // phase1028-3d-viewport-cct-sweep: 17th application of the
        // canonical sphere-at-position template. A 16-sphere rail
        // sweeps 1500 K -> 15000 K through cct_to_linear_rgb; a
        // larger marker sphere rides the rail at the panel's Kelvin
        // slider position (tinted with its own CCT colour). Firelight
        // orange -> tungsten -> noon white -> sky-shade blue becomes a
        // physical colour ramp in the scene.
        if (s.fx.cct_show_sweep_3d)
        {
            const auto& dbg_mesh = s.meshes.sphere;
            if (dbg_mesh.vb.is_valid())
            {
                cmd.bind_vertex_buffer(0, dbg_mesh.vb, 0);
                cmd.bind_index_buffer(dbg_mesh.ib, 0, dbg_mesh.index_type);
                constexpr cd::math::Vec3f kRailStart { -3.5F, 0.6F, -1.5F };
                constexpr float kRailLen = 4.0F;
                constexpr float kLoK = 1500.0F;
                constexpr float kHiK = 15000.0F;
                const auto draw_cct_sphere =
                    [&](float t01, float radius, float kelvin) {
                    const auto col = cd::light::cct_to_linear_rgb(kelvin);
                    PrimPush pp {};
                    cd::math::Mat4f model { cd::math::Mat4f::identity() };
                    model[0][0] = radius;
                    model[1][1] = radius;
                    model[2][2] = radius;
                    model[3][0] = kRailStart.x + t01 * kRailLen;
                    model[3][1] = kRailStart.y + ((radius > 0.10F) ? 0.35F : 0.0F);
                    model[3][2] = kRailStart.z;
                    pp.model = model;
                    pp.mvp = vp * model;
                    pp.tint[0] = col.x;
                    pp.tint[1] = col.y;
                    pp.tint[2] = col.z;
                    pp.tint[3] = 1.0F;
                    fill_prim_push_shared(pp, s.fx, sun, s.cam);
                    pp.fx_params[1]  = 0.0F;
                    pp.fx_params4[0] = 0.0F;
                    pp.fx_params4[1] = 0.45F;
                    cmd.push_constants(
                        s.materials.prim.pipeline_layout(),
                        cd::rhi::ShaderStage::kVertex | cd::rhi::ShaderStage::kFragment,
                        0, sizeof(pp), &pp);
                    cmd.draw_indexed(dbg_mesh.index_count, 1, 0, 0, 0);
                    s.counters.increment("draws_cct_sweep");
                };
                constexpr int kSweep = 16;
                for (int i = 0; i < kSweep; ++i)
                {
                    const float t = static_cast<float>(i) /
                                    static_cast<float>(kSweep - 1);
                    draw_cct_sphere(t, 0.08F, kLoK + t * (kHiK - kLoK));
                }
                const float marker_t = std::clamp(
                    (s.fx.cct_kelvin - kLoK) / (kHiK - kLoK), 0.0F, 1.0F);
                draw_cct_sphere(marker_t, 0.14F, s.fx.cct_kelvin);
            }
        }
}

template <typename StateT, typename SunT, typename FillFn>
inline void draw_attenuation_rail_overlay_3d(StateT& s,
                   cd::rhi::ICommandBuffer& cmd,
                   const cd::math::Mat4f& vp,
                   const SunT& sun,
                   FillFn&& fill_prim_push_shared)
{
        // phase1029-3d-viewport-attenuation-rail: 18th application of
        // the canonical sphere-at-position template. A warm "light"
        // marker sphere plus a 20-sphere rail marching away from it
        // over 1.5x range; each sphere's brightness encodes
        // cd::light::distance_attenuation(d, range) — the Frostbite
        // 2014 windowed inverse-square. The spheres past `range`
        // going black make the window cutoff physically visible.
        if (s.fx.atten_show_rail_3d)
        {
            const auto& dbg_mesh = s.meshes.sphere;
            if (dbg_mesh.vb.is_valid())
            {
                cmd.bind_vertex_buffer(0, dbg_mesh.vb, 0);
                cmd.bind_index_buffer(dbg_mesh.ib, 0, dbg_mesh.index_type);
                constexpr cd::math::Vec3f kLightPos { -3.5F, 0.6F, 1.5F };
                constexpr float kRailWorldLen = 4.0F;
                constexpr int kRailCount = 20;
                const auto draw_atten_sphere =
                    [&](const cd::math::Vec3f& pos, float radius,
                        const cd::math::Vec3f& tint) {
                    PrimPush pp {};
                    cd::math::Mat4f model { cd::math::Mat4f::identity() };
                    model[0][0] = radius;
                    model[1][1] = radius;
                    model[2][2] = radius;
                    model[3][0] = pos.x;
                    model[3][1] = pos.y;
                    model[3][2] = pos.z;
                    pp.model = model;
                    pp.mvp = vp * model;
                    pp.tint[0] = tint.x;
                    pp.tint[1] = tint.y;
                    pp.tint[2] = tint.z;
                    pp.tint[3] = 1.0F;
                    fill_prim_push_shared(pp, s.fx, sun, s.cam);
                    pp.fx_params[1]  = 0.0F;
                    pp.fx_params4[0] = 0.0F;
                    pp.fx_params4[1] = 0.5F;
                    cmd.push_constants(
                        s.materials.prim.pipeline_layout(),
                        cd::rhi::ShaderStage::kVertex | cd::rhi::ShaderStage::kFragment,
                        0, sizeof(pp), &pp);
                    cmd.draw_indexed(dbg_mesh.index_count, 1, 0, 0, 0);
                    s.counters.increment("draws_atten_rail");
                };
                draw_atten_sphere(kLightPos, 0.14F,
                                  { 0.95F, 0.80F, 0.35F });  // light marker
                // phase1043-sphere-upgrade: wireframe RANGE sphere via
                // cd::debug_line at the exact world radius where the
                // rail maps `range` (rail: d in [0, 1.5*range] over
                // 4 m, so range sits at 4 / 1.5 m) — the Frostbite
                // window cutoff becomes a visible boundary shell.
                s.debug_lines.add_sphere(
                    kLightPos, kRailWorldLen / 1.5F, 32,
                    { 0.95F, 0.80F, 0.35F, 1.0F });
                for (int i = 1; i <= kRailCount; ++i)
                {
                    const float t = static_cast<float>(i) /
                                    static_cast<float>(kRailCount);
                    const float d = t * s.fx.atten_range * 1.5F;
                    const float a = cd::light::distance_attenuation(
                        d, s.fx.atten_range);
                    // Normalise against the first rail sample so the
                    // near end reads bright regardless of range.
                    const float a0 = cd::light::distance_attenuation(
                        (1.0F / static_cast<float>(kRailCount)) *
                            s.fx.atten_range * 1.5F,
                        s.fx.atten_range);
                    const float shade = 0.04F + 0.92F *
                        std::clamp(a / std::max(a0, 1e-4F), 0.0F, 1.0F);
                    draw_atten_sphere(
                        { kLightPos.x + t * kRailWorldLen,
                          kLightPos.y, kLightPos.z },
                        0.07F, { shade, shade * 0.92F, shade * 0.70F });
                }
            }
        }
}

}  // namespace cd_sample
