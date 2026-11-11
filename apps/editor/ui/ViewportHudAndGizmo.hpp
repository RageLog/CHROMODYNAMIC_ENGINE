// =============================================================================
// CHROMODYNAMIC STUDIO -- apps/editor/ui/ViewportHudAndGizmo.hpp
//
// Valve Source 2 Hammer 5 Viewport Overlay, 3D Axis Gizmo & Workspace Router:
//   * Minimal top-left Hammer 5 viewport pill strip ([Move|Rotate|Scale],
//     Shading mode, Camera bookmarks, Snap, Grid, Colliders, Physics, Focus)
//   * Top-right 3D X/Y/Z Orientation Rose (via cd::app_common::CameraMath3D)
//   * Selection silhouette ring & selected-light gizmos
//   * Ray-plane & screen-space 3D Translate / Rotate / Scale gizmo + Undo
//   * Clean Command Palette (F1 / Ctrl+Shift+P) and Workspace Shell router
// =============================================================================
#pragma once

#include "core/StudioState.hpp"

#include <cd/math/Matrix.hpp>
#include <cd/rhi/Descriptors.hpp>

#include <cstdint>

namespace cd::studio::ui
{

void draw_viewport_overlay_strip_and_rose(StudioEngineState& s,
                                          std::uint32_t frame_w,
                                          std::uint32_t frame_h);

void draw_pie_gameplay_hud(StudioEngineState& s,
                           float dt,
                           const cd::math::Mat4f& vp,
                           std::uint32_t frame_w,
                           std::uint32_t frame_h);

void draw_shader_graph_workspace(StudioEngineState& s,
                                 std::uint32_t frame_w,
                                 std::uint32_t frame_h);

void draw_selection_and_light_overlays(StudioEngineState& s,
                                       const cd::math::Mat4f& vp,
                                       cd::rhi::Extent2D extent);

void update_and_draw_transform_gizmo(StudioEngineState& s,
                                     const cd::math::Mat4f& vp,
                                     cd::rhi::Extent2D extent);

void draw_command_palette(StudioEngineState& s, cd::rhi::Extent2D extent);

void draw_workspace_shell(StudioEngineState& s,
                          float dt,
                          const cd::math::Mat4f& vp,
                          std::uint32_t frame_w,
                          std::uint32_t frame_h);

}  // namespace cd::studio::ui
