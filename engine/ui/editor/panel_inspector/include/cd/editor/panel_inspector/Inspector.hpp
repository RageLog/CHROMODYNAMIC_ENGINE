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
#include <cd/material/AlphaMode.hpp>
#include <cd/ui/renderer/DrawBatcher.hpp>
#include <cd/ui/widgets/Widgets.hpp>

// Forward declarations for the draw_imgui path (only needed when the caller
// includes imgui.h and links cd::editor).
namespace cd::scene  { class Scene; }
namespace cd::editor { class EditHistory; }

// Forward declaration for the M12 W2 PBR / alpha-mode round-trip surface.
// Inspector only stores a non-owning observer pointer; the caller owns the
// MaterialInstance and is responsible for its lifetime.
namespace cd::material { class MaterialInstance; }

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

    // ---- M12 W2: PBR / alpha-mode material binding --------------------------
    //
    // When a MaterialInstance is bound, the DrawBatcher draw path renders an
    // additional PBR section below the LocalTransform rows:
    //
    //   * metallic   slider strip in [0, 1] (filled proportional to value).
    //   * roughness  slider strip in [0, 1].
    //   * alpha_mode 3-button dropdown (kOpaque / kMask / kBlend); the active
    //                mode gets an accent-colored highlight.
    //   * alpha_cutoff slider strip (visible only when alpha_mode == kMask).
    //
    // The setters listed below drive `MaterialInstance::set_metallic` /
    // `set_roughness` / `set_alpha_mode` / `set_alpha_cutoff` through the
    // bound pointer. The accessors read the live MaterialInstance state so
    // the same Inspector instance can round-trip values without caching.
    //
    // Lifetime contract: the MaterialInstance pointer is non-owning. Pass
    // nullptr to detach. The caller is responsible for ensuring the
    // MaterialInstance outlives the Inspector (or for calling
    // set_material_instance(nullptr) before destroying it).

    /// Bind a MaterialInstance for PBR / alpha-mode round-trip editing.
    /// Pass nullptr to detach (PBR section is then hidden in draw()).
    void set_material_instance(cd::material::MaterialInstance* mi) noexcept;

    /// Returns the currently bound MaterialInstance pointer (may be null).
    [[nodiscard]] cd::material::MaterialInstance* material_instance() const noexcept;

    /// Forward metallic / roughness / alpha_mode / alpha_cutoff to the bound
    /// MaterialInstance. No-op when no instance is bound. The MaterialInstance
    /// clamps inputs internally so these are safe to call with any float.
    void set_metallic(float m) noexcept;
    void set_roughness(float r) noexcept;
    void set_alpha_mode(cd::material::AlphaMode mode) noexcept;
    void set_alpha_cutoff(float c) noexcept;

    /// Read the round-trip state from the bound MaterialInstance. The fallback
    /// values (returned when no instance is bound) match the MaterialInstance
    /// defaults so callers do not need to branch on the binding state.
    [[nodiscard]] float metallic() const noexcept;
    [[nodiscard]] float roughness() const noexcept;
    [[nodiscard]] cd::material::AlphaMode alpha_mode() const noexcept;
    [[nodiscard]] float alpha_cutoff() const noexcept;

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
    cd::ecs::Entity                  target_           {};        ///< Currently inspected entity.
    cd::ecs::World*                  world_            { nullptr }; ///< Non-owning ptr to the ECS World.
    cd::material::MaterialInstance*  material_instance_ { nullptr }; ///< M12 W2 non-owning PBR / alpha editor target.
};

}  // namespace cd::editor::panel::inspector
