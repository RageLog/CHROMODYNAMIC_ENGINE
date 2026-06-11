// =============================================================================
// HelloViewportDemos.hpp
// -----------------------------------------------------------------------------
// phase1076 — HelloFrameLoop extraction, batch 1: the sphere-template
// 3D viewport demo blocks (phases 1005-1010) moved verbatim out of
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

}  // namespace cd_sample
