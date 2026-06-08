// =============================================================================
// CHROMODYNAMIC — cd/scene/Scene.cpp
// =============================================================================
#include <cd/math/Transform.hpp>
#include <cd/scene/Scene.hpp>

#include <algorithm>
#include <cstddef>
#include <vector>

namespace cd::scene
{

cd::ecs::Entity Scene::create_node()
{
    auto e = world_->create();
    world_->emplace<LocalTransform>(e, LocalTransform {});
    world_->emplace<WorldTransform>(e, WorldTransform {});
    return e;
}

void Scene::destroy_node(cd::ecs::Entity node)
{
    if (!world_->is_alive(node))
        return;
    // Detach from parent first so its Children list does not retain the
    // dead handle.
    detach(node);
    destroy_subtree_(node);
}

void Scene::destroy_subtree_(cd::ecs::Entity node)
{
    // Copy the children list before iterating: destroy_node() walks it via
    // detach() which mutates the Children component we're traversing.
    std::vector<cd::ecs::Entity> kids;
    if (auto* c = world_->get<Children>(node))
    {
        kids = c->entities;
    }
    for (auto child : kids)
    {
        destroy_subtree_(child);
    }
    world_->destroy(node);
}

bool Scene::attach(cd::ecs::Entity child, cd::ecs::Entity parent)
{
    if (!world_->is_alive(child) || !world_->is_alive(parent))
        return false;
    if (child == parent)
        return false;
    // Remove from old parent's child list, if any.
    detach(child);
    world_->emplace<Parent>(child, Parent { parent });

    auto* kids = world_->get<Children>(parent);
    if (kids == nullptr)
    {
        Children c;
        c.entities.push_back(child);
        world_->emplace<Children>(parent, std::move(c));
    }
    else
    {
        kids->entities.push_back(child);
    }
    return true;
}

void Scene::detach(cd::ecs::Entity child)
{
    auto* p = world_->get<Parent>(child);
    if (p == nullptr)
        return;
    const auto parent_e = p->entity;
    auto* kids = world_->get<Children>(parent_e);
    if (kids != nullptr)
    {
        auto& v = kids->entities;
        const auto removed = std::ranges::remove(v, child);
        v.erase(removed.begin(), removed.end());
        if (v.empty())
            world_->remove<Children>(parent_e);
    }
    world_->remove<Parent>(child);
}

void Scene::propagate_(cd::ecs::Entity node, const cd::math::Mat4f& parent_world)
{
    // Compose: world = parent_world · local
    const auto* lt = world_->get<LocalTransform>(node);
    if (lt == nullptr)
        return;
    const cd::math::Mat4f local_mat = cd::math::to_mat4(lt->value);
    const cd::math::Mat4f node_world = parent_world * local_mat;
    if (auto* wt = world_->get<WorldTransform>(node))
    {
        wt->matrix = node_world;
    }
    if (auto* kids = world_->get<Children>(node))
    {
        // Local copy; recursive calls may mutate the parent's component list.
        const auto child_copy = kids->entities;
        for (auto c : child_copy)
        {
            propagate_(c, node_world);
        }
    }
}

void Scene::update_transforms()
{
    // Find roots: entities with LocalTransform but no Parent. Single pool
    // walk: every node in the scene has a LocalTransform.
    std::vector<cd::ecs::Entity> roots;
    world_->for_each<LocalTransform>(
        [&](cd::ecs::Entity e, LocalTransform&)
        {
            if (world_->get<Parent>(e) == nullptr)
                roots.push_back(e);
        }
    );
    const auto identity = cd::math::Mat4f::identity();
    for (auto r : roots)
    {
        propagate_(r, identity);
    }
}

}  // namespace cd::scene
