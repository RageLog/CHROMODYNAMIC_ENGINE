// =============================================================================
// CHROMODYNAMIC STUDIO -- apps/editor/workspace_2d/Workspace2D.hpp
//
// Compiled 2D Engine Studio subsystem wrapping cd::world2d + cd::render2d:
//   * TileBrush + 16-tile Wang auto-tiling + Greedy Collider meshing
//   * BoxFill, Eraser, Entity/Light Spawner, Selection & Lua live scripting
//   * Headless AI Playtest runner & JSON command bus
// =============================================================================
#pragma once

#include "core/StudioState.hpp"

#include <cd/rhi/ICommandBuffer.hpp>

#include <cstdint>

namespace cd::studio::workspace_2d
{

void draw_workspace_2d(StudioEngineState& s, float dt, std::uint32_t frame_w, std::uint32_t frame_h);

void record_2d_canvas(StudioEngineState& s,
                      cd::rhi::ICommandBuffer& cmd,
                      cd::rhi::Extent2D extent);

}  // namespace cd::studio::workspace_2d
