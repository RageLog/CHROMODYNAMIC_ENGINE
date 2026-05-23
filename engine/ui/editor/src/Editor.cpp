// =============================================================================
// CHROMODYNAMIC — cd/editor/Editor.cpp
// =============================================================================
#include <cd/ecs/World.hpp>
#include <cd/editor/Editor.hpp>
#include <cd/input/Input.hpp>
#include <cd/scene/Scene.hpp>

#include <utility>
#include <vector>

namespace cd::editor
{

Editor::Editor(const EditorDesc& desc)
    : desc_ { desc }
{
    // Root panel covers the whole viewport. The editor uses a fixed three-
    // pane layout: scene-tree on the left, inspector on the right,
    // gizmo overlay top-center.
    root_.set_bounds({ 0.0F, 0.0F, desc.width, desc.height });
    root_.set_background({ 0.05F, 0.05F, 0.07F, 1.0F });

    constexpr float kLeftPaneW = 256.0F;
    constexpr float kRightPaneW = 320.0F;

    tree_ = root_.add_child<cd::editor_ui::SceneTreeView>();
    tree_->set_bounds({ 0.0F, 0.0F, kLeftPaneW, desc.height });

    inspector_ = root_.add_child<cd::editor_ui::PropertyInspector>();
    inspector_->set_bounds({ desc.width - kRightPaneW, 0.0F, kRightPaneW, desc.height });

    gizmo_ = root_.add_child<cd::editor_ui::TransformGizmo>(1.0F);
    gizmo_->set_bounds({ (desc.width - 84.0F) * 0.5F, 8.0F, 84.0F, 24.0F });

    // Default scene: a single root node that the user grows by attaching
    // entities to it. We point the inspector + tree at it on the first
    // tick.
    scene_root_ = scene_.create_node();
    tree_->set_scene(&scene_, scene_root_);
}

void Editor::select(cd::ecs::Entity e)
{
    selection_ = e;
    inspector_->set_target(&scene_, e);
    gizmo_->set_target(&scene_, e);
}

void Editor::dispatch_input_()
{
    // Drain queued events and route every mouse-button-down event to the
    // widget tree. The state-machine half (polled state, wheel accum) is
    // already kept current by InputContext::push_event.
    auto events = input_.drain_events();
    for (const auto& e : events)
    {
        if (e.kind == cd::input::EventKind::kMouseButtonDown && e.mouse_button == cd::input::MouseButton::kLeft)
        {
            root_.dispatch_click(e.mouse_x, e.mouse_y);
        }
    }
}

std::vector<cd::ui::DrawCommand> Editor::tick()
{
    dispatch_input_();
    inspector_->refresh();
    tree_->refresh();

    std::vector<cd::ui::DrawCommand> cmds;
    root_.collect_draw_commands(cmds);
    return cmds;
}

}  // namespace cd::editor
