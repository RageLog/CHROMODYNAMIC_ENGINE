// =============================================================================
// CHROMODYNAMIC — cd/scene/Trigger.hpp
// Phase 95.A / Wave 263 — named trigger volume.
//
// `Trigger` is a non-physical AABB region in the scene that fires
// caller-supplied callbacks when an entity enters / exits. Gameplay
// layer drives `update(entity, position)`; the trigger transitions
// "outside" → "inside" → fires on_enter; later "inside" → "outside"
// fires on_exit.
//
// Single-entity tracker for simplicity; multi-entity occupancy is
// caller's job (vector<Trigger> + lookup table).
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <cd/ecs/Entity.hpp>
#include <cd/math/Vector.hpp>
#include <cd/physics/Aabb.hpp>

#include <functional>
#include <string>

namespace cd::scene
{

class Trigger
{
public:
    Trigger(std::string name, const cd::physics::Aabb& volume)
        : name_ { std::move(name) }, volume_ { volume } {}

    void set_on_enter(std::function<void(cd::ecs::Entity)> fn) { on_enter_ = std::move(fn); }
    void set_on_exit (std::function<void(cd::ecs::Entity)> fn) { on_exit_  = std::move(fn); }

    /// Drive transitions from gameplay tick. `pos` is the entity's
    /// world-space position. State machine fires enter/exit exactly
    /// once on boundary crossing.
    void update(cd::ecs::Entity e, const cd::math::Vec3f& pos)
    {
        const bool inside_now = cd::physics::contains(volume_, pos);
        if (inside_now && !inside_)
        {
            inside_ = true;
            current_entity_ = e;
            if (on_enter_) on_enter_(e);
        }
        else if (!inside_now && inside_)
        {
            inside_ = false;
            if (on_exit_) on_exit_(current_entity_);
            current_entity_ = cd::ecs::Entity {};
        }
    }

    [[nodiscard]] const std::string&         name()   const noexcept { return name_; }
    [[nodiscard]] const cd::physics::Aabb&   volume() const noexcept { return volume_; }
    [[nodiscard]] bool                       is_occupied() const noexcept { return inside_; }
    [[nodiscard]] cd::ecs::Entity            occupant() const noexcept { return current_entity_; }

private:
    std::string                                 name_;
    cd::physics::Aabb                           volume_;
    std::function<void(cd::ecs::Entity)>        on_enter_;
    std::function<void(cd::ecs::Entity)>        on_exit_;
    cd::ecs::Entity                             current_entity_ {};
    bool                                        inside_ { false };
};

}  // namespace cd::scene
