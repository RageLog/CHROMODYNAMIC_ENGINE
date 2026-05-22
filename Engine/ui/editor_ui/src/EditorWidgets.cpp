// =============================================================================
// CHROMODYNAMIC — cd/editor_ui/EditorWidgets.cpp
// =============================================================================
#include <cd/ecs/World.hpp>
#include <cd/editor_ui/EditorWidgets.hpp>
#include <cd/math/Vector.hpp>
#include <cd/scene/Scene.hpp>
#include <cd/ui/Widget.hpp>

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace cd::editor_ui
{

namespace
{

[[nodiscard]] std::string format_vec3(const cd::math::Vec3f& v)
{
    // Hand-rolled snprintf into a char[] then string — std::format pulls in
    // ~200 KB of object code for a one-liner.
    char buf[64];
    std::snprintf(
        buf,
        sizeof(buf),
        "(%.2f, %.2f, %.2f)",
        static_cast<double>(v.x),
        static_cast<double>(v.y),
        static_cast<double>(v.z)
    );
    return std::string { buf };
}

}  // namespace

// ---- PropertyInspector ----------------------------------------------------

PropertyInspector::PropertyInspector()
{
    set_background({ 0.10F, 0.10F, 0.12F, 1.0F });
    header_ = add_child<cd::ui::Label>("Inspector");
    header_->set_bounds({ 0.0F, 0.0F, 256.0F, 18.0F });
    body_ = add_child<cd::ui::Label>("(no target)");
    body_->set_bounds({ 0.0F, 20.0F, 256.0F, 18.0F });
}

void PropertyInspector::set_target(cd::scene::Scene* scene, cd::ecs::Entity e) noexcept
{
    scene_ = scene;
    target_ = e;
    refresh();
}

void PropertyInspector::refresh()
{
    if (scene_ == nullptr || !scene_->world().is_alive(target_))
    {
        header_->set_text("Inspector");
        body_->set_text("(no target)");
        return;
    }
    header_->set_text("Inspector: entity " + std::to_string(target_.id));
    if (const auto* lt = scene_->local(target_))
    {
        body_->set_text("pos " + format_vec3(lt->value.position) + "  scale " + format_vec3(lt->value.scale));
    }
    else
    {
        body_->set_text("(no LocalTransform)");
    }
}

const std::string& PropertyInspector::header_text() const noexcept
{
    return header_->text();
}

const std::string& PropertyInspector::body_text() const noexcept
{
    return body_->text();
}

// ---- TransformGizmo -------------------------------------------------------

TransformGizmo::TransformGizmo(float step)
    : step_ { step }
{
    set_background({ 0.0F, 0.0F, 0.0F, 0.0F });  // transparent container
    axis_x_ = add_child<cd::ui::Button>("X");
    axis_x_->set_bounds({ 0.0F, 0.0F, 24.0F, 24.0F });
    axis_x_->set_background({ 0.8F, 0.2F, 0.2F, 1.0F });
    axis_x_->set_on_click(
        [this]
        {
            apply_delta_(0);
        }
    );

    axis_y_ = add_child<cd::ui::Button>("Y");
    axis_y_->set_bounds({ 28.0F, 0.0F, 24.0F, 24.0F });
    axis_y_->set_background({ 0.2F, 0.8F, 0.2F, 1.0F });
    axis_y_->set_on_click(
        [this]
        {
            apply_delta_(1);
        }
    );

    axis_z_ = add_child<cd::ui::Button>("Z");
    axis_z_->set_bounds({ 56.0F, 0.0F, 24.0F, 24.0F });
    axis_z_->set_background({ 0.2F, 0.4F, 0.9F, 1.0F });
    axis_z_->set_on_click(
        [this]
        {
            apply_delta_(2);
        }
    );
}

void TransformGizmo::set_target(cd::scene::Scene* scene, cd::ecs::Entity e) noexcept
{
    scene_ = scene;
    target_ = e;
}

std::size_t TransformGizmo::total_clicks() const noexcept
{
    return axis_x_->click_count() + axis_y_->click_count() + axis_z_->click_count();
}

void TransformGizmo::click_axis(int axis_index_xyz)
{
    switch (axis_index_xyz)
    {
        case 0:
            axis_x_->dispatch_click(axis_x_->bounds().x + 1.0F, axis_x_->bounds().y + 1.0F);
            break;
        case 1:
            axis_y_->dispatch_click(axis_y_->bounds().x + 1.0F, axis_y_->bounds().y + 1.0F);
            break;
        case 2:
            axis_z_->dispatch_click(axis_z_->bounds().x + 1.0F, axis_z_->bounds().y + 1.0F);
            break;
        default:
            break;
    }
}

void TransformGizmo::apply_delta_(int axis_index_xyz)
{
    if (scene_ == nullptr)
        return;
    auto* lt = scene_->local(target_);
    if (lt == nullptr)
        return;
    switch (axis_index_xyz)
    {
        case 0:
            lt->value.position.x += step_;
            break;
        case 1:
            lt->value.position.y += step_;
            break;
        case 2:
            lt->value.position.z += step_;
            break;
        default:
            break;
    }
}

// ---- SceneTreeView --------------------------------------------------------

SceneTreeView::SceneTreeView()
{
    set_background({ 0.08F, 0.08F, 0.10F, 1.0F });
}

void SceneTreeView::set_scene(cd::scene::Scene* scene, cd::ecs::Entity root) noexcept
{
    scene_ = scene;
    root_ = root;
    refresh();
}

void SceneTreeView::refresh()
{
    // Rebuild every frame: scene graphs are small in practice (tens of
    // entries) and the alternative — diffing against the previous row set —
    // is more complex than this turn warrants. clear_children() drops every
    // Label we added on the previous refresh; the recursive add_row_ then
    // walks the current Scene depth-first.
    clear_children();
    if (scene_ == nullptr || !scene_->world().is_alive(root_))
        return;
    add_row_(root_, 0);
}

void SceneTreeView::add_row_(cd::ecs::Entity e, int depth)
{
    if (scene_ == nullptr || !scene_->world().is_alive(e))
        return;
    std::string text;
    for (int i = 0; i < depth; ++i)
        text += "  ";
    text += "entity ";
    text += std::to_string(e.id);
    auto* label = add_child<cd::ui::Label>(text);
    const auto row = static_cast<float>(child_count() - 1) * 18.0F;
    label->set_bounds({ 0.0F, row, bounds().w, 18.0F });

    if (const auto* kids = scene_->world().get<cd::scene::Children>(e))
    {
        for (auto child : kids->entities)
        {
            add_row_(child, depth + 1);
        }
    }
}

std::size_t SceneTreeView::row_count() const noexcept
{
    return child_count();
}

}  // namespace cd::editor_ui
