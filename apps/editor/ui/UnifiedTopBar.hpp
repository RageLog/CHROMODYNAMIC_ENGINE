// =============================================================================
// CHROMODYNAMIC STUDIO -- apps/editor/ui/UnifiedTopBar.hpp
//
// Valve Source 2 Hammer 5 + Godot 4 Unified 32px Top Bar:
//   * Single clean 32px top bar (secondary 29px ribbon toolbar eliminated)
//   * Left: File | Edit | Create | View | Project | Help
//   * Center: [ 2D ] [ 3D ] [ Graph ] workspace pills + [ Play (F5) / Stop ] + [ Export .cdpak ]
//   * Right: discrete FPS / ms telemetry badge clear of window chrome buttons
// =============================================================================
#pragma once

#include "core/StudioState.hpp"

namespace cd::studio::ui
{

void draw_unified_top_bar(StudioEngineState& s, float dt);

}  // namespace cd::studio::ui
