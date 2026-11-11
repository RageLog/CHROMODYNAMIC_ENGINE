// =============================================================================
// CHROMODYNAMIC STUDIO -- apps/editor/ui/InspectorDock.hpp
//
// Valve Source 2 Hammer 5 + Godot 4 Property Inspector ("Inspector", slot = 3):
//   * Clean 2-tab layout: Inspector | World & Post-FX
//   * Selected Entity: compact header, collapsible Transform, Mesh & Surface
//     Material (drag-drop + popup + direct PBR sliders), Skeletal Animation,
//     and Components & Modifiers stack + [+ Add Component...]
//   * Selected Light: name, enable toggle, delete, physical CCT/intensity/cone
// =============================================================================
#pragma once

#include "core/StudioState.hpp"

namespace cd::studio::ui
{

void draw_inspector_dock(StudioEngineState& s);

}  // namespace cd::studio::ui
