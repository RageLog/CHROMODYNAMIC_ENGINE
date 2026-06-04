// =============================================================================
// CHROMODYNAMIC — cd/editor/ui/EditorWidgets.hpp
// Phase 5 / Sprint S5.2 — editor-specific widgets.
//
// Sits on top of cd::ui and the world tier (cd::ecs + cd::scene). These
// widgets exist to let an editor process the same data game runtime sees,
// without the editor having to mutate ECS components by hand.
//
//   * `PropertyInspector` — displays the components attached to an
//     entity and lets the user mutate them. Built on top of a Panel +
//     vertically-stacked Label rows.
//   * `TransformGizmo` — translate handle for a single entity's
//     `cd::scene::LocalTransform`. The gizmo holds three axis-aligned
//     buttons (X / Y / Z); clicking advances the position by a fixed
//     `step` along the corresponding axis.
//     Phase 725: active-axis highlight — clicking an axis sets it as
//     "active" (accent_warning gold + full opacity); inactive axes are
//     dimmed (0.4 alpha). `clear_active_axis()` resets to kNone (Esc).
//   * `SceneTreeView` — Panel + nested labels for the parent → child
//     hierarchy under a Scene root.
//
// Layout is manual (each container sets its child rects on construction
// and on `refresh()`); flex/grid integration is a follow-up.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <cd/ecs/Entity.hpp>
#include <cd/editor/AxisGizmo.hpp>
#include <cd/scene/Scene.hpp>
#include <cd/ui/Widget.hpp>

#include <cstdint>
#include <string>

namespace cd::editor::ui
{

// ---- Property inspector ---------------------------------------------------

/// Two-row inspector: a header label + a body label that lists the
/// inspected entity's transform values. `refresh()` re-reads the entity's
/// LocalTransform — call it once per editor frame (or after a known
/// mutation) so the displayed text stays in sync.
class PropertyInspector : public cd::ui::Panel
{
public:
    PropertyInspector();

    /// Point the inspector at a different entity. Caller is responsible
    /// for ensuring the entity is alive in the bound Scene.
    void set_target(cd::scene::Scene* scene, cd::ecs::Entity e) noexcept;

    /// Refresh the displayed labels from the current target. No-op when
    /// the target entity is dead or no scene is bound.
    void refresh();

    [[nodiscard]] cd::ecs::Entity target() const noexcept
    {
        return target_;
    }

    [[nodiscard]] const std::string& header_text() const noexcept;
    [[nodiscard]] const std::string& body_text() const noexcept;

private:
    cd::scene::Scene* scene_ { nullptr };
    cd::ecs::Entity target_ {};
    cd::ui::Label* header_ { nullptr };
    cd::ui::Label* body_ { nullptr };
};

// ---- Transform gizmo ------------------------------------------------------

/// Three-axis translation gizmo. Each axis click adds `step` to the
/// corresponding `position` coordinate of the target entity's
/// LocalTransform. Click counts are exposed for tests.
class TransformGizmo : public cd::ui::Panel
{
public:
    explicit TransformGizmo(float step = 1.0F);

    void set_target(cd::scene::Scene* scene, cd::ecs::Entity e) noexcept;

    [[nodiscard]] cd::ecs::Entity target() const noexcept
    {
        return target_;
    }

    void set_step(float s) noexcept
    {
        step_ = s;
    }

    [[nodiscard]] float step() const noexcept
    {
        return step_;
    }

    [[nodiscard]] std::size_t total_clicks() const noexcept;

    /// Test helper: invoke the gizmo by simulating a click on a specific axis.
    void click_axis(int axis_index_xyz);  // 0=X, 1=Y, 2=Z

    // ---- Active-axis highlight (Phase 725) --------------------------------

    /// Returns the currently highlighted axis (kNone when no axis is active).
    [[nodiscard]] GizmoAxis active_axis() const noexcept
    {
        return active_axis_;
    }

    /// Programmatically set the active axis and refresh button colours.
    /// Passing kNone is equivalent to `clear_active_axis()`.
    void set_active_axis(GizmoAxis axis) noexcept;

    /// Clear the active axis (Esc behaviour). All buttons return to their
    /// default colours.
    void clear_active_axis() noexcept;

private:
    void apply_delta_(int axis_index_xyz);
    void refresh_axis_colors_() noexcept;

    cd::scene::Scene* scene_ { nullptr };
    cd::ecs::Entity target_ {};
    float step_ { 1.0F };
    GizmoAxis active_axis_ { GizmoAxis::kNone };
    cd::ui::Button* axis_x_ { nullptr };
    cd::ui::Button* axis_y_ { nullptr };
    cd::ui::Button* axis_z_ { nullptr };
};

// ---- Scene tree view ------------------------------------------------------

/// Reads the scene graph from a Scene + root entity and emits one Label
/// per node, indented by depth. `refresh()` rebuilds the label list (the
/// existing children are detached and discarded).
class SceneTreeView : public cd::ui::Panel
{
public:
    SceneTreeView();

    void set_scene(cd::scene::Scene* scene, cd::ecs::Entity root) noexcept;
    void refresh();

    /// Number of label rows currently displayed (for tests).
    [[nodiscard]] std::size_t row_count() const noexcept;

private:
    void add_row_(cd::ecs::Entity e, int depth);

    cd::scene::Scene* scene_ { nullptr };
    cd::ecs::Entity root_ {};
};

}  // namespace cd::editor::ui
