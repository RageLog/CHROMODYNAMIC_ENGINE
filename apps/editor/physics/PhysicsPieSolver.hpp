// =============================================================================
// CHROMODYNAMIC STUDIO -- apps/editor/physics/PhysicsPieSolver.hpp
//
// Valve Source 2 vphysics2-modeled Play-In-Editor (PIE) & 3D/2D simulation
// stepper, collider wireframe renderer, and 3D drag-and-drop viewport handler.
// =============================================================================
#pragma once

#include "core/StudioState.hpp"

#include <cd/math/Matrix.hpp>

#include <cstdint>

namespace cd::studio::physics_pie
{

void enter_pie(StudioEngineState& s);

void exit_pie(StudioEngineState& s);

void reset_physics_positions(StudioEngineState& s);

void tick_modifiers_and_physics(StudioEngineState& s, float dt);

void draw_viewport_colliders_and_drop_targets(StudioEngineState& s,
                                              const cd::math::Mat4f& vp,
                                              std::uint32_t width,
                                              std::uint32_t height);

}  // namespace cd::studio::physics_pie
