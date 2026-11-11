// =============================================================================
// CHROMODYNAMIC STUDIO -- apps/editor/ui/SceneTreeDock.hpp
//
// Valve Source 2 Hammer 5 + Godot 4 Scene Outliner ("Scene Outliner", slot = 1):
//   * [+ Add Node] popup + Search filter + Layer filter
//   * Unified clean Scene Tree (Lights + 3D Entities) with eye/lock toggles,
//     F2 / double-click inline rename, right-click context menu, and drag-drop
// =============================================================================
#pragma once

#include "core/StudioState.hpp"

namespace cd::studio::ui
{

void draw_scene_tree_dock(StudioEngineState& s);

}  // namespace cd::studio::ui
