// =============================================================================
// CHROMODYNAMIC — cd/editor/Editor.hpp
// Phase 5 / Sprint S5.3 — editor application shell.
//
// `Editor` is the integration layer that wires the engine's tiers into a
// single editable session:
//
//   * Owns the authoritative `cd::ecs::World` + `cd::scene::Scene`.
//   * Builds a root `cd::ui::Panel` and populates it with editor_ui
//     widgets (PropertyInspector, TransformGizmo, SceneTreeView).
//   * Routes pointer events through the widget tree (delegating to
//     `cd::input::InputContext`) so the same input plumbing the game
//     runtime uses also drives the editor.
//   * Provides `tick()` — refresh widgets, run game systems if any, emit
//     draw commands ready for the engine's render frontend.
//
// The editor is intentionally renderer-agnostic. A `draw()` consumer
// (cd_sample_editor or an embedded viewport) consumes the DrawCommand
// vector that `tick()` returns.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <cd/ecs/World.hpp>
#include <cd/editor_ui/EditorWidgets.hpp>
#include <cd/input/Input.hpp>
#include <cd/scene/Scene.hpp>
#include <cd/ui/Widget.hpp>

#include <cstddef>
#include <vector>

namespace cd::editor
{

struct EditorDesc
{
    /// Viewport size in pixels. Drives the root panel bounds.
    float width { 1280.0F };
    float height { 720.0F };
};

class Editor
{
public:
    explicit Editor(const EditorDesc& desc = {});
    ~Editor() = default;
    Editor(const Editor&) = delete;
    Editor& operator=(const Editor&) = delete;
    Editor(Editor&&) = delete;
    Editor& operator=(Editor&&) = delete;

    // ---- World / scene access (engines wire game systems off these) ----

    [[nodiscard]] cd::ecs::World& world() noexcept
    {
        return world_;
    }

    [[nodiscard]] cd::scene::Scene& scene() noexcept
    {
        return scene_;
    }

    [[nodiscard]] cd::input::InputContext& input() noexcept
    {
        return input_;
    }

    /// Root scene node — every editable entity should live under it.
    [[nodiscard]] cd::ecs::Entity scene_root() const noexcept
    {
        return scene_root_;
    }

    // ---- Selection -----------------------------------------------------

    /// Point the inspector and gizmo at a specific entity. Pass an empty
    /// handle to clear the selection.
    void select(cd::ecs::Entity e);

    [[nodiscard]] cd::ecs::Entity selection() const noexcept
    {
        return selection_;
    }

    // ---- Per-frame tick ------------------------------------------------

    /// Drain queued input events, refresh widgets, and collect draw
    /// commands. Returns the command vector ready for a renderer frontend.
    std::vector<cd::ui::DrawCommand> tick();

    // ---- Introspection (tests) ----------------------------------------

    [[nodiscard]] cd::ui::Widget& root() noexcept
    {
        return root_;
    }

    [[nodiscard]] const cd::editor_ui::PropertyInspector& inspector() const noexcept
    {
        return *inspector_;
    }

    [[nodiscard]] const cd::editor_ui::TransformGizmo& gizmo() const noexcept
    {
        return *gizmo_;
    }

    [[nodiscard]] const cd::editor_ui::SceneTreeView& tree_view() const noexcept
    {
        return *tree_;
    }

private:
    void dispatch_input_();

    EditorDesc desc_ {};
    cd::ecs::World world_ {};
    cd::scene::Scene scene_ { world_ };
    cd::ecs::Entity scene_root_ {};
    cd::ecs::Entity selection_ {};
    cd::input::InputContext input_ {};

    cd::ui::Panel root_ {};
    cd::editor_ui::PropertyInspector* inspector_ { nullptr };
    cd::editor_ui::TransformGizmo* gizmo_ { nullptr };
    cd::editor_ui::SceneTreeView* tree_ { nullptr };
};

}  // namespace cd::editor
