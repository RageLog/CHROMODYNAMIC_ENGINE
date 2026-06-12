// =============================================================================
// CHROMODYNAMIC — cd/scene/Scene.hpp
// Phase 4 / Sprint S4.2 — hierarchical transforms on top of cd::ecs.
//
// A Scene layers parent-child relationships and world-space transforms on
// top of a `cd::ecs::World`. The ECS holds the authoritative entity store;
// Scene just adds the components and the propagation pass.
//
// Components (registered automatically the first time they're used):
//   * `cd::scene::LocalTransform` — TRS in the parent's frame. Engines
//     mutate this every frame; it is the editable representation.
//   * `cd::scene::WorldTransform` — Mat4 in world space, refreshed by
//     `update_transforms()` from the local + ancestor transforms.
//   * `cd::scene::Parent`        — non-owning reference to the parent
//     entity. Root nodes have no Parent component.
//   * `cd::scene::Children`      — list of direct children. Maintained by
//     attach()/detach() so renderers can do top-down traversal cheaply.
//
// Design notes (SOTA references: production engine's Transform Hierarchy [GDC 2018],
// USceneComponent, godot::Node3D):
//   * Transform propagation is a *single* depth-first walk from each root,
//     running in registration order — no cycles allowed.
//   * The world matrix is column-major (matches Vulkan/D3D upload layout).
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <cd/ecs/Entity.hpp>
#include <cd/ecs/World.hpp>
#include <cd/math/Matrix.hpp>
#include <cd/math/Transform.hpp>

#include <cstddef>
#include <vector>

namespace cd::scene
{

// ---- Scene-graph components ------------------------------------------------

/// Per-entity TRS in its parent's frame. Identity by default.
struct LocalTransform
{
    cd::math::Transformf value {};
};

/// Per-entity world-space matrix, computed each frame by Scene::update_transforms.
struct WorldTransform
{
    cd::math::Mat4f matrix { cd::math::Mat4f::identity() };
};

/// Non-owning parent reference. Absent for root nodes.
struct Parent
{
    cd::ecs::Entity entity {};
};

/// Ordered list of direct children. Maintained by attach()/detach().
struct Children
{
    std::vector<cd::ecs::Entity> entities {};
};

// ---- Scene ----------------------------------------------------------------

class Scene
{
public:
    /// Construct a Scene that operates on `world`. The world must outlive
    /// the Scene.
    explicit Scene(cd::ecs::World& world) noexcept
        : world_ { &world }
    {
    }

    /// Create a new scene node with an identity LocalTransform and an
    /// identity WorldTransform pre-installed. Root-level by default — call
    /// `attach()` to put it under a parent.
    [[nodiscard]] cd::ecs::Entity create_node();

    /// Destroy a node *and all of its descendants*. The ECS handles
    /// component cleanup; we just walk the children list first.
    void destroy_node(cd::ecs::Entity node);

    /// Make `child` a direct child of `parent`. Both must already exist and
    /// be live. A child can only have one parent; if it already has a
    /// different parent the old link is removed automatically. Returns
    /// false when either entity is dead.
    bool attach(cd::ecs::Entity child, cd::ecs::Entity parent);

    /// Detach `child` from its current parent (promotes to root). No-op if
    /// the child has no parent.
    void detach(cd::ecs::Entity child);

    /// Walk every root node and refresh WorldTransform from
    /// LocalTransform · parent_world. Top-down so children see fresh
    /// ancestor matrices in O(N).
    void update_transforms();

    /// Convenience accessors so callers do not need to round-trip through
    /// the ECS just to read a transform.
    [[nodiscard]] const LocalTransform* local(cd::ecs::Entity e) const
    {
        return world_->get<LocalTransform>(e);
    }

    [[nodiscard]] LocalTransform* local(cd::ecs::Entity e)
    {
        return world_->get<LocalTransform>(e);
    }

    [[nodiscard]] const WorldTransform* world_transform(cd::ecs::Entity e) const
    {
        return world_->get<WorldTransform>(e);
    }

    [[nodiscard]] cd::ecs::Entity parent_of(cd::ecs::Entity e) const
    {
        auto* p = world_->get<Parent>(e);
        return p != nullptr ? p->entity : cd::ecs::Entity {};
    }

    [[nodiscard]] cd::ecs::World& world() noexcept
    {
        return *world_;
    }

    [[nodiscard]] const cd::ecs::World& world() const noexcept
    {
        return *world_;
    }

    /// Visit every scene node — i.e. every entity that carries a
    /// LocalTransform component. Order is the ECS for_each order
    /// (stable across a frame, not necessarily stable across runs).
    template <class Fn>
    void for_each_node(Fn&& fn) const
    {
        world_->for_each<LocalTransform>(
            [&](cd::ecs::Entity e, LocalTransform& lt)
            {
                fn(e, lt);
            }
        );
    }

    /// Visit every root node (no `Parent` component).
    template <class Fn>
    void for_each_root(Fn&& fn) const
    {
        world_->for_each<LocalTransform>(
            [&](cd::ecs::Entity e, LocalTransform& lt)
            {
                if (world_->get<Parent>(e) == nullptr)
                    fn(e, lt);
            }
        );
    }

    /// Depth-first walk of `root` and all its descendants. `fn(entity,
    /// depth)` is called for `root` (depth=0) then for each child
    /// recursively. Cycles are not possible by construction (attach()
    /// rejects them) — we still cap recursion at 256 levels as a safety
    /// fallback against malformed data.
    template <class Fn>
    void for_each_descendant(cd::ecs::Entity root, Fn&& fn) const
    {
        // descend_ takes `Fn&`; binding the named parameter (which is an
        // lvalue here) to that reference works whether the caller passed
        // an rvalue lambda or an lvalue callable.
        descend(root, 0u, fn);
    }

    /// Direct children of `e` in attach() order. Returns nullptr if `e`
    /// has no children (no Children component installed yet).
    [[nodiscard]] const Children* children_of(cd::ecs::Entity e) const
    {
        return world_->get<Children>(e);
    }

private:
    void destroy_subtree(cd::ecs::Entity node);
    void propagate(cd::ecs::Entity node, const cd::math::Mat4f& parent_world);

    template <class Fn>
    void descend(cd::ecs::Entity node, std::uint32_t depth, Fn& fn) const
    {
        constexpr std::uint32_t kMaxDepth = 256;
        if (depth > kMaxDepth)
            return;
        fn(node, depth);
        const auto* ch = world_->get<Children>(node);
        if (ch == nullptr)
            return;
        for (const auto& c : ch->entities)
            descend(c, depth + 1, fn);
    }

    cd::ecs::World* world_ { nullptr };
};

}  // namespace cd::scene
