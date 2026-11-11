// =============================================================================
// CHROMODYNAMIC STUDIO -- apps/editor/ui/ProjectAndToolsDock.hpp
//
// Valve Source 2 Asset Browser + Godot 4 FileSystem & Tools Dock
// ("Project & Tools", slot = 4):
//   * Tab 0: Assets & FileSystem -- direct 2-pane browser (Folder Tree on left,
//            responsive IconTile Grid on right; single-click selects & syncs
//            context, double-click opens/spawns/assigns, drag-and-drop onto
//            3D Viewport or Inspector slots)
//   * Tab 1: Console -- clean searchable engine log
//   * Tab 2: Construct Catalog -- 250 engine constructs with category filters
//   * Tab 3: Profiler -- frame-time graph, pass counters, diagnostic probe gate
// =============================================================================
#pragma once

#include "core/StudioState.hpp"

namespace cd::studio::ui
{

void draw_project_and_tools_dock(StudioEngineState& s);

}  // namespace cd::studio::ui
