// =============================================================================
// CHROMODYNAMIC — cd/editor/HierarchyView.hpp
// Phase 77.A / Wave 245 — editor hierarchy tree view state.
//
// Tracks which entities are expanded (children visible) in the
// hierarchy panel. Decoupled from rendering: caller iterates the
// flat list returned by `visible_order(scene)` and feeds each
// (entity, depth) into the ImGui tree-node call.
//
//   editor::HierarchyView hv;
//   hv.expand(some_entity);
//   for (auto [e, depth] : hv.visible_order(scene)) {
//       ImGui::TreeNodeEx(...);
//   }
//
// Storage: a small unordered_set of "expanded" entity IDs (default
// is "collapsed").
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <cd/ecs/Entity.hpp>
#include <cd/scene/Scene.hpp>

#include <cstdint>
#include <unordered_set>
#include <utility>
#include <vector>

namespace cd::editor
{

class HierarchyView
{
public:
    void expand(cd::ecs::Entity e) { expanded_.insert(e.id); }
    void collapse(cd::ecs::Entity e) { expanded_.erase(e.id); }

    void toggle(cd::ecs::Entity e)
    {
        auto it = expanded_.find(e.id);
        if (it == expanded_.end()) expanded_.insert(e.id);
        else                       expanded_.erase(it);
    }

    [[nodiscard]] bool is_expanded(cd::ecs::Entity e) const noexcept
    {
        return expanded_.contains(e.id);
    }

    [[nodiscard]] std::size_t expanded_count() const noexcept { return expanded_.size(); }

    /// Walk the scene and emit (entity, depth) for every visible row:
    /// root → its children iff root expanded → their children iff each
    /// is also expanded, recursively.
    [[nodiscard]] std::vector<std::pair<cd::ecs::Entity, std::uint32_t>>
    visible_order(const cd::scene::Scene& scene) const
    {
        std::vector<std::pair<cd::ecs::Entity, std::uint32_t>> out;
        scene.for_each_root(
            [&](cd::ecs::Entity root, cd::scene::LocalTransform&)
            {
                walk(scene, root, 0, out);
            });
        return out;
    }

    void clear() noexcept { expanded_.clear(); }

private:
    void walk(const cd::scene::Scene& scene,
              cd::ecs::Entity e,
              std::uint32_t depth,
              std::vector<std::pair<cd::ecs::Entity, std::uint32_t>>& out) const
    {
        out.emplace_back(e, depth);
        if (!is_expanded(e)) return;
        const auto* kids = scene.children_of(e);
        if (kids == nullptr) return;
        for (auto child : kids->entities) walk(scene, child, depth + 1, out);
    }

    std::unordered_set<std::uint32_t> expanded_;
};

}  // namespace cd::editor
