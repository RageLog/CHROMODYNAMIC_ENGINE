// =============================================================================
// CHROMODYNAMIC STUDIO -- apps/editor/ui/ProjectHubModal.hpp
//
// Clean Valve Source 2 / Godot 4 Project Hub screen:
//   * Positioned cleanly below the single 32px Top Bar
//   * 2D-First & 3D Commercial Starter Templates (2D Platformer, 3D Empty Stage,
//     3D Physics Sandbox, 3D Action Game, PBR Optics, Sponza Atrium + DDGI)
//   * Direct Showcase & Recent Scene browser on disk
// =============================================================================
#pragma once

#include "core/StudioState.hpp"

#include <cstdint>

namespace cd::studio::ui
{

void draw_project_hub_modal(StudioEngineState& s, std::uint32_t frame_w, std::uint32_t frame_h);

}  // namespace cd::studio::ui
