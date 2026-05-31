// =============================================================================
// CHROMODYNAMIC — cd/editor/panel_inspector/Inspector.hpp
//
// phase543 — cd::editor::panel::inspector  (panel_inspector library)
//
// Inspector panel: shows the LocalTransform components of the currently
// selected ECS entity and provides two draw paths:
//
//   * draw()        — DrawBatcher-based path for the DockSpace shell
//                     (apps/editor). Renders a coloured background + row
//                     labels for Position, Scale, and Rotation fields.
//
//   * draw_imgui()  — ImGui path for hello_editor (ImGui DragFloat3 /
//                     SliderFloat drag-fields that mutate the live scene).
//
// State setters (both paths use the same state):
//   set_target(Entity)        — entity to inspect (invalid = no selection)
//   set_world_ptr(World*)     — non-owning pointer to the ECS World (allows
//                               LocalTransform lookup via World::get<>)
//   get_selected() const      — returns the currently tracked entity
//
// Lifetime contract:
//   The World* (and the Scene* used by draw_imgui) must outlive the
//   Inspector or be cleared via set_world_ptr(nullptr) / set_scene_ptr(nullptr)
//   before they are destroyed.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <cd/ecs/Entity.hpp>
#include <cd/ecs/World.hpp>
#include <cd/ui/renderer/DrawBatcher.hpp>
#include <cd/ui/widgets/Widgets.hpp>

// Forward declarations for the draw_imgui path (only needed when the caller
// includes imgui.h and links cd::editor).
namespace cd::scene  { class Scene; }
namespace cd::editor { class EditHistory; }

namespace cd::editor::panel::inspector
{

class Inspector
{
public:
    // Default-constructible; starts with no target entity.
    Inspector() noexcept = default;

    // ---- State API ----------------------------------------------------------

    /// Set the entity to inspect. Pass a default-constructed (invalid) Entity
    /// to clear the selection.
    void set_target(cd::ecs::Entity e) noexcept;

    /// Set the non-owning pointer to the ECS World. May be nullptr to detach.
    void set_world_ptr(cd::ecs::World* world) noexcept;

    /// Returns the entity currently being inspected.
    [[nodiscard]] cd::ecs::Entity get_selected() const noexcept;

    // ---- DrawBatcher path (DockSpace / apps/editor) -------------------------

    /// Emit draw commands into `batcher` within `bounds`.
    /// Renders the inspector panel background + field rows using the
    /// DrawBatcher solid-quad and (future) glyph APIs.
    ///
    /// Thread-safety: must be called from the render thread only.
    void draw(cd::ui::renderer::DrawBatcher& batcher,
              const cd::ui::widgets::Theme&   theme,
              const cd::ui::widgets::Rect&    bounds) const;

    // ---- ImGui path (hello_editor) ------------------------------------------

    /// Render the inspector using ImGui widgets (DragFloat3, SliderFloat …).
    /// Pushes RotateCommand / TranslateCommand / ScaleCommand through
    /// `history` on drag-end so Ctrl+Z / Ctrl+Y undo works.
    ///
    /// `rot_slider_deg`    — caller-owned Y-rotation accumulator (reset when
    ///                       the selection changes).
    /// `rot_slider_entity` — tracks which entity the slider is targeting so
    ///                       the accumulator is reset on selection change.
    ///
    /// Preconditions:
    ///   * ImGui::NewFrame() has been called this frame.
    ///   * scene != nullptr and history != nullptr.
    ///   * set_world_ptr() has been called with the same World that backs
    ///     scene->world().
    ///
    /// Returns true if an entity is selected and was rendered.
    bool draw_imgui(cd::scene::Scene&      scene,
                    cd::editor::EditHistory& history,
                    float&                 rot_slider_deg,
                    cd::ecs::Entity&       rot_slider_entity) const;

private:
    cd::ecs::Entity  target_   {};         ///< Currently inspected entity.
    cd::ecs::World*  world_    { nullptr }; ///< Non-owning ptr to the ECS World.
};

}  // namespace cd::editor::panel::inspector
