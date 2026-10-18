// =============================================================================
// CHROMODYNAMIC — cd/scene/ElementCapsuleTree.hpp
// Unified Nested Element & TraitCapsule Hierarchy (Beyond-Godot Scene Graph)
//
// Core architectural primitives:
//   * `Element` (`SceneElement`): Self-contained scene building block across 7
//     canonical domains (`kCoreStage`, `kPlanar2D`, `kSpatial3D`, `kVessel`,
//     `kRadianceAether`, `kControlUi`, `kNextGenSota`).
//   * `TraitCapsule`: Lightweight inline capability attached directly to an
//     Element without polluting the scene hierarchy tree with dummy sub-nodes.
//   * Seamless Fold / Unfold: Any inline `TraitCapsule` can be unfolded into a
//     full nested child `Element` when deep sub-tree nesting is needed, and any
//     leaf child `Element` can be folded back into its parent's inline capsule
//     list.
//   * `PropagationMask`: Per-link selective inheritance bitmask controlling
//     whether transform, time-dilation (`world_time_scale`), visibility, tint,
//     and physics group propagate from parent to child.
//   * `Vessel` & `PilotMode`: First-class controllable entity container that
//     can be embarked by a human player, AI synth, or remote network peer.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace cd::scene
{

using ElementId = std::uint32_t;
inline constexpr ElementId kInvalidElementId = 0u;

enum class ElementDomain : std::uint8_t
{
    kCoreStage = 0,
    kPlanar2D,
    kSpatial3D,
    kVessel,
    kRadianceAether,
    kControlUi,
    kNextGenSota,
};

enum class PilotMode : std::uint8_t
{
    kNone = 0,
    kHumanPilot,
    kSynthPilot,
    kRemoteNetPilot,
};

enum class PropagationMask : std::uint32_t
{
    kNone         = 0u,
    kTransform    = 1u << 0u,
    kTimeScale    = 1u << 1u,
    kVisibility   = 1u << 2u,
    kTint         = 1u << 3u,
    kPhysicsGroup = 1u << 4u,
    kAll          = kTransform | kTimeScale | kVisibility | kTint | kPhysicsGroup,
};

[[nodiscard]] constexpr PropagationMask operator|(PropagationMask a, PropagationMask b) noexcept
{
    return static_cast<PropagationMask>(
        static_cast<std::uint32_t>(a) | static_cast<std::uint32_t>(b));
}

[[nodiscard]] constexpr PropagationMask operator&(PropagationMask a, PropagationMask b) noexcept
{
    return static_cast<PropagationMask>(
        static_cast<std::uint32_t>(a) & static_cast<std::uint32_t>(b));
}

[[nodiscard]] constexpr PropagationMask operator^(PropagationMask a, PropagationMask b) noexcept
{
    return static_cast<PropagationMask>(
        static_cast<std::uint32_t>(a) ^ static_cast<std::uint32_t>(b));
}

[[nodiscard]] constexpr PropagationMask operator~(PropagationMask a) noexcept
{
    return static_cast<PropagationMask>(~static_cast<std::uint32_t>(a));
}

constexpr PropagationMask& operator|=(PropagationMask& a, PropagationMask b) noexcept
{
    a = a | b;
    return a;
}

constexpr PropagationMask& operator&=(PropagationMask& a, PropagationMask b) noexcept
{
    a = a & b;
    return a;
}

[[nodiscard]] constexpr bool has_propagation(PropagationMask mask, PropagationMask flag) noexcept
{
    return (static_cast<std::uint32_t>(mask) & static_cast<std::uint32_t>(flag)) != 0u;
}

struct TraitCapsule
{
    std::string capsule_type {};
    std::string godot_equivalent {};
    bool        enabled { true };
    float       local_offset[3] { 0.0F, 0.0F, 0.0F };
};

struct Element
{
    ElementId                  id { kInvalidElementId };
    std::string                name {};
    ElementDomain              domain { ElementDomain::kSpatial3D };
    std::string                archetype_type {};
    ElementId                  parent_id { kInvalidElementId };
    std::vector<ElementId>     children {};
    PropagationMask            propagation_mask { PropagationMask::kAll };
    PilotMode                  pilot_mode { PilotMode::kNone };
    bool                       is_vessel { false };

    // Local spatial, temporal & visual state.
    float                      local_pos[3] { 0.0F, 0.0F, 0.0F };
    float                      local_scale[3] { 1.0F, 1.0F, 1.0F };
    float                      local_time_scale { 1.0F };
    bool                       visible { true };
    float                      local_tint[4] { 1.0F, 1.0F, 1.0F, 1.0F };
    std::uint32_t              physics_group { 0u };

    // Propagated world / effective state computed by `propagate_hierarchy()`.
    float                      world_pos[3] { 0.0F, 0.0F, 0.0F };
    float                      world_scale[3] { 1.0F, 1.0F, 1.0F };
    float                      world_time_scale { 1.0F };
    bool                       effective_visible { true };
    float                      world_tint[4] { 1.0F, 1.0F, 1.0F, 1.0F };
    std::uint32_t              effective_physics_group { 0u };

    // Inline capability capsules embedded directly inside this Element.
    std::vector<TraitCapsule>  inline_capsules {};
};

using SceneElement = Element;

class ElementCapsuleTree
{
public:
    [[nodiscard]] ElementId create_element(
        std::string_view name,
        ElementDomain    domain = ElementDomain::kSpatial3D)
    {
        const ElementId id = next_id_++;
        Element elem {};
        elem.id        = id;
        elem.name      = std::string(name);
        elem.domain    = domain;
        elem.is_vessel = (domain == ElementDomain::kVessel);
        elements_.emplace(id, std::move(elem));
        return id;
    }

    [[nodiscard]] ElementId create_vessel(
        std::string_view name,
        PilotMode        mode = PilotMode::kHumanPilot)
    {
        const ElementId id = create_element(name, ElementDomain::kVessel);
        auto* elem = find(id);
        if (elem != nullptr)
        {
            elem->is_vessel  = true;
            elem->pilot_mode = mode;
        }
        return id;
    }

    [[nodiscard]] bool nest_child(
        ElementId       parent_id,
        ElementId       child_id,
        PropagationMask mask = PropagationMask::kAll)
    {
        if (parent_id == kInvalidElementId || child_id == kInvalidElementId)
            return false;
        if (parent_id == child_id)
            return false;

        auto* parent = find(parent_id);
        auto* child  = find(child_id);
        if (parent == nullptr || child == nullptr)
            return false;

        // Cycle guard: reject if `child_id` is already an ancestor of `parent_id`.
        if (is_ancestor(child_id, parent_id))
            return false;

        // Detach from previous parent if already nested elsewhere.
        if (child->parent_id != kInvalidElementId && child->parent_id != parent_id)
        {
            if (auto* old_parent = find(child->parent_id); old_parent != nullptr)
            {
                auto& siblings = old_parent->children;
                siblings.erase(std::ranges::remove(siblings, child_id).begin(), siblings.end());
            }
        }

        child->parent_id        = parent_id;
        child->propagation_mask = mask;

        if (std::ranges::find(parent->children, child_id) == parent->children.end())
        {
            parent->children.push_back(child_id);
        }
        return true;
    }

    bool detach_from_parent(ElementId child_id)
    {
        auto* child = find(child_id);
        if (child == nullptr || child->parent_id == kInvalidElementId)
            return false;

        if (auto* parent = find(child->parent_id); parent != nullptr)
        {
            auto& siblings = parent->children;
            siblings.erase(std::ranges::remove(siblings, child_id).begin(), siblings.end());
        }
        child->parent_id = kInvalidElementId;
        return true;
    }

    bool add_inline_capsule(ElementId element_id, TraitCapsule capsule)
    {
        auto* elem = find(element_id);
        if (elem == nullptr)
            return false;
        elem->inline_capsules.push_back(std::move(capsule));
        return true;
    }

    // Unfolds an inline `TraitCapsule` at `capsule_index` into a full nested
    // child `Element` under `element_id`.
    [[nodiscard]] ElementId unfold_capsule_to_nested_child(
        ElementId   element_id,
        std::size_t capsule_index)
    {
        auto* parent = find(element_id);
        if (parent == nullptr || capsule_index >= parent->inline_capsules.size())
            return kInvalidElementId;

        TraitCapsule cap = std::move(parent->inline_capsules[capsule_index]);
        parent->inline_capsules.erase(
            parent->inline_capsules.begin() + static_cast<std::ptrdiff_t>(capsule_index));

        const ElementDomain child_domain =
            (parent->domain == ElementDomain::kVessel) ? ElementDomain::kSpatial3D : parent->domain;
        const std::string child_name =
            cap.capsule_type.empty() ? "UnfoldedCapsule" : cap.capsule_type;

        const ElementId child_id = create_element(child_name, child_domain);
        auto* child = find(child_id);
        if (child == nullptr)
            return kInvalidElementId;

        child->archetype_type = std::move(cap.godot_equivalent);
        child->visible        = cap.enabled;
        child->local_pos[0]   = cap.local_offset[0];
        child->local_pos[1]   = cap.local_offset[1];
        child->local_pos[2]   = cap.local_offset[2];

        (void)nest_child(element_id, child_id, PropagationMask::kAll);
        return child_id;
    }

    // Folds a leaf nested child `Element` back into its parent's inline
    // `TraitCapsule` list and removes the child Element from the hierarchy.
    [[nodiscard]] bool fold_nested_child_into_capsule(ElementId child_id)
    {
        auto* child = find(child_id);
        if (child == nullptr || child->parent_id == kInvalidElementId)
            return false;
        if (!child->children.empty())
            return false;

        const ElementId parent_id = child->parent_id;
        auto* parent = find(parent_id);
        if (parent == nullptr)
            return false;

        TraitCapsule cap {};
        cap.capsule_type     = child->name;
        cap.godot_equivalent = child->archetype_type;
        cap.enabled          = child->visible;
        cap.local_offset[0]  = child->local_pos[0];
        cap.local_offset[1]  = child->local_pos[1];
        cap.local_offset[2]  = child->local_pos[2];

        // Fold any inline capsules that lived on the child into the parent as well.
        std::vector<TraitCapsule> grand_capsules = std::move(child->inline_capsules);

        auto& siblings = parent->children;
        siblings.erase(std::ranges::remove(siblings, child_id).begin(), siblings.end());

        parent->inline_capsules.push_back(std::move(cap));
        for (auto& gc : grand_capsules)
        {
            parent->inline_capsules.push_back(std::move(gc));
        }

        elements_.erase(child_id);
        return true;
    }

    bool embark_pilot(ElementId vessel_id, PilotMode mode)
    {
        if (mode == PilotMode::kNone)
            return false;
        auto* elem = find(vessel_id);
        if (elem == nullptr)
            return false;
        elem->is_vessel  = true;
        elem->pilot_mode = mode;
        return true;
    }

    bool disembark_pilot(ElementId vessel_id)
    {
        auto* elem = find(vessel_id);
        if (elem == nullptr)
            return false;
        elem->pilot_mode = PilotMode::kNone;
        return true;
    }

    void propagate_hierarchy()
    {
        for (auto& [id, elem] : elements_)
        {
            if (elem.parent_id == kInvalidElementId)
            {
                propagate_from_root(elem);
            }
        }
    }

    [[nodiscard]] Element* find(ElementId id) noexcept
    {
        auto it = elements_.find(id);
        return (it != elements_.end()) ? &it->second : nullptr;
    }

    [[nodiscard]] const Element* find(ElementId id) const noexcept
    {
        auto it = elements_.find(id);
        return (it != elements_.end()) ? &it->second : nullptr;
    }

    [[nodiscard]] Element* get(ElementId id) noexcept { return find(id); }

    [[nodiscard]] const Element* get(ElementId id) const noexcept { return find(id); }

    [[nodiscard]] std::size_t element_count() const noexcept { return elements_.size(); }

    [[nodiscard]] std::size_t size() const noexcept { return elements_.size(); }

    [[nodiscard]] std::size_t total_inline_capsule_count() const noexcept
    {
        std::size_t total = 0u;
        for (const auto& [id, elem] : elements_)
        {
            total += elem.inline_capsules.size();
        }
        return total;
    }

    [[nodiscard]] std::size_t piloted_vessel_count() const noexcept
    {
        std::size_t count = 0u;
        for (const auto& [id, elem] : elements_)
        {
            if (elem.is_vessel && elem.pilot_mode != PilotMode::kNone)
            {
                ++count;
            }
        }
        return count;
    }

    [[nodiscard]] std::vector<ElementId> root_elements() const
    {
        std::vector<ElementId> roots;
        for (const auto& [id, elem] : elements_)
        {
            if (elem.parent_id == kInvalidElementId)
            {
                roots.push_back(id);
            }
        }
        std::ranges::sort(roots);
        return roots;
    }

    void clear() noexcept
    {
        elements_.clear();
        next_id_ = 1u;
    }

private:
    [[nodiscard]] bool is_ancestor(ElementId potential_ancestor, ElementId node_id) const noexcept
    {
        ElementId cursor = node_id;
        std::size_t guard = 0u;
        const std::size_t limit = elements_.size() + 1u;
        while (cursor != kInvalidElementId && guard < limit)
        {
            if (cursor == potential_ancestor)
                return true;
            const auto* elem = find(cursor);
            if (elem == nullptr)
                break;
            cursor = elem->parent_id;
            ++guard;
        }
        return false;
    }

    void propagate_from_root(Element& root)
    {
        for (std::size_t i = 0u; i < 3u; ++i)
        {
            root.world_pos[i]   = root.local_pos[i];
            root.world_scale[i] = root.local_scale[i];
        }
        for (std::size_t i = 0u; i < 4u; ++i)
        {
            root.world_tint[i] = root.local_tint[i];
        }
        root.world_time_scale        = root.local_time_scale;
        root.effective_visible       = root.visible;
        root.effective_physics_group = root.physics_group;

        for (const ElementId child_id : root.children)
        {
            if (auto* child = find(child_id); child != nullptr)
            {
                propagate_child(root, *child);
            }
        }
    }

    void propagate_child(const Element& parent, Element& child)
    {
        const PropagationMask mask = child.propagation_mask;

        if (has_propagation(mask, PropagationMask::kTransform))
        {
            for (std::size_t i = 0u; i < 3u; ++i)
            {
                child.world_pos[i]   = parent.world_pos[i] + child.local_pos[i] * parent.world_scale[i];
                child.world_scale[i] = parent.world_scale[i] * child.local_scale[i];
            }
        }
        else
        {
            for (std::size_t i = 0u; i < 3u; ++i)
            {
                child.world_pos[i]   = child.local_pos[i];
                child.world_scale[i] = child.local_scale[i];
            }
        }

        if (has_propagation(mask, PropagationMask::kTimeScale))
        {
            child.world_time_scale = parent.world_time_scale * child.local_time_scale;
        }
        else
        {
            child.world_time_scale = child.local_time_scale;
        }

        if (has_propagation(mask, PropagationMask::kVisibility))
        {
            child.effective_visible = parent.effective_visible && child.visible;
        }
        else
        {
            child.effective_visible = child.visible;
        }

        if (has_propagation(mask, PropagationMask::kTint))
        {
            for (std::size_t i = 0u; i < 4u; ++i)
            {
                child.world_tint[i] = parent.world_tint[i] * child.local_tint[i];
            }
        }
        else
        {
            for (std::size_t i = 0u; i < 4u; ++i)
            {
                child.world_tint[i] = child.local_tint[i];
            }
        }

        if (has_propagation(mask, PropagationMask::kPhysicsGroup))
        {
            child.effective_physics_group =
                (child.physics_group != 0u) ? child.physics_group : parent.effective_physics_group;
        }
        else
        {
            child.effective_physics_group = child.physics_group;
        }

        for (const ElementId grandchild_id : child.children)
        {
            if (auto* grandchild = find(grandchild_id); grandchild != nullptr)
            {
                propagate_child(child, *grandchild);
            }
        }
    }

    std::unordered_map<ElementId, Element> elements_ {};
    ElementId                              next_id_ { 1u };
};

}  // namespace cd::scene
