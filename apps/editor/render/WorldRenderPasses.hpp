// =============================================================================
// CHROMODYNAMIC STUDIO -- apps/editor/render/WorldRenderPasses.hpp
//
// Valve Source 2 worldrenderer-modeled 3D GPU render passes for Studio:
//   * Sun resolution & multi-light UBO upload
//   * Analytical sky pass (delegating camera basis to cd::app_common::CameraMath3D)
//   * CSM shadow-map depth pass & golden planar shadows
//   * HDR MRT scene pass (serial & 3-lane parallel secondary recorders)
//   * Velocity G-buffer pass, Karis 7-pass bloom mip chain, TAA/SSR composite pass
// =============================================================================
#pragma once

#include "core/StudioState.hpp"
#include "StudioLighting.hpp"

#include <cd/rhi/ICommandBuffer.hpp>

#include <chrono>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

namespace cd::studio::render
{

[[nodiscard]] SunLight resolve_sun_light(const std::vector<LightRow>& lights) noexcept;

void fill_prim_push_shared(cd::studio_rt::PrimPush& pp,
                           const cd_studio::StudioRenderFx& fx,
                           const SunLight& sun,
                           const cd::camera::Camera& cam) noexcept;

void upload_multi_light_ubo(cd::rhi::IDevice& device,
                            cd::rhi::BufferHandle lights_ubo,
                            const std::vector<LightRow>& lights,
                            cd::core::CounterTable& counters);

void draw_sky_pass(cd::rhi::IDrawRecorder& cmd,
                   const cd::camera::Camera& cam,
                   float aspect,
                   const SunLight& sun,
                   cd::material::Material& sky_material);

void draw_planar_shadows(StudioEngineState& s,
                         cd::rhi::IDrawRecorder& cmd,
                         const SunLight& sun,
                         float floor_y,
                         float shadow_lift,
                         const cd::math::Mat4f& vp);

void draw_shadow_map_pass(StudioEngineState& s,
                          cd::rhi::ICommandBuffer& cmd,
                          const SunLight& sun,
                          cd::rhi::Extent2D shadow_map_size,
                          bool shadow_dirty);

void velocity_gbuffer_pass(StudioEngineState& s,
                           cd::rhi::ICommandBuffer& cmd,
                           cd::rhi::Extent2D extent,
                           const cd::math::Mat4f& vp_unjittered,
                           bool force_first_frame);

void run_bloom_chain(StudioEngineState& s,
                     cd::rhi::ICommandBuffer& cmd,
                     float ev_stops,
                     bool force_first_frame);

void begin_composite_pass(StudioEngineState& s,
                          cd::rhi::ICommandBuffer& cmd,
                          cd::rhi::TextureViewHandle swapchain_view,
                          cd::rhi::Extent2D extent,
                          std::chrono::steady_clock::time_point frame_loop_start,
                          const cd::math::Mat4f& vp_unjittered);

[[nodiscard]] HdrSceneFrame make_hdr_frame(std::uint32_t frame_idx,
                                           cd::rhi::Extent2D extent,
                                           const cd::camera::Camera& cam,
                                           float taa_amount) noexcept;

[[nodiscard]] HdrSceneFrame begin_hdr_scene_pass(StudioEngineState& s,
                                                 cd::rhi::ICommandBuffer& cmd,
                                                 cd::rhi::Extent2D extent);

[[nodiscard]] std::pair<HdrSceneFrame, std::unique_ptr<cd::rhi::IParallelPassRecorder>>
begin_hdr_scene_pass_lanes(StudioEngineState& s,
                           cd::rhi::ICommandBuffer& cmd,
                           cd::rhi::Extent2D extent,
                           std::uint32_t lane_count);

void prep_hdr_lane(cd::rhi::IDrawRecorder& lane, cd::rhi::Extent2D extent);

void draw_floor_and_entities(StudioEngineState& s,
                             cd::rhi::IDrawRecorder& cmd,
                             float floor_y,
                             const cd::math::Mat4f& vp,
                             const SunLight& sun,
                             bool show_editor_floor);

void spawn_golden_fixture_entities(StudioEngineState& s, bool chrome_probe_mode);

}  // namespace cd::studio::render
