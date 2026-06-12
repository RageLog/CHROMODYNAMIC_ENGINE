// =============================================================================
// HelloShowcaseProbes.hpp
// -----------------------------------------------------------------------------
// phase1099 — R-Showcase "Run25" CPU probe sections (texture-synth /
// decal / particles / VG / VT / meshlet / BRDF family / atmosphere /
// shafts / LTC / ReSTIR GI / IBL / camera / frustum / CCT /
// attenuation / cubemap / CSM / cluster / motion-vector / input /
// ECS stress / scene serializer / audio tone / slerp / math helpers /
// bezier / asset tag / RNG) moved VERBATIM out of
// draw_r_showcase_panel (2310 lines -> ~700). Template over the
// main.cpp-local LightRow/SceneEntity types — the established
// HelloViewportDemos extraction shape. Function-local statics keep
// per-probe demo state; the single instantiation in main.cpp
// preserves behaviour exactly.
// =============================================================================
#pragma once

#include "HelloEngineFx.hpp"

#include <cd/asset/AssetId.hpp>
#include <cd/asset/AssetRegistry.hpp>
#include <cd/atmosphere/Atmosphere.hpp>
#include <cd/audio/Mixer.hpp>
#include <cd/brdf/ltc/Ltc.hpp>
#include <cd/brdf/sheen_clearcoat/SheenClearcoat.hpp>
#include <cd/brdf/sss/Sss.hpp>
#include <cd/camera/Camera.hpp>
#include <cd/camera/Frustum.hpp>
#include <cd/decal/Decal.hpp>
#include <cd/ecs/Entity.hpp>
#include <cd/ecs/World.hpp>
#include <cd/gpu_particles/GpuParticles.hpp>
#include <cd/ibl/BrdfLut.hpp>
#include <cd/ibl/Cubemap.hpp>
#include <cd/input/Axis.hpp>
#include <cd/light/Attenuation.hpp>
#include <cd/light/CascadedShadow.hpp>
#include <cd/light/ClusterGrid.hpp>
#include <cd/light/ColorTemperature.hpp>
#include <cd/light/Light.hpp>
#include <cd/light_shafts/LightShafts.hpp>
#include <cd/math/Easing.hpp>
#include <cd/math/Functions.hpp>
#include <cd/math/Matrix.hpp>
#include <cd/math/Quaternion.hpp>
#include <cd/math/Spline.hpp>
#include <cd/math/Vector.hpp>
#include <cd/mesh_shader/Meshlet.hpp>
#include <cd/restir_gi/GiReservoir.hpp>
#include <cd/scene/Scene.hpp>
#include <cd/scene/Serializer.hpp>
#include <cd/texture_synth/Earth.hpp>
#include <cd/texture_synth/Noise.hpp>
#include <cd/velocity/Velocity.hpp>
#include <cd/virtual_geometry/VirtualGeometry.hpp>
#include <cd/virtual_textures/VirtualTextures.hpp>

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <numbers>
#include <string>
#include <vector>

namespace cd_sample {

// (The probe bodies only touch fx + ImGui + the libraries — the
// dependency scan's lights/entities hits were comment text, so no
// template indirection is needed.)
inline void draw_run25_probes(HelloEngineFx& fx)
{
    // phase925-texture-synth-live-demo (Run 25 Strand B): CPU
    // noise-quality probe. Lets the user compare cubic vs quintic
    // Hermite smoothing live across one row of a noise texture --
    // the same fix that landed in phase 853 for the cloud shader.
    // Useful when picking the right kernel for a CPU-baked detail
    // map (e.g. an asset cooker that needs the edge-free quintic
    // for diagonal seams).
    if (ImGui::CollapsingHeader("Run25  Texture-Synth Noise Probe"))
    {
        // phase1024-3d-viewport-noise-heightfield: freq migrated onto
        // HelloEngineFx so the 3D overlay reshapes with the SAME value.
        static float s_ns_row_v = 0.5F;
        ImGui::SliderInt("Noise frequency", &fx.noise_freq, 2, 32);
        ImGui::SliderFloat("Sample row v",  &s_ns_row_v, 0.0F, 1.0F);
        const int s_ns_freq = fx.noise_freq;
        constexpr int kCols = 256;
        std::array<float, kCols> row_cubic  {};
        std::array<float, kCols> row_quintic {};
        std::array<float, kCols> row_fbm6   {};
        for (int i = 0; i < kCols; ++i)
        {
            const float u = static_cast<float>(i) / static_cast<float>(kCols - 1);
            row_cubic[static_cast<std::size_t>(i)] =
                cd::texture_synth::value_noise2(u, s_ns_row_v,
                                                static_cast<float>(s_ns_freq));
            row_quintic[static_cast<std::size_t>(i)] =
                cd::texture_synth::value_noise2_quintic(u, s_ns_row_v,
                                                        static_cast<float>(s_ns_freq));
            row_fbm6[static_cast<std::size_t>(i)] =
                cd::texture_synth::fbm2_quintic_6oct(u, s_ns_row_v,
                                                    static_cast<float>(s_ns_freq));
        }
        ImGui::PlotLines(
            "##ns_cubic",
            row_cubic.data(), kCols, 0,
            "Cubic Hermite (3t^2 - 2t^3)",
            0.0F, 1.0F, ImVec2(0, 56));
        ImGui::PlotLines(
            "##ns_quintic",
            row_quintic.data(), kCols, 0,
            "Quintic Hermite (6t^5 - 15t^4 + 10t^3)",
            0.0F, 1.0F, ImVec2(0, 56));
        ImGui::PlotLines(
            "##ns_fbm6",
            row_fbm6.data(), kCols, 0,
            "fBm 6-oct quintic",
            0.0F, 1.0F, ImVec2(0, 56));
        // Earth procedural single-pixel preview -- runs the same
        // fbm2 + colour ramp the bake_earth_albedo_rgba8 baker uses,
        // proves the cd::texture_synth::Earth module compiles +
        // is reachable from hello_engine without baking a full
        // texture every frame.
        static float s_ns_earth_u = 0.30F;
        static float s_ns_earth_v = 0.55F;
        ImGui::SliderFloat("Earth sample u", &s_ns_earth_u, 0.0F, 1.0F);
        ImGui::SliderFloat("Earth sample v", &s_ns_earth_v, 0.0F, 1.0F);
        const float lat = (s_ns_earth_v - 0.5F) * std::numbers::pi_v<float>;
        const float pole_falloff = std::cos(lat);
        float n = cd::texture_synth::fbm2(s_ns_earth_u, s_ns_earth_v, 6.0F);
        n = n * pole_falloff + 0.15F * (1.0F - pole_falloff);
        const bool is_land = n > 0.48F;
        cd::math::Vec3f earth {};
        if (is_land)
        {
            const float t = std::clamp((n - 0.48F) / 0.52F, 0.0F, 1.0F);
            cd::math::Vec3f low  { 0.30F, 0.55F, 0.18F };
            cd::math::Vec3f mid  { 0.55F, 0.45F, 0.20F };
            cd::math::Vec3f high { 0.90F, 0.88F, 0.82F };
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
            const float ocean_depth = std::clamp((0.48F - n) / 0.48F, 0.0F, 1.0F);
            earth = { 0.08F + (0.20F - 0.08F) * (1.0F - ocean_depth),
                      0.25F + (0.50F - 0.25F) * (1.0F - ocean_depth),
                      0.50F + (0.78F - 0.50F) * (1.0F - ocean_depth) };
        }
        if (pole_falloff < 0.18F)
            earth = { 0.92F, 0.94F, 0.97F };
        ImGui::ColorButton("Earth sample",
                           { earth.x, earth.y, earth.z, 1.0F },
                           ImGuiColorEditFlags_NoAlpha, ImVec2(64, 24));
        ImGui::SameLine();
        const char* biome = "sea";
        if (is_land)
            biome = "land";
        else if (pole_falloff < 0.18F)
            biome = "snow";
        ImGui::Text("Earth (%s) noise=%.3f", biome, static_cast<double>(n));
        ImGui::TextDisabled("Quintic eliminates the cubic axis-aligned ridges");
        ImGui::TextDisabled("(Perlin 2002 improvement; clouds shader phase 853).");
        ImGui::Separator();
        ImGui::Checkbox("Show noise height-field in 3D viewport",
                        &fx.noise_show_field_3d);
        if (fx.noise_show_field_3d)
        {
            ImGui::TextDisabled("  16x16 sphere carpet; height + brightness = fbm 6-oct quintic");
        }
        ImGui::Checkbox("Show Earth globe in 3D viewport",
                        &fx.earth_show_globe_3d);
        if (fx.earth_show_globe_3d)
        {
            ImGui::TextDisabled("  ~98-sphere globe; continents/oceans/polar caps from the same ramp");
        }
    }
    // phase926-decal-live-demo (Run 25 Strand B): CPU-side
    // cd::decal::project_world_to_decal + decal_intersects_aabb
    // probe. Lets the user drag a decal OBB centre / extents and
    // a test point + sees inside/outside + local NDC + atlas UV
    // resolve LIVE. Same math the GBuffer decal pass runs per
    // pixel; here we make it click-debuggable.
    if (ImGui::CollapsingHeader("Run25  Decal Projector Probe"))
    {
        // phase1008-3d-viewport-decal-obb-gizmo: decal state migrated
        // off function-statics onto HelloEngineFx so the 3D-viewport
        // overlay (added below + in the render loop) can read the
        // SAME values the user is dragging here. fx stores std::array
        // for include-light test target; we shadow into a Decal here.
        ImGui::SliderFloat3("Decal position",
                            fx.decal_demo_position.data(), -4.0F, 4.0F);
        ImGui::SliderFloat3("Decal half-extents",
                            fx.decal_demo_half_extents.data(), 0.1F, 4.0F);
        ImGui::SliderFloat3("Test point (world)",
                            fx.decal_demo_test_point.data(), -4.0F, 4.0F);
        cd::decal::Decal s_decal {};
        s_decal.position     = { fx.decal_demo_position[0],
                                 fx.decal_demo_position[1],
                                 fx.decal_demo_position[2] };
        s_decal.half_extents = { fx.decal_demo_half_extents[0],
                                 fx.decal_demo_half_extents[1],
                                 fx.decal_demo_half_extents[2] };
        const cd::math::Vec3f test_point {
            fx.decal_demo_test_point[0],
            fx.decal_demo_test_point[1],
            fx.decal_demo_test_point[2] };
        cd::math::Vec3f local {};
        std::array<float, 2> uv {};
        const bool inside =
            cd::decal::project_world_to_decal(s_decal, test_point, local, uv);
        ImGui::Text("Inside OBB: %s", inside ? "YES" : "NO");
        ImGui::Text("Local NDC (right, up, fwd): (%.3f, %.3f, %.3f)",
                    static_cast<double>(local.x),
                    static_cast<double>(local.y),
                    static_cast<double>(local.z));
        ImGui::Text("Atlas UV: (%.3f, %.3f)",
                    static_cast<double>(uv[0]),
                    static_cast<double>(uv[1]));
        // Fixed scene AABB so the user can also see the binning
        // path's inside/outside result -- which clusters / tiles
        // the production pass would touch.
        constexpr cd::math::Vec3f kAabbMin { -1.0F, -1.0F, -1.0F };
        constexpr cd::math::Vec3f kAabbMax {  1.0F,  1.0F,  1.0F };
        const bool aabb_hit =
            cd::decal::decal_intersects_aabb(s_decal, kAabbMin, kAabbMax);
        ImGui::Text("Intersects scene AABB [-1, 1]^3: %s",
                    aabb_hit ? "YES" : "NO");
        // phase1008: 3D viewport gizmo. When checked, the render loop
        // emits 1 sphere at the OBB centre + 8 spheres at the OBB
        // corners. Centre tint encodes the AABB-intersect result
        // (green=hits scene AABB, red=miss); corner tint is white.
        ImGui::Separator();
        ImGui::Checkbox("Show decal OBB in 3D viewport",
                        &fx.decal_show_obb_3d);
        if (fx.decal_show_obb_3d)
        {
            ImGui::TextDisabled("  centre tint = AABB-hit (green) / miss (red); corners = white");
            ImGui::TextDisabled("  12 wireframe edges via cd::debug_line (phase 1031)");
        }
        ImGui::TextDisabled("Same math as the GBuffer decal pass.");
        ImGui::TextDisabled("Persson 2009 / Filion 2012 cluster binning.");
    }
    // phase927-gpu-particles-live-demo (Run 25 Strand B): drive
    // cd::gpu_particles::advance + compact_alive on a small CPU
    // particle pool. Lets the user click "Step 1 dt" repeatedly and
    // watch the live count drop as particles die + plot the count
    // history. Same advance + compact path the kSimulateCS kernel
    // runs per particle on the GPU.
    if (ImGui::CollapsingHeader("Run25  GPU Particles Probe"))
    {
        constexpr int kCapacity = 64;
        static std::array<cd::gpu_particles::Particle, kCapacity> s_particles {};
        static std::vector<float> s_alive_history;
        static bool s_initialised = false;
        static float s_dt = 0.05F;
        static cd::math::Vec3f s_gravity { 0.0F, -9.81F, 0.0F };
        if (!s_initialised)
        {
            for (std::size_t i = 0; i < kCapacity; ++i)
            {
                s_particles[i].life = 1.0F;
                s_particles[i].max_life = 1.0F;
                s_particles[i].velocity = {
                    static_cast<float>(i % 4) - 1.5F,
                    static_cast<float>((i / 4) % 4) * 0.5F,
                    static_cast<float>(i % 3) - 1.0F };
            }
            s_initialised = true;
        }
        ImGui::SliderFloat("dt (s)",      &s_dt, 0.01F, 0.5F);
        ImGui::SliderFloat3("Gravity",    &s_gravity.x, -20.0F, 20.0F);
        if (ImGui::Button("Step 1 dt"))
        {
            cd::gpu_particles::advance(
                std::span<cd::gpu_particles::Particle>(s_particles),
                s_dt,
                s_gravity);
            const auto alive = cd::gpu_particles::compact_alive(
                std::span<cd::gpu_particles::Particle>(s_particles));
            s_alive_history.push_back(static_cast<float>(alive));
            if (s_alive_history.size() > 256U)
                s_alive_history.erase(s_alive_history.begin());
        }
        ImGui::SameLine();
        if (ImGui::Button("Reset particles"))
        {
            s_initialised = false;
            s_alive_history.clear();
        }
        const auto alive_now = cd::gpu_particles::compact_alive(
            std::span<cd::gpu_particles::Particle>(s_particles));
        ImGui::Text("Live particles: %u / %d", alive_now, kCapacity);
        if (!s_alive_history.empty())
        {
            ImGui::PlotLines(
                "##gp_alive",
                s_alive_history.data(),
                static_cast<int>(s_alive_history.size()),
                0,
                "Live count history",
                0.0F,
                static_cast<float>(kCapacity),
                ImVec2(0, 64));
        }
        ImGui::TextDisabled("advance() ticks position + velocity + life;");
        ImGui::TextDisabled("compact_alive() rearranges to drive indirect draw.");
        ImGui::Separator();
        ImGui::Checkbox("Show GPU particles in 3D viewport",
                        &fx.gpu_particles_show_3d);
        if (fx.gpu_particles_show_3d)
        {
            ImGui::TextDisabled("  live 64-particle pool, auto-stepped @ dt=1/60; -y gravity");
            ImGui::TextDisabled("  pool auto-respawns when fully dead");
        }
    }
    // phase927-virtual-geometry-live-demo (Run 25 Strand B): drive
    // cd::virtual_geometry::projected_error_pixels +
    // is_lod_frontier with a synthetic 4-node DAG. Lets the user
    // drag the camera back / forth and watch which cluster IDs the
    // LOD picker keeps as the frontier.
    if (ImGui::CollapsingHeader("Run25  Virtual Geometry LOD Probe"))
    {
        // phase1023-3d-viewport-vg-lod-frontier: state migrated onto
        // HelloEngineFx so the 3D overlay picks with the SAME values.
        const float s_vg_cam_z     = fx.vg_lod_cam_z;
        const float s_vg_threshold = fx.vg_lod_threshold;
        const int   s_vg_vp_h      = fx.vg_lod_vp_h;
        ImGui::SliderFloat("Camera Z (back from origin)",
                           &fx.vg_lod_cam_z, 1.0F, 100.0F, "%.2f");
        ImGui::SliderFloat("LOD threshold (px error)",
                           &fx.vg_lod_threshold, 0.5F, 32.0F, "%.2f");
        ImGui::SliderInt("Viewport height (px)",
                         &fx.vg_lod_vp_h, 240, 2160);
        // Build a 4-node synthetic DAG: 1 coarse parent + 3 finer
        // children with successively smaller self_error.
        std::array<cd::virtual_geometry::ClusterNode, 4> dag {};
        dag[0].bounds_sphere   = { 0.0F, 0.0F, 0.0F, 1.0F };
        dag[0].self_error      = 0.5F;
        dag[0].parent_error    = 2.0F;
        dag[1].bounds_sphere   = { 0.4F, 0.0F, 0.0F, 0.4F };
        dag[1].self_error      = 0.2F;
        dag[1].parent_error    = 0.5F;
        dag[2].bounds_sphere   = { 0.0F, 0.4F, 0.0F, 0.4F };
        dag[2].self_error      = 0.2F;
        dag[2].parent_error    = 0.5F;
        dag[3].bounds_sphere   = { 0.0F, 0.0F, 0.4F, 0.4F };
        dag[3].self_error      = 0.1F;
        dag[3].parent_error    = 0.2F;
        const cd::math::Vec3f cam_eye { 0.0F, 0.0F, s_vg_cam_z };
        // 60 deg horizontal FOV -> half_fov ~ 0.5236 rad.
        constexpr float half_fov = 0.5236F;
        const auto picks = cd::virtual_geometry::pick_clusters(
            std::span<const cd::virtual_geometry::ClusterNode>(dag),
            s_vg_threshold,
            cam_eye, half_fov,
            static_cast<std::uint32_t>(s_vg_vp_h));
        ImGui::Text("Picked cluster IDs (%zu): %s%s%s%s",
                    picks.size(),
                    picks.size() > 0 ? std::to_string(picks[0]).c_str() : "",
                    picks.size() > 1 ? (", " + std::to_string(picks[1])).c_str() : "",
                    picks.size() > 2 ? (", " + std::to_string(picks[2])).c_str() : "",
                    picks.size() > 3 ? (", " + std::to_string(picks[3])).c_str() : "");
        for (std::size_t i = 0; i < dag.size(); ++i)
        {
            const auto err_px = cd::virtual_geometry::projected_error_pixels(
                cd::math::Vec3f { dag[i].bounds_sphere.x,
                                  dag[i].bounds_sphere.y,
                                  dag[i].bounds_sphere.z },
                dag[i].self_error,
                cam_eye, half_fov,
                static_cast<std::uint32_t>(s_vg_vp_h));
            ImGui::Text("  node[%zu] self_err=%.4f  proj=%.2f px",
                        i,
                        static_cast<double>(dag[i].self_error),
                        static_cast<double>(err_px));
        }
        ImGui::TextDisabled("Karis 2021 Nanite-style LOD picker.");
        ImGui::Separator();
        ImGui::Checkbox("Show LOD frontier in 3D viewport",
                        &fx.vg_show_lod_3d);
        if (fx.vg_show_lod_3d)
        {
            ImGui::TextDisabled("  green = node in picked frontier; grey = refined away");
            ImGui::TextDisabled("  white marker = virtual camera distance (clamped)");
        }
    }
    // phase927-virtual-textures-live-demo (Run 25 Strand B): drive
    // cd::virtual_textures::PageTable in a tight loop. Lets the
    // user request virtual pages by (x, y, mip) and SEE the FIFO
    // eviction behaviour as the atlas fills.
    if (ImGui::CollapsingHeader("Run25  Virtual Textures Probe"))
    {
        // phase1053-3d-viewport-vt-atlas: the PageTable moved into
        // EngineState (render loop owns it; residents() drives the
        // 3D atlas overlay). The panel requests via fx.vt_req +
        // serial bump and reads the mirrored results (1-frame lag).
        ImGui::SliderInt("Request page X", &fx.vt_req[0], 0, 31);
        ImGui::SliderInt("Request page Y", &fx.vt_req[1], 0, 31);
        ImGui::SliderInt("Request page mip", &fx.vt_req[2], 0, 4);
        if (ImGui::Button("Request page"))
        {
            ++fx.vt_req_serial;
        }
        ImGui::Text("Resident pages: %u / 16 (atlas 4x4)",
                    fx.vt_resident_count);
        if (fx.vt_lookup_found)
            ImGui::Text("Requested page resident @ slot (%u, %u)",
                        fx.vt_lookup_slot_x, fx.vt_lookup_slot_y);
        else
            ImGui::TextDisabled("Requested page NOT resident (Request to allocate)");
        ImGui::TextDisabled("Mittring 2008 page-allocator + FIFO eviction.");
        ImGui::Separator();
        ImGui::Checkbox("Show atlas in 3D viewport",
                        &fx.vt_show_atlas_3d);
        if (fx.vt_show_atlas_3d)
        {
            ImGui::TextDisabled("  4x4 grid; filled cell = resident (tint = mip), cross = your page");
        }
    }
    // phase927-mesh-shader-live-demo (Run 25 Strand B): drive
    // cd::mesh_shader::build_meshlets on a synthetic 100-triangle
    // mesh + report the meshlet count + per-meshlet vertex / triangle
    // counts. Same greedy clustering Meshoptimizer starts with.
    if (ImGui::CollapsingHeader("Run25  Mesh Shader Meshlet Builder Probe"))
    {
        // phase1054-3d-viewport-meshlets: tri count migrated onto
        // HelloEngineFx so the 3D cluster view rebuilds with it.
        ImGui::SliderInt("Source triangle count", &fx.ms_tri_count, 8, 500);
        const int s_ms_tri_count = fx.ms_tri_count;
        // Build a synthetic triangle-strip-like flat indexed mesh.
        const auto n = static_cast<std::uint32_t>(s_ms_tri_count);
        std::vector<cd::math::Vec3f> positions(n + 2);
        std::vector<std::uint32_t> indices;
        indices.reserve(static_cast<std::size_t>(n) * 3U);
        for (std::uint32_t i = 0; i < positions.size(); ++i)
            positions[i] = { static_cast<float>(i) * 0.1F,
                             static_cast<float>(i & 1U) * 0.1F,
                             0.0F };
        for (std::uint32_t i = 0; i < n; ++i)
        {
            indices.push_back(i);
            indices.push_back(i + 1U);
            indices.push_back(i + 2U);
        }
        const auto data = cd::mesh_shader::build_meshlets(
            std::span<const std::uint32_t>(indices),
            std::span<const cd::math::Vec3f>(positions));
        ImGui::Text("Meshlets produced: %zu  (cap 64 verts / 124 tris each)",
                    data.meshlets.size());
        for (std::size_t i = 0; i < data.meshlets.size() && i < 8U; ++i)
        {
            ImGui::Text("  meshlet[%zu]: %u verts, %u tris",
                        i,
                        data.meshlets[i].vertex_count,
                        data.meshlets[i].triangle_count);
        }
        ImGui::TextDisabled("NVIDIA 2018 / Karis 2021 Nanite leaf granularity.");
        ImGui::Separator();
        ImGui::Checkbox("Show meshlet clusters in 3D viewport",
                        &fx.ms_show_meshlets_3d);
        if (fx.ms_show_meshlets_3d)
        {
            ImGui::TextDisabled("  strip triangles edge-drawn, colour = meshlet id (Nanite view)");
        }
    }
    // phase929-brdf-sheen-clearcoat-live-demo (Run 25 Strand B):
    // drive cd::brdf::sheen_clearcoat::charlie_d + v_neubelt +
    // clearcoat_d_v on a swept n.h angle. Lets the user see the
    // distribution + visibility curves respond to the roughness
    // slider live -- same math the production PBR fragment shader
    // runs per pixel.
    if (ImGui::CollapsingHeader("Run25  Sheen + Clearcoat BRDF Probe"))
    {
        // phase1039-3d-viewport-brdf-lobes: state migrated onto
        // HelloEngineFx so the 3D lobes reshape with the SAME values.
        ImGui::SliderFloat("Roughness", &fx.sc_roughness, 0.0F, 1.0F);
        ImGui::SliderFloat("n . v",     &fx.sc_nv, 0.0F, 1.0F);
        ImGui::SliderFloat("n . l",     &fx.sc_nl, 0.0F, 1.0F);
        const float s_sc_roughness = fx.sc_roughness;
        const float s_sc_nv = fx.sc_nv;
        const float s_sc_nl = fx.sc_nl;
        constexpr int kSamples = 128;
        std::array<float, kSamples> charlie    {};
        std::array<float, kSamples> clearcoat  {};
        for (int i = 0; i < kSamples; ++i)
        {
            const float nh = static_cast<float>(i) /
                             static_cast<float>(kSamples - 1);
            charlie[static_cast<std::size_t>(i)] =
                cd::brdf::sheen_clearcoat::charlie_d(s_sc_roughness, nh);
            clearcoat[static_cast<std::size_t>(i)] =
                cd::brdf::sheen_clearcoat::clearcoat_d_v(
                    s_sc_roughness, nh, s_sc_nv, s_sc_nl);
        }
        const float v_neubelt =
            cd::brdf::sheen_clearcoat::v_neubelt(s_sc_nv, s_sc_nl);
        ImGui::Text("Neubelt visibility (sheen): %.4f",
                    static_cast<double>(v_neubelt));
        ImGui::PlotLines(
            "##sc_charlie",
            charlie.data(), kSamples, 0,
            "Charlie D vs n.h",
            0.0F,
            *std::ranges::max_element(charlie) * 1.1F + 1e-4F,
            ImVec2(0, 56));
        ImGui::PlotLines(
            "##sc_clearcoat",
            clearcoat.data(), kSamples, 0,
            "Clearcoat D*V vs n.h",
            0.0F,
            *std::ranges::max_element(clearcoat) * 1.1F + 1e-4F,
            ImVec2(0, 56));
        ImGui::TextDisabled("Estevez 2017 (Charlie sheen) + Filament clearcoat.");
        ImGui::Separator();
        ImGui::Checkbox("Show BRDF lobes in 3D viewport",
                        &fx.sc_show_lobes_3d);
        if (fx.sc_show_lobes_3d)
        {
            ImGui::TextDisabled("  warm lobe = Charlie sheen D, cool = clearcoat D*V");
            ImGui::TextDisabled("  grey base = surface plane, vertical = normal");
        }
    }
    // phase929-brdf-sss-live-demo (Run 25 Strand B): drive
    // cd::brdf::sss diffusion profiles. Plots the per-channel falloff
    // curve so the user sees Burley 2015 + Christensen-Burley R(r)
    // respond to the mean-free-path slider.
    if (ImGui::CollapsingHeader("Run25  SSS BRDF Probe"))
    {
        // phase1040-3d-viewport-sss-falloff: mfp migrated onto
        // HelloEngineFx so the 3D curves reshape with the SAME values.
        ImGui::ColorEdit3("Mean free path (R/G/B)", fx.sss_mfp.data());
        const cd::math::Vec3f s_sss_mfp {
            fx.sss_mfp[0], fx.sss_mfp[1], fx.sss_mfp[2] };
        constexpr int kRadii = 128;
        std::array<float, kRadii> falloff_r {};
        std::array<float, kRadii> falloff_g {};
        std::array<float, kRadii> falloff_b {};
        for (int i = 0; i < kRadii; ++i)
        {
            const float r = static_cast<float>(i) /
                            static_cast<float>(kRadii - 1) * 4.0F;  // mm
            // Burley-style normalised diffusion R(r) = (exp(-r/3d) +
            // exp(-r/d)) / (8 pi d r); we plot the unnormalised
            // exponential pair for visibility.
            const float dr = std::max(s_sss_mfp.x, 1e-3F);
            const float dg = std::max(s_sss_mfp.y, 1e-3F);
            const float db = std::max(s_sss_mfp.z, 1e-3F);
            falloff_r[static_cast<std::size_t>(i)] =
                0.25F * (std::exp(-r / (3.0F * dr)) + std::exp(-r / dr));
            falloff_g[static_cast<std::size_t>(i)] =
                0.25F * (std::exp(-r / (3.0F * dg)) + std::exp(-r / dg));
            falloff_b[static_cast<std::size_t>(i)] =
                0.25F * (std::exp(-r / (3.0F * db)) + std::exp(-r / db));
        }
        ImGui::PlotLines(
            "##sss_r",
            falloff_r.data(), kRadii, 0,
            "Burley diffusion R falloff (mm)",
            0.0F, 0.6F,
            ImVec2(0, 48));
        ImGui::PlotLines(
            "##sss_g",
            falloff_g.data(), kRadii, 0,
            "Burley diffusion G falloff",
            0.0F, 0.6F,
            ImVec2(0, 48));
        ImGui::PlotLines(
            "##sss_b",
            falloff_b.data(), kRadii, 0,
            "Burley diffusion B falloff",
            0.0F, 0.6F,
            ImVec2(0, 48));
        ImGui::TextDisabled("Burley 2015 + Christensen-Burley dipole approximation.");
        ImGui::Separator();
        ImGui::Checkbox("Show SSS falloff in 3D viewport",
                        &fx.sss_show_falloff_3d);
        if (fx.sss_show_falloff_3d)
        {
            ImGui::TextDisabled("  R/G/B curves on one baseline; red bleeds farthest = skin glow");
        }
    }
    // phase930-atmosphere-phase-fns-live-demo (Run 25 Strand B):
    // drive cd::atmosphere::henyey_greenstein + rayleigh_phase.
    // Lets the user see the forward / backward scatter response
    // of Mie (HG) vs Rayleigh as they drag the asymmetry slider.
    if (ImGui::CollapsingHeader("Run25  Atmosphere Phase Functions Probe"))
    {
        // phase1035-3d-viewport-phase-function-polar: g migrated onto
        // HelloEngineFx so the 3D polar lobes reshape with the SAME g.
        ImGui::SliderFloat("Mie asymmetry g", &fx.atmo_phase_g, -0.95F, 0.95F);
        const float s_atm_g = fx.atmo_phase_g;
        constexpr int kAngles = 180;
        std::array<float, kAngles> hg {};
        std::array<float, kAngles> ray {};
        for (int i = 0; i < kAngles; ++i)
        {
            const float cos_t = -1.0F + 2.0F * static_cast<float>(i) /
                                          static_cast<float>(kAngles - 1);
            hg[static_cast<std::size_t>(i)] =
                cd::atmosphere::henyey_greenstein(cos_t, s_atm_g);
            ray[static_cast<std::size_t>(i)] =
                cd::atmosphere::rayleigh_phase(cos_t);
        }
        ImGui::PlotLines(
            "##atm_hg",
            hg.data(), kAngles, 0,
            "Henyey-Greenstein phase (cos_theta = -1..1)",
            0.0F,
            *std::ranges::max_element(hg) * 1.1F + 1e-4F,
            ImVec2(0, 56));
        ImGui::PlotLines(
            "##atm_ray",
            ray.data(), kAngles, 0,
            "Rayleigh phase (cos_theta = -1..1)",
            0.0F,
            *std::ranges::max_element(ray) * 1.1F + 1e-4F,
            ImVec2(0, 56));
        ImGui::TextDisabled("Hillaire 2020 + Bruneton 2008 production atmo.");
        ImGui::Separator();
        ImGui::Checkbox("Show phase-function polar plot in 3D viewport",
                        &fx.atmo_show_polar_3d);
        if (fx.atmo_show_polar_3d)
        {
            ImGui::TextDisabled("  warm lobe = HG(g), cool = Rayleigh, grey ring = isotropic ref");
            ImGui::TextDisabled("  +X = forward scatter direction (light travel)");
        }
    }
    // phase930-light-shafts-live-demo (Run 25 Strand B): drive
    // cd::light_shafts::compute_inline. CPU helper that mirrors
    // the inline GLSL cone-alignment fallback used when shadow-
    // map ray-march budget is tight. Plots intensity vs azimuth
    // around the sun axis.
    if (ImGui::CollapsingHeader("Run25  Light Shafts Inline Probe"))
    {
        // phase1041-3d-viewport-shafts-ring: cam dir migrated onto
        // HelloEngineFx so the 3D ring reshapes with the SAME values.
        ImGui::SliderFloat("Cam dir x", fx.shafts_cam_dir.data(), -1.0F, 1.0F);
        ImGui::SliderFloat("Cam dir y", &fx.shafts_cam_dir[1], -1.0F, 1.0F);
        ImGui::SliderFloat("Cam dir z", &fx.shafts_cam_dir[2], -1.0F, 1.0F);
        const float s_ls_cam_dir_x = fx.shafts_cam_dir[0];
        const float s_ls_cam_dir_y = fx.shafts_cam_dir[1];
        const float s_ls_cam_dir_z = fx.shafts_cam_dir[2];
        const cd::math::Vec3f sun_L { 0.0F, 1.0F, 0.0F };
        // Sweep around the sun, plotting the inline intensity.
        constexpr int kSamples = 128;
        std::array<float, kSamples> radial {};
        for (int i = 0; i < kSamples; ++i)
        {
            const float theta = static_cast<float>(i) /
                                static_cast<float>(kSamples - 1) *
                                std::numbers::pi_v<float> * 2.0F;
            // Sweep a candidate cam-to-pixel direction around the
            // axis of sun_L. Direct two-coefficient computation of
            // the HG fall-off without invoking the GLSL helper, so
            // the demo doesn't depend on the (compute-shader-only)
            // light_shafts_inline_cone glsl-string. Mirrors the
            // GLSL fragment fallback path.
            const float cam_x = s_ls_cam_dir_x + std::cos(theta) * 0.3F;
            const float cam_y = s_ls_cam_dir_y + std::sin(theta) * 0.3F;
            const float cam_z = s_ls_cam_dir_z;
            const float inv_len = 1.0F / std::max(
                std::sqrt(cam_x * cam_x + cam_y * cam_y + cam_z * cam_z),
                1e-3F);
            const float dx = cam_x * inv_len;
            const float dy = cam_y * inv_len;
            const float dz = cam_z * inv_len;
            const float cos_t = -(dx * sun_L.x + dy * sun_L.y + dz * sun_L.z);
            radial[static_cast<std::size_t>(i)] = std::max(0.0F, cos_t);
        }
        ImGui::PlotLines(
            "##ls_radial",
            radial.data(), kSamples, 0,
            "Inline cone alignment around sun axis",
            0.0F, 1.0F,
            ImVec2(0, 64));
        ImGui::TextDisabled("Inline fragment-shader cone-alignment fallback.");
        ImGui::Separator();
        ImGui::Checkbox("Show alignment ring in 3D viewport",
                        &fx.shafts_show_ring_3d);
        if (fx.shafts_show_ring_3d)
        {
            ImGui::TextDisabled("  polar ring around sun axis; bulge = max shaft intensity azimuth");
        }
    }
    // phase931-ltc-area-light-live-demo (Run 25 Strand B): drive
    // cd::brdf::ltc::polygon_irradiance against a unit square area
    // light. Lets the user drag roughness + n.v + light-size + light
    // distance and watch the analytic irradiance respond live --
    // Heitz 2016 polygon LTC closed form, the same math the
    // PBR fragment shader runs per pixel for area lights.
    if (ImGui::CollapsingHeader("Run25  LTC Area Light Probe"))
    {
        static float s_ltc_roughness = 0.4F;
        static float s_ltc_nv = 0.85F;
        static float s_ltc_half = 1.0F;
        static float s_ltc_dist = 2.0F;
        ImGui::SliderFloat("Roughness", &s_ltc_roughness, 0.01F, 1.0F);
        ImGui::SliderFloat("n . v",     &s_ltc_nv, 0.05F, 1.0F);
        ImGui::SliderFloat("Light half-extent",
                           &s_ltc_half, 0.1F, 4.0F);
        ImGui::SliderFloat("Light distance Z", &s_ltc_dist, 0.5F, 10.0F);
        const auto m = cd::brdf::ltc::ltc_inverse_matrix(s_ltc_roughness, s_ltc_nv);
        // Square light at +Z facing the shading point.
        const float h = s_ltc_half;
        const float z = s_ltc_dist;
        std::array<cd::math::Vec3f, 4> corners {{
            { -h,  h, z }, {  h,  h, z }, {  h, -h, z }, { -h, -h, z }
        }};
        // Normalise vertices to unit sphere directions (tangent-space
        // shading-point at origin, +Z surface normal).
        for (auto& c : corners)
        {
            const float ln = 1.0F / std::max(
                std::sqrt(c.x * c.x + c.y * c.y + c.z * c.z), 1e-3F);
            c = { c.x * ln, c.y * ln, c.z * ln };
        }
        const float irr =
            cd::brdf::ltc::polygon_irradiance(corners, m);
        ImGui::Text("LTC matrix (sparse abcd): (%.3f, %.3f, %.3f, %.3f)",
                    static_cast<double>(m.a),
                    static_cast<double>(m.b),
                    static_cast<double>(m.c),
                    static_cast<double>(m.d));
        ImGui::Text("Polygon irradiance: %.4f", static_cast<double>(irr));
        // Sweep roughness for the same light to give a curve.
        constexpr int kSweep = 64;
        std::array<float, kSweep> sweep {};
        for (int i = 0; i < kSweep; ++i)
        {
            const float r = static_cast<float>(i) /
                            static_cast<float>(kSweep - 1);
            const auto mr = cd::brdf::ltc::ltc_inverse_matrix(r, s_ltc_nv);
            sweep[static_cast<std::size_t>(i)] =
                cd::brdf::ltc::polygon_irradiance(corners, mr);
        }
        ImGui::PlotLines(
            "##ltc_sweep",
            sweep.data(), kSweep, 0,
            "Irradiance vs roughness [0..1]",
            0.0F,
            *std::ranges::max_element(sweep) * 1.1F + 1e-4F,
            ImVec2(0, 56));
        ImGui::TextDisabled("Heitz 2016 LTC closed-form polygon irradiance.");
    }
    // phase932-restir-gi-live-demo (Run 25 Strand B): drive
    // cd::restir_gi::Reservoir + update. Streams synthetic indirect
    // bounce samples (point + normal + incoming radiance) through
    // a per-pixel reservoir + reports the survivor + final weight
    // live. Same shape as the DI reservoir (phase 920) but the
    // survivor carries world-space geometry instead of a light id
    // -- Ouyang 2021 ReSTIR GI.
    if (ImGui::CollapsingHeader("Run25  ReSTIR GI Live Demo"))
    {
        static int s_rgi_samples = 32;
        static std::uint32_t s_rgi_seed = 0xC1DDF1U;
        ImGui::SliderInt("Candidate bounces / pixel",
                         &s_rgi_samples, 1, 128);
        ImGui::InputScalar("Seed (PCG32)",
                           ImGuiDataType_U32, &s_rgi_seed);
        cd::restir_gi::Reservoir res {};
        std::uint32_t rng = s_rgi_seed;
        auto next_u32 = [&rng]() noexcept -> std::uint32_t
        {
            rng = rng * 1664525U + 1013904223U;
            return rng;
        };
        auto next_unit = [&next_u32]() noexcept -> float
        {
            return static_cast<float>(next_u32() & 0xFFFFFFU)
                 / static_cast<float>(0xFFFFFFU);
        };
        for (int i = 0; i < s_rgi_samples; ++i)
        {
            cd::restir_gi::Sample s {};
            // Synthetic bounce: point on a small jittered sphere around the
            // origin, normal toward camera, incoming colour wave drifting
            // toward warm.
            const float a = next_unit() * std::numbers::pi_v<float> * 2.0F;
            const float r = next_unit() * 1.5F;
            s.point  = { std::cos(a) * r, std::sin(a) * r, 1.0F + next_unit() };
            s.normal = { 0.0F, 0.0F, 1.0F };
            s.incoming = {
                0.4F + next_unit() * 0.6F,
                0.3F + next_unit() * 0.6F,
                0.1F + next_unit() * 0.4F };
            // target_pdf ~ incoming luminance (max channel proxy).
            const float lum = std::max({ s.incoming.x, s.incoming.y, s.incoming.z });
            cd::restir_gi::update(res, s, lum, next_unit());
        }
        ImGui::Text("Reservoir.M (samples streamed): %u", res.M);
        ImGui::Text("Reservoir.weight_sum: %.3f",
                    static_cast<double>(res.weight_sum));
        const float fw = res.final_weight(std::max(
            { res.selected.incoming.x,
              res.selected.incoming.y,
              res.selected.incoming.z }));
        ImGui::Text("Survivor point: (%.3f, %.3f, %.3f)",
                    static_cast<double>(res.selected.point.x),
                    static_cast<double>(res.selected.point.y),
                    static_cast<double>(res.selected.point.z));
        ImGui::Text("Survivor incoming RGB: (%.3f, %.3f, %.3f)",
                    static_cast<double>(res.selected.incoming.x),
                    static_cast<double>(res.selected.incoming.y),
                    static_cast<double>(res.selected.incoming.z));
        ImGui::Text("Survivor final_weight: %.4f",
                    static_cast<double>(fw));
        ImGui::ColorButton("Survivor radiance",
                           { res.selected.incoming.x,
                             res.selected.incoming.y,
                             res.selected.incoming.z, 1.0F },
                           ImGuiColorEditFlags_NoAlpha, ImVec2(64, 24));
        ImGui::TextDisabled("Ouyang 2021 ReSTIR GI WRS streaming.");
    }
    // phase934-ibl-brdf-lut-live-demo (Run 25 Strand B): drive
    // cd::ibl::bake_brdf_lut on a button press at a tiny working
    // size (32x32 with 64 samples ~ 1 ms) so the user can drag a
    // (n_dot_v, roughness) marker over the baked LUT and watch
    // the (scale, bias) split-sum coefficients respond live. Same
    // split-sum tap the production fragment shader does on the
    // 256x256 baked LUT.
    if (ImGui::CollapsingHeader("Run25  IBL BRDF Split-Sum LUT Probe"))
    {
        static cd::ibl::BrdfLut s_brdf_lut {};
        const float s_brdf_nv = fx.brdf_lut_nv;
        const float s_brdf_r  = fx.brdf_lut_r;
        if (ImGui::Button("Bake 32x32 x 64 samples (~1 ms)"))
        {
            s_brdf_lut = cd::ibl::bake_brdf_lut(32, 32, 64);
        }
        ImGui::SameLine();
        if (ImGui::Button("Bake 256x256 x 1024 (~50 ms)"))
        {
            s_brdf_lut = cd::ibl::bake_brdf_lut(256, 256, 1024);
        }
        if (s_brdf_lut.width == 0)
        {
            ImGui::TextDisabled("(Click Bake to fill the LUT)");
        }
        else
        {
            ImGui::Text("LUT size: %u x %u  (%zu floats, RG packed)",
                        s_brdf_lut.width, s_brdf_lut.height, s_brdf_lut.rg.size());
            // phase1038-3d-viewport-brdf-lut-surface: lookup point
            // migrated onto HelloEngineFx so the 3D marker rides it.
            ImGui::SliderFloat("Lookup n.v",       &fx.brdf_lut_nv, 0.001F, 1.0F);
            ImGui::SliderFloat("Lookup roughness", &fx.brdf_lut_r,  0.001F, 1.0F);
            const auto px = static_cast<std::uint32_t>(
                std::clamp(s_brdf_nv * static_cast<float>(s_brdf_lut.width),
                           0.0F,
                           static_cast<float>(s_brdf_lut.width - 1U)));
            const auto py = static_cast<std::uint32_t>(
                std::clamp(s_brdf_r * static_cast<float>(s_brdf_lut.height),
                           0.0F,
                           static_cast<float>(s_brdf_lut.height - 1U)));
            const std::size_t off =
                (static_cast<std::size_t>(py) * s_brdf_lut.width + px) * 2U;
            const float scale = s_brdf_lut.rg[off + 0U];
            const float bias  = s_brdf_lut.rg[off + 1U];
            ImGui::Text("LUT @ (%u, %u): scale=%.4f  bias=%.4f",
                        px, py,
                        static_cast<double>(scale),
                        static_cast<double>(bias));
            ImGui::TextDisabled("Karis split-sum: specular = F0*scale + bias.");
        }
        ImGui::Separator();
        ImGui::Checkbox("Show LUT surface in 3D viewport",
                        &fx.brdf_show_lut_3d);
        if (fx.brdf_show_lut_3d)
        {
            ImGui::TextDisabled("  wireframe terrain: x=n.v, z=roughness, height=scale");
            ImGui::TextDisabled("  warm cross = your lookup point");
        }
    }
    // phase934-camera-basis-live-demo (Run 25 Strand B): drive
    // cd::camera::Camera + look_at against a yaw/pitch slider pair.
    // Reports view-matrix orthonormal basis so the user can SEE the
    // forward/right/up vectors rotate as they drag. Same look_at
    // path the engine FreeLookController uses each frame.
    if (ImGui::CollapsingHeader("Run25  Camera Basis Probe"))
    {
        // phase1027-3d-viewport-camera-basis: state migrated onto
        // HelloEngineFx so the 3D triad swings with the SAME values.
        ImGui::SliderFloat("Yaw (deg)",   &fx.camera_basis_yaw_deg,   -180.0F, 180.0F);
        ImGui::SliderFloat("Pitch (deg)", &fx.camera_basis_pitch_deg, -89.0F, 89.0F);
        ImGui::SliderFloat3("Position",   fx.camera_basis_pos.data(), -10.0F, 10.0F);
        const cd::math::Vec3f s_cam_pos {
            fx.camera_basis_pos[0],
            fx.camera_basis_pos[1],
            fx.camera_basis_pos[2] };
        const float yaw_r   = fx.camera_basis_yaw_deg   * std::numbers::pi_v<float> / 180.0F;
        const float pitch_r = fx.camera_basis_pitch_deg * std::numbers::pi_v<float> / 180.0F;
        const cd::math::Vec3f forward {
            std::cos(pitch_r) * std::sin(yaw_r),
            std::sin(pitch_r),
           -std::cos(pitch_r) * std::cos(yaw_r) };
        const cd::math::Vec3f world_up { 0.0F, 1.0F, 0.0F };
        // right = normalize(cross(forward, world_up))
        cd::math::Vec3f right {
            forward.y * world_up.z - forward.z * world_up.y,
            forward.z * world_up.x - forward.x * world_up.z,
            forward.x * world_up.y - forward.y * world_up.x };
        const float rln = 1.0F / std::max(
            std::sqrt(right.x * right.x + right.y * right.y + right.z * right.z),
            1e-3F);
        right = { right.x * rln, right.y * rln, right.z * rln };
        // up = cross(right, forward)
        const cd::math::Vec3f up {
            right.y * forward.z - right.z * forward.y,
            right.z * forward.x - right.x * forward.z,
            right.x * forward.y - right.y * forward.x };
        ImGui::Text("Forward: (%.3f, %.3f, %.3f)",
                    static_cast<double>(forward.x),
                    static_cast<double>(forward.y),
                    static_cast<double>(forward.z));
        ImGui::Text("Right  : (%.3f, %.3f, %.3f)",
                    static_cast<double>(right.x),
                    static_cast<double>(right.y),
                    static_cast<double>(right.z));
        ImGui::Text("Up     : (%.3f, %.3f, %.3f)",
                    static_cast<double>(up.x),
                    static_cast<double>(up.y),
                    static_cast<double>(up.z));
        ImGui::Text("Cam pos: (%.3f, %.3f, %.3f)",
                    static_cast<double>(s_cam_pos.x),
                    static_cast<double>(s_cam_pos.y),
                    static_cast<double>(s_cam_pos.z));
        ImGui::TextDisabled("Free-look basis identical to FreeLookController.");
        ImGui::Separator();
        ImGui::Checkbox("Show camera basis in 3D viewport",
                        &fx.camera_basis_show_3d);
        if (fx.camera_basis_show_3d)
        {
            ImGui::TextDisabled("  triad at probe position: red=right, green=up, blue=forward");
        }
    }
    // phase935-frustum-cull-live-demo (Run 25 Strand B): drive
    // cd::camera::extract_frustum + test_aabb against a sliding
    // AABB cluster. Lets the user drag the camera + cluster
    // separately and SEE the 3-valued cull result + per-cluster
    // tally live -- same path the production scene_ingest cull
    // pass runs per draw bucket.
    if (ImGui::CollapsingHeader("Run25  Frustum Cull Probe"))
    {
        static cd::math::Vec3f s_fc_cam_pos { 0.0F, 1.5F, 6.0F };
        static cd::math::Vec3f s_fc_cluster_centre { 0.0F, 0.0F, 0.0F };
        static float s_fc_cluster_extent = 0.5F;
        static float s_fc_fov_deg = 60.0F;
        ImGui::SliderFloat3("Camera pos",      &s_fc_cam_pos.x, -10.0F, 10.0F);
        ImGui::SliderFloat3("Cluster centre",  &s_fc_cluster_centre.x, -10.0F, 10.0F);
        ImGui::SliderFloat("Cluster extent",   &s_fc_cluster_extent, 0.05F, 4.0F);
        ImGui::SliderFloat("Camera FOV (deg)", &s_fc_fov_deg, 10.0F, 120.0F);
        cd::camera::Camera cam {};
        cam.eye    = s_fc_cam_pos;
        cam.target = { 0.0F, 0.0F, 0.0F };
        cam.up     = { 0.0F, 1.0F, 0.0F };
        cam.fov_y  = s_fc_fov_deg * std::numbers::pi_v<float> / 180.0F;
        cam.near_z = 0.1F;
        cam.far_z  = 100.0F;
        const auto frustum = cd::camera::extract_frustum(cam, 16.0F / 9.0F);
        // Build a 3x3 cluster grid centred on s_fc_cluster_centre +
        // count outside/intersect/inside results.
        std::uint32_t outside = 0;
        std::uint32_t intersect = 0;
        std::uint32_t inside = 0;
        for (int gz = -1; gz <= 1; ++gz)
        {
            for (int gy = -1; gy <= 1; ++gy)
            {
                for (int gx = -1; gx <= 1; ++gx)
                {
                    const cd::math::Vec3f centre {
                        s_fc_cluster_centre.x + static_cast<float>(gx) * 2.0F,
                        s_fc_cluster_centre.y + static_cast<float>(gy) * 2.0F,
                        s_fc_cluster_centre.z + static_cast<float>(gz) * 2.0F };
                    const cd::math::Vec3f mn {
                        centre.x - s_fc_cluster_extent,
                        centre.y - s_fc_cluster_extent,
                        centre.z - s_fc_cluster_extent };
                    const cd::math::Vec3f mx {
                        centre.x + s_fc_cluster_extent,
                        centre.y + s_fc_cluster_extent,
                        centre.z + s_fc_cluster_extent };
                    const auto r = cd::camera::test_aabb(frustum, mn, mx);
                    switch (r)
                    {
                        case cd::camera::CullResult::kOutside:      ++outside; break;
                        case cd::camera::CullResult::kIntersecting: ++intersect; break;
                        case cd::camera::CullResult::kInside:       ++inside; break;
                    }
                }
            }
        }
        ImGui::Text("3x3x3 cluster tally (27 total):");
        ImGui::BulletText("kOutside     : %u", outside);
        ImGui::BulletText("kIntersecting: %u", intersect);
        ImGui::BulletText("kInside      : %u", inside);
        ImGui::TextDisabled("p-vertex / n-vertex two-corner cull (Akenine 2018).");
    }
    // phase937-light-cct-live-demo (Run 25 Strand B): drive
    // cd::light::cct_to_linear_rgb across the 1000..15000 K range.
    // Lets the user drag the CCT slider + see the linear-sRGB
    // colour respond live, with the canonical presets called out
    // (tungsten / daylight / overcast).
    if (ImGui::CollapsingHeader("Run25  Light CCT Probe"))
    {
        // phase1028-3d-viewport-cct-sweep: kelvin migrated onto
        // HelloEngineFx so the 3D rail marker rides the SAME value.
        ImGui::SliderFloat("CCT (Kelvin)", &fx.cct_kelvin, 1000.0F, 15000.0F, "%.0f K");
        const auto rgb = cd::light::cct_to_linear_rgb(fx.cct_kelvin);
        ImGui::ColorButton("CCT colour",
                           { rgb.x, rgb.y, rgb.z, 1.0F },
                           ImGuiColorEditFlags_NoAlpha, ImVec2(72, 24));
        ImGui::SameLine();
        ImGui::Text("RGB linear: (%.3f, %.3f, %.3f)",
                    static_cast<double>(rgb.x),
                    static_cast<double>(rgb.y),
                    static_cast<double>(rgb.z));
        // Sweep CCT across the artist range so the user sees the
        // gradient strip.
        constexpr int kSwatches = 16;
        for (int i = 0; i < kSwatches; ++i)
        {
            const float t = static_cast<float>(i) /
                            static_cast<float>(kSwatches - 1);
            const float k = 1500.0F + t * (15000.0F - 1500.0F);
            const auto sw = cd::light::cct_to_linear_rgb(k);
            char id[24] {};
            std::snprintf(id, sizeof(id), "##sw_%d", i);
            ImGui::ColorButton(id,
                               { sw.x, sw.y, sw.z, 1.0F },
                               ImGuiColorEditFlags_NoAlpha, ImVec2(24, 16));
            if ((i % kSwatches) != (kSwatches - 1))
                ImGui::SameLine();
        }
        ImGui::TextDisabled("1500K (firelight) -> 15000K (blue sky shade).");
        ImGui::TextDisabled("Krystek 1985 + Bruce Lindbloom XYZ -> sRGB.");
        ImGui::Separator();
        ImGui::Checkbox("Show CCT sweep in 3D viewport",
                        &fx.cct_show_sweep_3d);
        if (fx.cct_show_sweep_3d)
        {
            ImGui::TextDisabled("  16-sphere rail 1500K->15000K; big sphere = your Kelvin");
        }
    }
    // phase938-light-attenuation-live-demo (Run 25 Strand B): drive
    // cd::light::distance_attenuation + cone_attenuation. Plots the
    // Frostbite windowed inverse-square distance falloff + the
    // smoothstep-squared cone falloff so the user can see how range
    // / inner-half-angle / outer-half-angle reshape light response
    // live.
    if (ImGui::CollapsingHeader("Run25  Light Attenuation Probe"))
    {
        // phase1029-3d-viewport-attenuation-rail: range migrated onto
        // HelloEngineFx so the 3D rail stretches with the SAME value.
        static float s_la_inner_deg = 15.0F;
        static float s_la_outer_deg = 30.0F;
        static float s_la_lumens = 1500.0F;
        float& s_la_range = fx.atten_range;
        ImGui::SliderFloat("Range (m)",
                           &s_la_range, 0.5F, 32.0F, "%.2f");
        ImGui::SliderFloat("Spot inner half-angle (deg)",
                           &s_la_inner_deg, 1.0F, 60.0F);
        ImGui::SliderFloat("Spot outer half-angle (deg)",
                           &s_la_outer_deg, 5.0F, 89.0F);
        ImGui::SliderFloat("Luminous flux (lumens)",
                           &s_la_lumens, 0.0F, 5000.0F);
        constexpr int kDistSamples = 128;
        std::array<float, kDistSamples> dist_fall {};
        for (int i = 0; i < kDistSamples; ++i)
        {
            const float d = static_cast<float>(i) /
                            static_cast<float>(kDistSamples - 1) *
                            (s_la_range * 1.5F);
            dist_fall[static_cast<std::size_t>(i)] =
                cd::light::distance_attenuation(d, s_la_range);
        }
        const float cos_in  = std::cos(s_la_inner_deg *
                                       std::numbers::pi_v<float> / 180.0F);
        const float cos_out = std::cos(s_la_outer_deg *
                                       std::numbers::pi_v<float> / 180.0F);
        constexpr int kConeSamples = 128;
        std::array<float, kConeSamples> cone_fall {};
        for (int i = 0; i < kConeSamples; ++i)
        {
            const float ang = static_cast<float>(i) /
                              static_cast<float>(kConeSamples - 1) * 90.0F;
            const float c = std::cos(ang *
                                     std::numbers::pi_v<float> / 180.0F);
            cone_fall[static_cast<std::size_t>(i)] =
                cd::light::cone_attenuation(c, cos_in, cos_out);
        }
        const float point_i = cd::light::lumens_to_point_intensity(s_la_lumens);
        const float spot_i  = cd::light::lumens_to_spot_intensity(s_la_lumens, cos_out);
        ImGui::Text("Point intensity:  %.3f cd  (Phi / 4 pi)",
                    static_cast<double>(point_i));
        ImGui::Text("Spot intensity:   %.3f cd  (Phi / 2 pi (1 - cos_outer))",
                    static_cast<double>(spot_i));
        ImGui::PlotLines(
            "##la_dist",
            dist_fall.data(), kDistSamples, 0,
            "Distance attenuation (0..1.5x range)",
            0.0F,
            *std::ranges::max_element(dist_fall) * 1.1F + 1e-4F,
            ImVec2(0, 56));
        ImGui::PlotLines(
            "##la_cone",
            cone_fall.data(), kConeSamples, 0,
            "Cone attenuation (0..90 deg)",
            0.0F, 1.0F,
            ImVec2(0, 56));
        ImGui::TextDisabled("Frostbite 2014 windowed inverse-square + smoothstep-squared cone.");
        ImGui::Separator();
        ImGui::Checkbox("Show attenuation rail in 3D viewport",
                        &fx.atten_show_rail_3d);
        if (fx.atten_show_rail_3d)
        {
            ImGui::TextDisabled("  warm light marker + 20 spheres; brightness = falloff at distance");
        }
    }
    // phase939-ibl-cubemap-sample-live-demo (Run 25 Strand B): drive
    // cd::ibl::bake_sky_cube + sample_cubemap_dir against a tiny
    // (16-pixel face) synthetic sky. Lets the user drag a sample
    // direction + watch the sampled RGB respond live -- proves the
    // CPU cubemap addressing + bilinear path that the IBL bake
    // pipeline depends on.
    if (ImGui::CollapsingHeader("Run25  IBL Cubemap Sample Probe"))
    {
        static cd::ibl::CubeMapRgbF s_sky_cm = []() {
            // Bake a tiny analytical sky once per program: blue zenith,
            // warm horizon. Same shape as the engine's procedural-sky
            // fallback.
            return cd::ibl::bake_sky_cube(16, [](cd::math::Vec3f d) -> cd::math::Vec3f {
                const float ln = 1.0F / std::max(std::sqrt(
                    d.x * d.x + d.y * d.y + d.z * d.z), 1e-3F);
                const cd::math::Vec3f n { d.x * ln, d.y * ln, d.z * ln };
                const float t = std::clamp(n.y * 0.5F + 0.5F, 0.0F, 1.0F);
                cd::math::Vec3f horizon { 0.95F, 0.65F, 0.40F };
                cd::math::Vec3f zenith  { 0.30F, 0.55F, 0.95F };
                return { horizon.x + (zenith.x - horizon.x) * t,
                         horizon.y + (zenith.y - horizon.y) * t,
                         horizon.z + (zenith.z - horizon.z) * t };
            });
        }();
        // phase1022-3d-viewport-cubemap-globe: dir migrated onto
        // HelloEngineFx so the 3D overlay highlights the SAME
        // direction the user drags here.
        ImGui::SliderFloat("Sample dir x", fx.cubemap_sample_dir.data(), -1.0F, 1.0F);
        ImGui::SliderFloat("Sample dir y", &fx.cubemap_sample_dir[1], -1.0F, 1.0F);
        ImGui::SliderFloat("Sample dir z", &fx.cubemap_sample_dir[2], -1.0F, 1.0F);
        const cd::math::Vec3f s_cm_dir {
            fx.cubemap_sample_dir[0],
            fx.cubemap_sample_dir[1],
            fx.cubemap_sample_dir[2] };
        const auto sample =
            cd::ibl::sample_cubemap_dir(s_sky_cm, s_cm_dir);
        ImGui::ColorButton("Sky sample",
                           { sample.x, sample.y, sample.z, 1.0F },
                           ImGuiColorEditFlags_NoAlpha, ImVec2(72, 24));
        ImGui::SameLine();
        ImGui::Text("RGB: (%.3f, %.3f, %.3f)",
                    static_cast<double>(sample.x),
                    static_cast<double>(sample.y),
                    static_cast<double>(sample.z));
        ImGui::Text("Cube face_size: %u  (16 face_size = 6 faces x 256 texels)",
                    s_sky_cm.face_size);
        ImGui::TextDisabled("bake_sky_cube + sample_cubemap_dir CPU path.");
        ImGui::TextDisabled("Production IBL prefilter convolves this for GGX lobes.");
        ImGui::Separator();
        ImGui::Checkbox("Show cubemap globe in 3D viewport",
                        &fx.cubemap_show_globe_3d);
        if (fx.cubemap_show_globe_3d)
        {
            ImGui::TextDisabled("  ~50-sphere globe tinted by cubemap; big sphere = your sample dir");
        }
    }
    // phase942-csm-split-live-demo (Run 25 Strand B): drive
    // cd::light::practical_split_distances against the four-cascade
    // CSM default. Lets the user drag near + far + lambda and see
    // the per-cascade split distances respond live -- same Practical
    // Split Scheme Doom Eternal uses (Zhang et al. 2006).
    if (ImGui::CollapsingHeader("Run25  CSM Split Distances Probe"))
    {
        static float s_csm_near = 0.1F;
        static float s_csm_far  = 100.0F;
        static float s_csm_lambda = 0.75F;
        static int   s_csm_cascades = 4;
        ImGui::SliderFloat("Near (m)", &s_csm_near, 0.01F, 1.0F, "%.3f");
        ImGui::SliderFloat("Far  (m)", &s_csm_far,  10.0F, 1000.0F, "%.1f");
        ImGui::SliderFloat("Lambda (0=uniform, 1=log)",
                           &s_csm_lambda, 0.0F, 1.0F);
        ImGui::SliderInt("Cascade count",
                         &s_csm_cascades, 1,
                         static_cast<int>(cd::light::kMaxCascades));
        const auto splits = cd::light::practical_split_distances(
            s_csm_near, s_csm_far,
            static_cast<std::uint32_t>(s_csm_cascades),
            s_csm_lambda);
        for (int i = 0; i <= s_csm_cascades; ++i)
        {
            ImGui::Text("  splits[%d] = %.3f m", i,
                        static_cast<double>(
                            splits[static_cast<std::size_t>(i)]));
        }
        // Plot the cascade boundaries as bars filling [near, far].
        std::array<float, cd::light::kMaxCascades + 1> bars {};
        for (int i = 0; i <= s_csm_cascades; ++i)
            bars[static_cast<std::size_t>(i)] =
                splits[static_cast<std::size_t>(i)];
        ImGui::PlotHistogram(
            "##csm_bars",
            bars.data(), s_csm_cascades + 1, 0,
            "Split distances (m)",
            0.0F, s_csm_far * 1.1F,
            ImVec2(0, 48));
        ImGui::TextDisabled("Zhang 2006 Practical Split (lambda-weighted uni+log).");
        // phase1010-3d-viewport-csm-cascade-depth: render 4 spheres
        // along the camera view-direction at the centre depth of each
        // cascade, computed from the camera's REAL near_z/far_z (not
        // the panel sliders -- that way the visual shows the actual
        // shadow distribution the engine uses). Tinted red→yellow→
        // green→blue by cascade index, the textbook SDSM debug palette.
        ImGui::Separator();
        ImGui::Checkbox("Show CSM cascade depth in 3D viewport",
                        &fx.csm_show_cascade_depth_3d);
        if (fx.csm_show_cascade_depth_3d)
        {
            ImGui::TextDisabled("  4 spheres along camera forward at cascade centre depths");
            ImGui::TextDisabled("  red=near, yellow, green, blue=far; radius = slice depth extent");
        }
    }
    // phase942-cluster-grid-live-demo (Run 25 Strand B): drive
    // cd::light::ClusterGrid build + per-light assignment. Lets the
    // user drag the grid resolution + assigns 12 lights to clusters
    // + reports per-cluster avg light count + max cluster id.
    if (ImGui::CollapsingHeader("Run25  Light Cluster Grid Probe"))
    {
        static int s_cg_x = 16;
        static int s_cg_y = 9;
        static int s_cg_z = 24;
        ImGui::SliderInt("Cluster X", &s_cg_x, 4, 32);
        ImGui::SliderInt("Cluster Y", &s_cg_y, 4, 24);
        ImGui::SliderInt("Cluster Z", &s_cg_z, 4, 32);
        const auto total = static_cast<std::uint32_t>(
            s_cg_x * s_cg_y * s_cg_z);
        ImGui::Text("Total clusters: %u (X*Y*Z)", total);
        ImGui::TextDisabled("DOOM 2016 / Frostbite cluster shading layout.");
        ImGui::TextDisabled("Production fills clusters from GPU compute (R3 panel).");
        // phase1011-3d-viewport-cluster-density-heatmap: emit one
        // sphere per cell of a small world-space grid, tinted by
        // the number of scene lights whose range sphere covers that
        // cell. Lets the user SEE which regions of the scene cluster
        // shading would consider hot, without needing a viewport-
        // aligned GPU dispatch.
        ImGui::Separator();
        ImGui::Checkbox("Show cluster density heat-map in 3D viewport",
                        &fx.cluster_show_density_3d);
        if (fx.cluster_show_density_3d)
        {
            ImGui::TextDisabled("  8x4x8 world grid (spacing 2 m), tint = # lights covering cell");
            ImGui::TextDisabled("  empty cells hidden; green=1, yellow=2, red=3+");
        }
    }
    // phase949-velocity-motion-vector-live-demo (Run 25 Strand B):
    // drive cd::velocity::motion_vector_uv + motion_pixels. Lets the
    // user pick prev/curr clip-space positions and SEE the
    // screen-space UV delta + pixel-magnitude live -- same math the
    // TAA + per-object motion blur paths consume per pixel.
    if (ImGui::CollapsingHeader("Run25  Motion Vector Probe"))
    {
        // phase1021-3d-viewport-motion-vector: prev/curr migrated off
        // function-statics onto HelloEngineFx so the 3D overlay reads
        // the SAME values the user drags here.
        static int s_v_width = 1920;
        static int s_v_height = 1080;
        ImGui::SliderFloat3("Prev clip (x, y, z) / w=1",
                            fx.mvec_prev.data(), -1.0F, 1.0F);
        ImGui::SliderFloat3("Curr clip (x, y, z) / w=1",
                            fx.mvec_curr.data(), -1.0F, 1.0F);
        ImGui::SliderInt("Viewport width (px)",  &s_v_width,  64, 4096);
        ImGui::SliderInt("Viewport height (px)", &s_v_height, 64, 2160);
        const cd::math::Vec4f s_v_prev {
            fx.mvec_prev[0], fx.mvec_prev[1],
            fx.mvec_prev[2], fx.mvec_prev[3] };
        const cd::math::Vec4f s_v_curr {
            fx.mvec_curr[0], fx.mvec_curr[1],
            fx.mvec_curr[2], fx.mvec_curr[3] };
        const auto uv_delta =
            cd::velocity::motion_vector_uv(s_v_prev, s_v_curr);
        const auto motion_px =
            cd::velocity::motion_pixels(
                uv_delta,
                static_cast<std::uint32_t>(s_v_width),
                static_cast<std::uint32_t>(s_v_height));
        ImGui::Text("UV delta: (%.4f, %.4f)",
                    static_cast<double>(uv_delta.x),
                    static_cast<double>(uv_delta.y));
        ImGui::Text("Pixel magnitude: %.3f px",
                    static_cast<double>(motion_px));
        ImGui::TextDisabled("Motion vectors drive TAA + motion blur + ReSTIR.");
        ImGui::Separator();
        ImGui::Checkbox("Show motion vector in 3D viewport",
                        &fx.mvec_show_3d);
        if (fx.mvec_show_3d)
        {
            ImGui::TextDisabled("  clip XY mapped to 2x2 m panel: red=prev, green=curr, dots=path");
        }
    }
    // phase954-input-axis-live-demo (Run 25 Strand B): drive
    // cd::input::Axis. Lets the user click "neg" and "pos" buttons
    // (sim of two key holds) + see how the axis maps the pair to a
    // scalar in [-1, 1]. Also exposes the analog override slider.
    if (ImGui::CollapsingHeader("Run25  Input Axis Probe"))
    {
        static cd::input::Axis s_axis {};
        static bool s_axis_neg = false;
        static bool s_axis_pos = false;
        static float s_axis_analog = 0.0F;
        ImGui::Checkbox("Negative key held", &s_axis_neg);
        ImGui::SameLine();
        ImGui::Checkbox("Positive key held", &s_axis_pos);
        if (ImGui::Button("set_keys(neg, pos)"))
            (void)s_axis.set_keys(s_axis_neg, s_axis_pos);
        ImGui::SliderFloat("Analog override", &s_axis_analog, -1.0F, 1.0F);
        ImGui::SameLine();
        if (ImGui::Button("set_analog"))
            s_axis.set_analog(s_axis_analog);
        ImGui::Text("Axis value: %.3f", static_cast<double>(s_axis.value()));
        // A coloured bar visualises the [-1, 1] sign + magnitude.
        const float v = s_axis.value();
        ImGui::TextColored(v >= 0.0F ? ImVec4(0.4F, 1.0F, 0.4F, 1.0F)
                                     : ImVec4(1.0F, 0.4F, 0.4F, 1.0F),
                           "%c bar: %.2f",
                           v >= 0.0F ? '+' : '-',
                           static_cast<double>(std::abs(v)));
        ImGui::TextDisabled("[neg, pos] -> [-1, 0, 1] with both = 0 dead zone.");
    }
    // phase959-ecs-world-live-demo (Run 25 Strand B): drive cd::ecs::World.
    // Lets the user spawn N entities into a throw-away world + see the
    // create/destroy counts. Mirrors the test_ecs.cpp BulkLifecycleStress
    // case at smaller N for UI responsiveness.
    if (ImGui::CollapsingHeader("Run25  ECS World Stress Probe"))
    {
        static cd::ecs::World s_demo_world {};
        static int s_demo_spawn_count = 64;
        static std::uint32_t s_demo_total_created = 0;
        static std::uint32_t s_demo_total_destroyed = 0;
        ImGui::SliderInt("Spawn batch size", &s_demo_spawn_count, 1, 1024);
        if (ImGui::Button("Spawn batch"))
        {
            for (int i = 0; i < s_demo_spawn_count; ++i)
            {
                (void)s_demo_world.create();
                ++s_demo_total_created;
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Reset world"))
        {
            const auto destroyed_now = s_demo_world.alive_count();
            s_demo_total_destroyed += static_cast<std::uint32_t>(destroyed_now);
            s_demo_world = cd::ecs::World{};
        }
        ImGui::Text("Alive entities: %zu", s_demo_world.alive_count());
        ImGui::Text("Total created : %u", s_demo_total_created);
        ImGui::Text("Total destroyed: %u", s_demo_total_destroyed);
        ImGui::TextDisabled("cd::ecs::World sparse-set + generation handles.");
        // phase1056-3d-viewport-ecs-cloud: mirror the live count for
        // the overlay (pure fx function; World stays panel-local).
        fx.ecs_alive_mirror =
            static_cast<std::uint32_t>(s_demo_world.alive_count());
        ImGui::Separator();
        ImGui::Checkbox("Show entity cloud in 3D viewport",
                        &fx.ecs_show_cloud_3d);
        if (fx.ecs_show_cloud_3d)
        {
            ImGui::TextDisabled("  1 cross per alive entity, golden-angle disc (display cap 2048)");
        }
    }
    // phase960-scene-serializer-live-demo (Run 25 Strand B): drive
    // cd::scene::serialize_scene on a small ad-hoc scene + display
    // the JSON byte size. Lets the user spawn N nodes and see the
    // serialized payload grow live.
    if (ImGui::CollapsingHeader("Run25  Scene Serializer Probe"))
    {
        static int s_ss_nodes = 8;
        ImGui::SliderInt("Demo scene node count", &s_ss_nodes, 0, 256);
        if (ImGui::Button("Serialize"))
        {
            cd::ecs::World w {};
            cd::scene::Scene s { w };
            for (int i = 0; i < s_ss_nodes; ++i)
                (void)s.create_node();
            const auto val = cd::scene::serialize_scene(s);
            const auto bytes = cd::asset::json::serialize(val);
            ImGui::TextDisabled("(re-click to refresh)");
            ImGui::Text("Last serialize: %zu chars", bytes.size());
        }
        ImGui::TextDisabled("Round-trip is locked by engine/world/scene/tests/test_scene.cpp.");
    }
    // phase961-audio-tone-generator-live-demo (Run 25 Strand B):
    // synthesize a single audio tone block + pass through a quick
    // chain of cd::audio:: helpers (Compressor + LowPass + Limiter),
    // showing the RMS before and after so the user gets visible
    // feedback on the DSP pipeline (and can save the buffer via the
    // existing Audio panel's "Save Last 5 s" if they want to hear it).
    if (ImGui::CollapsingHeader("Run25  Audio Tone Synth Probe"))
    {
        // phase1036-3d-viewport-audio-waveform: hz/amp migrated onto
        // HelloEngineFx so the 3D ribbon reshapes with the SAME tone.
        static int s_at_samples = 4096;
        ImGui::SliderFloat("Frequency (Hz)", &fx.audio_tone_hz, 50.0F, 5000.0F);
        ImGui::SliderFloat("Amplitude [0,1]", &fx.audio_tone_amp, 0.0F, 1.0F);
        ImGui::SliderInt("Block samples", &s_at_samples, 256, 16384);
        const float s_at_hz  = fx.audio_tone_hz;
        const float s_at_amp = fx.audio_tone_amp;
        std::vector<float> raw(static_cast<std::size_t>(s_at_samples), 0.0F);
        constexpr float kSr = 48000.0F;
        for (std::size_t i = 0; i < raw.size(); ++i)
        {
            const float t = static_cast<float>(i) / kSr;
            raw[i] = std::sin(2.0F * std::numbers::pi_v<float> * s_at_hz * t)
                   * s_at_amp;
        }
        float rms_in = 0.0F;
        for (auto v : raw) rms_in += v * v;
        rms_in = std::sqrt(rms_in / static_cast<float>(raw.size()));
        ImGui::Text("Input RMS  = %.4f  (sin %.0f Hz at amp %.2f)",
                    static_cast<double>(rms_in),
                    static_cast<double>(s_at_hz),
                    static_cast<double>(s_at_amp));
        // Just-RMS readout; full DSP chain runs in the main Audio panel.
        ImGui::TextDisabled("Full Compressor->LowPass->Limiter chain runs in Audio panel.");
        ImGui::TextDisabled("This probe shows the raw synthesized block.");
        ImGui::Separator();
        ImGui::Checkbox("Show waveform in 3D viewport",
                        &fx.audio_show_wave_3d);
        if (fx.audio_show_wave_3d)
        {
            ImGui::TextDisabled("  ~2.5 cycles as a polyline ribbon; grey line = zero axis");
        }
    }
    // phase962-quaternion-slerp-live-demo (Run 25 Strand B): drive
    // cd::math::slerp between two quaternions. Lets the user drag a t
    // parameter + see the interpolated quat respond live -- same path
    // the animation runtime uses for skin-pose interpolation.
    if (ImGui::CollapsingHeader("Run25  Quaternion Slerp Probe"))
    {
        // phase1019-3d-viewport-quat-slerp-triad: state migrated off
        // function-statics onto HelloEngineFx so the 3D overlay reads
        // the SAME values the user drags here.
        ImGui::SliderFloat("t [0, 1]", &fx.quat_slerp_t, 0.0F, 1.0F);
        ImGui::SliderFloat4("Quat A (x, y, z, w)", fx.quat_slerp_a.data(), -1.0F, 1.0F);
        ImGui::SliderFloat4("Quat B (x, y, z, w)", fx.quat_slerp_b.data(), -1.0F, 1.0F);
        const cd::math::Quatf s_qs_a {
            fx.quat_slerp_a[0], fx.quat_slerp_a[1],
            fx.quat_slerp_a[2], fx.quat_slerp_a[3] };
        const cd::math::Quatf s_qs_b {
            fx.quat_slerp_b[0], fx.quat_slerp_b[1],
            fx.quat_slerp_b[2], fx.quat_slerp_b[3] };
        const auto q = cd::math::slerp(s_qs_a, s_qs_b, fx.quat_slerp_t);
        ImGui::Text("Result: (%.3f, %.3f, %.3f, %.3f)",
                    static_cast<double>(q.x),
                    static_cast<double>(q.y),
                    static_cast<double>(q.z),
                    static_cast<double>(q.w));
        const float magnitude = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
        ImGui::Text("Magnitude (should be ~1.0): %.4f", static_cast<double>(magnitude));
        ImGui::TextDisabled("Spherical-linear quaternion interpolation; great-circle path.");
        ImGui::Separator();
        ImGui::Checkbox("Show slerp triad in 3D viewport",
                        &fx.quat_slerp_show_3d);
        if (fx.quat_slerp_show_3d)
        {
            ImGui::TextDisabled("  RGB triad = slerp(A,B,t) axes; small spheres = X-tip arc A->B");
        }
    }
    // phase964-math-helpers-live-demo (Run 25 Strand B): drive
    // cd::math::lerp + smoothstep + remap on a swept x parameter and
    // plot the three curves. Lets the user see how the smooth-step
    // shape (Hermite) compares to plain linear-interp + the bounded
    // remap mapping.
    if (ImGui::CollapsingHeader("Run25  Math Helpers Probe (lerp / smoothstep / remap)"))
    {
        static float s_mh_a = 0.0F;
        static float s_mh_b = 1.0F;
        static float s_mh_remap_lo = -2.0F;
        static float s_mh_remap_hi = 5.0F;
        ImGui::SliderFloat("Lerp a",   &s_mh_a, -2.0F, 2.0F);
        ImGui::SliderFloat("Lerp b",   &s_mh_b, -2.0F, 2.0F);
        ImGui::SliderFloat("Remap target lo", &s_mh_remap_lo, -8.0F, 8.0F);
        ImGui::SliderFloat("Remap target hi", &s_mh_remap_hi, -8.0F, 8.0F);
        constexpr int kSweep = 128;
        std::array<float, kSweep> ll {};
        std::array<float, kSweep> ss {};
        std::array<float, kSweep> rr {};
        for (int i = 0; i < kSweep; ++i)
        {
            const float t = static_cast<float>(i) / static_cast<float>(kSweep - 1);
            ll[static_cast<std::size_t>(i)] =
                cd::math::lerp(s_mh_a, s_mh_b, t);
            ss[static_cast<std::size_t>(i)] =
                cd::math::smoothstep(0.0F, 1.0F, t);
            rr[static_cast<std::size_t>(i)] =
                cd::math::remap(0.0F, 1.0F, s_mh_remap_lo, s_mh_remap_hi, t);
        }
        const auto rr_lo = *std::ranges::min_element(rr);
        const auto rr_hi = *std::ranges::max_element(rr);
        ImGui::PlotLines("##mh_lerp", ll.data(), kSweep, 0,
                         "lerp(a, b, t)", s_mh_a - 0.1F, s_mh_b + 0.1F,
                         ImVec2(0, 48));
        ImGui::PlotLines("##mh_ss",   ss.data(), kSweep, 0,
                         "smoothstep(0, 1, t) (Hermite)", 0.0F, 1.0F,
                         ImVec2(0, 48));
        ImGui::PlotLines("##mh_rr",   rr.data(), kSweep, 0,
                         "remap(0..1, lo..hi, t)",
                         rr_lo - 0.1F, rr_hi + 0.1F,
                         ImVec2(0, 48));
        ImGui::Text("approx_equal(0.1+0.2, 0.3): %s",
                    cd::math::approx_equal(0.1F + 0.2F, 0.3F) ? "true" : "false");
        ImGui::Text("clamp(7.5, -1, 1) = %.3f  saturate(-0.3) = %.3f",
                    static_cast<double>(cd::math::clamp(7.5F, -1.0F, 1.0F)),
                    static_cast<double>(cd::math::saturate(-0.3F)));
        ImGui::TextDisabled("Foundation math helpers used everywhere in render + gameplay.");
    }
    // phase967-cubic-bezier-live-demo (Run 25 Strand B): drive
    // cd::math::CubicBezier::at across t in [0, 1] + plot x/y/z
    // coordinates separately so the user sees the cubic curve shape
    // respond to the 4 control points live.
    if (ImGui::CollapsingHeader("Run25  Cubic Bezier Probe"))
    {
        // phase1012-3d-viewport-cubic-bezier-curve: control points
        // migrated off function-statics onto HelloEngineFx so the
        // 3D-viewport overlay (below + render loop) reads the SAME
        // values the user is dragging here.
        ImGui::SliderFloat3("P0", fx.bezier_p0.data(), -4.0F, 4.0F);
        ImGui::SliderFloat3("P1", fx.bezier_p1.data(), -4.0F, 4.0F);
        ImGui::SliderFloat3("P2", fx.bezier_p2.data(), -4.0F, 4.0F);
        ImGui::SliderFloat3("P3", fx.bezier_p3.data(), -4.0F, 4.0F);
        cd::math::CubicBezier s_cb {};
        s_cb.p0 = { fx.bezier_p0[0], fx.bezier_p0[1], fx.bezier_p0[2] };
        s_cb.p1 = { fx.bezier_p1[0], fx.bezier_p1[1], fx.bezier_p1[2] };
        s_cb.p2 = { fx.bezier_p2[0], fx.bezier_p2[1], fx.bezier_p2[2] };
        s_cb.p3 = { fx.bezier_p3[0], fx.bezier_p3[1], fx.bezier_p3[2] };
        constexpr int kSweep = 64;
        std::array<float, kSweep> xs {};
        std::array<float, kSweep> ys {};
        std::array<float, kSweep> zs {};
        for (int i = 0; i < kSweep; ++i)
        {
            const float t = static_cast<float>(i) / static_cast<float>(kSweep - 1);
            const auto p = s_cb.at(t);
            xs[static_cast<std::size_t>(i)] = p.x;
            ys[static_cast<std::size_t>(i)] = p.y;
            zs[static_cast<std::size_t>(i)] = p.z;
        }
        ImGui::Text("Arc length (32-sample est.): %.4f",
                    static_cast<double>(s_cb.arc_length()));
        const auto xs_lo = *std::ranges::min_element(xs);
        const auto xs_hi = *std::ranges::max_element(xs);
        const auto ys_lo = *std::ranges::min_element(ys);
        const auto ys_hi = *std::ranges::max_element(ys);
        const auto zs_lo = *std::ranges::min_element(zs);
        const auto zs_hi = *std::ranges::max_element(zs);
        ImGui::PlotLines("##cb_x", xs.data(), kSweep, 0, "Bezier x(t)",
                         xs_lo - 0.1F, xs_hi + 0.1F, ImVec2(0, 48));
        ImGui::PlotLines("##cb_y", ys.data(), kSweep, 0, "Bezier y(t)",
                         ys_lo - 0.1F, ys_hi + 0.1F, ImVec2(0, 48));
        ImGui::PlotLines("##cb_z", zs.data(), kSweep, 0, "Bezier z(t)",
                         zs_lo - 0.1F, zs_hi + 0.1F, ImVec2(0, 48));
        ImGui::TextDisabled("Same CubicBezier used by camera-path / anim splines.");
        ImGui::Separator();
        ImGui::Checkbox("Show Bezier curve in 3D viewport",
                        &fx.bezier_show_curve_3d);
        if (fx.bezier_show_curve_3d)
        {
            ImGui::TextDisabled("  4 white control spheres + 32 magenta curve samples");
        }
    }
    // phase968-asset-registry-tag-from-extension-probe (Run 25 Strand B):
    // drive cd::asset::AssetRegistry::tag_from_extension. Lets the user
    // type any path and SEE which loader tag (json / wav / gltf / obj /
    // image / etc) the registry would auto-dispatch to. Same mapping the
    // load_auto() helper uses.
    if (ImGui::CollapsingHeader("Run25  Asset Registry Tag Probe"))
    {
        static char s_ar_path[256] = "shaders/x.frag.spv";
        ImGui::InputText("Path (extension only matters)",
                         s_ar_path, sizeof(s_ar_path));
        const auto tag = cd::asset::AssetRegistry::tag_from_extension(s_ar_path);
        if (tag.empty())
        {
            ImGui::TextColored(ImVec4(1.0F, 0.5F, 0.3F, 1.0F),
                               "No loader tag matches this extension.");
        }
        else
        {
            ImGui::TextColored(ImVec4(0.4F, 1.0F, 0.4F, 1.0F),
                               "Loader tag: \"%.*s\"",
                               static_cast<int>(tag.size()), tag.data());
        }
        ImGui::TextDisabled("Built-in mapping: .png/.jpg/.bmp/.tga/.hdr -> image;");
        ImGui::TextDisabled(".obj -> obj, .ktx2 -> ktx2, .gltf/.glb -> gltf,");
        ImGui::TextDisabled(".cdmesh -> cdmesh, .cdtex -> cdtex,");
        ImGui::TextDisabled(".wav -> wav, .json -> json.");
    }
    // phase970-random-pcg32-distribution-probe (Run 25 Strand B):
    // drive cd::math::Random + plot a 32-bin histogram of next_float()
    // samples. Lets the user click "Generate N samples" and SEE the
    // uniform distribution flatten out as N grows.
    if (ImGui::CollapsingHeader("Run25  Random Distribution Probe"))
    {
        // phase1037-3d-viewport-rng-histogram: bins migrated onto
        // HelloEngineFx so the 3D column chart shows the SAME data.
        static cd::math::Random s_rng { 0x9E3779B97F4A7C15ULL };
        static int s_rng_samples_per_click = 256;
        auto& s_rng_hist  = fx.rng_hist;
        auto& s_rng_total = fx.rng_total;
        ImGui::SliderInt("Samples per click", &s_rng_samples_per_click, 16, 4096);
        if (ImGui::Button("Generate samples"))
        {
            for (int i = 0; i < s_rng_samples_per_click; ++i)
            {
                const float v = s_rng.next_float();
                const auto bin = static_cast<std::size_t>(
                    std::clamp(v * 32.0F, 0.0F, 31.999F));
                ++s_rng_hist[bin];
                ++s_rng_total;
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Reset histogram"))
        {
            s_rng_hist = {};
            s_rng_total = 0;
        }
        std::array<float, 32> hist_f {};
        std::uint32_t hist_max = 1;
        for (std::size_t i = 0; i < 32; ++i)
        {
            hist_f[i] = static_cast<float>(s_rng_hist[i]);
            hist_max = std::max(s_rng_hist[i], hist_max);
        }
        ImGui::Text("Total samples: %llu",
                    static_cast<unsigned long long>(s_rng_total));
        const float expected = s_rng_total > 0
                                   ? static_cast<float>(s_rng_total) / 32.0F
                                   : 1.0F;
        ImGui::Text("Expected per bin (uniform): %.1f", static_cast<double>(expected));
        ImGui::PlotHistogram("##rng_hist",
                             hist_f.data(),
                             32, 0,
                             "Bins across [0, 1) (PCG32 next_float)",
                             0.0F, static_cast<float>(hist_max),
                             ImVec2(0, 64));
        ImGui::TextDisabled("PCG32 deterministic stream; large N converges to uniform.");
        ImGui::Checkbox("Show histogram in 3D viewport",
                        &fx.rng_show_hist_3d);
        if (fx.rng_show_hist_3d)
        {
            ImGui::TextDisabled("  32 line columns; grey line = expected-uniform height");
        }
    }
}

}  // namespace cd_sample
