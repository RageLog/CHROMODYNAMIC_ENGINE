// =============================================================================
// CHROMODYNAMIC — cd/world_container/LayerMember.hpp
//
// phase1110 — the ECS half of the Layer model. Layer.hpp's contract
// says "Layer doesn't own entities; the ECS does — entities reference
// the layer by its stable string name". This header supplies that
// reference as a plain ECS component plus the query helpers the
// editor and the streaming ship (v1.7) consume.
//
// Design notes:
//   * The name is stored as cd::core::FixedString<32> — no heap, POD
//     storage, trivially serializable as a string extra in the
//     .cdscene bridge format.
//   * An entity WITHOUT the component is implicitly on "Default"
//     (layer_of() returns that), so existing scenes keep working and
//     the component only costs where the user actually organises.
//   * Helpers are free functions over cd::ecs::World — Scene wraps a
//     World, so both hello_editor (Scene) and raw-World consumers use
//     the same surface.
// =============================================================================
#pragma once

#include <cd/core/FixedString.hpp>
#include <cd/ecs/Entity.hpp>
#include <cd/ecs/World.hpp>

#include <cstddef>
#include <string_view>
#include <utility>

namespace cd::world_container
{

inline constexpr std::string_view kDefaultLayerName { "Default" };

/// ECS component: which Layer (by stable name) this entity belongs to.
struct LayerMember
{
    cd::core::FixedString<32> layer { kDefaultLayerName };
};

/// Assign (or reassign) an entity's layer. Truncates names longer than
/// the FixedString capacity — keep layer names short and stable.
inline void assign_layer(cd::ecs::World& w, cd::ecs::Entity e, std::string_view name)
{
    w.emplace<LayerMember>(e, LayerMember { cd::core::FixedString<32> { name } });
}

/// Layer name for an entity; entities without the component are on
/// "Default" by contract.
[[nodiscard]] inline std::string_view layer_of(const cd::ecs::World& w, cd::ecs::Entity e)
{
    const auto* m = w.get<LayerMember>(e);
    return m != nullptr ? m->layer.view() : kDefaultLayerName;
}

/// Drop the explicit membership (entity falls back to "Default").
inline void clear_layer(cd::ecs::World& w, cd::ecs::Entity e)
{
    w.remove<LayerMember>(e);
}

/// Count entities explicitly on `name`. NOTE: entities on the implicit
/// "Default" (no component) are NOT counted — this counts memberships,
/// not the default population.
[[nodiscard]] inline std::size_t count_members(const cd::ecs::World& w, std::string_view name)
{
    std::size_t n = 0;
    w.for_each<LayerMember>(
        [&](cd::ecs::Entity, const LayerMember& m)
        {
            if (m.layer.view() == name) ++n;
        });
    return n;
}

/// Visit every entity explicitly on `name`. Fn signature: void(Entity).
template <class Fn>
inline void for_each_member(const cd::ecs::World& w, std::string_view name, Fn&& fn)
{
    w.for_each<LayerMember>(
        [&](cd::ecs::Entity e, const LayerMember& m)
        {
            if (m.layer.view() == name) std::forward<Fn>(fn)(e);
        });
}

/// Editor support: a Layer rename must carry its members along.
/// Returns the number of entities migrated.
inline std::size_t rename_layer_members(cd::ecs::World& w,
                                        std::string_view old_name,
                                        std::string_view new_name)
{
    std::size_t n = 0;
    w.for_each<LayerMember>(
        [&](cd::ecs::Entity, LayerMember& m)
        {
            if (m.layer.view() == old_name)
            {
                m.layer = cd::core::FixedString<32> { new_name };
                ++n;
            }
        });
    return n;
}

}  // namespace cd::world_container
