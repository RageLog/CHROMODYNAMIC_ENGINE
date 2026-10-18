// =============================================================================
// CHROMODYNAMIC — cd/physics/RockstarPhysicsWorldAdapter.hpp
// Module #33: Concrete `cd::physics::IPhysicsWorld` implementation backed by
// the 6-DOF `cd::physics::v3d::PhysicsWorld3D` (DynamicBvh3D + GJK/EPA + 15-axis
// OBB SAT + Temporal Gauss-Seidel friction-cone contact solver).
// =============================================================================
#pragma once

#include <cd/physics/IPhysicsWorld.hpp>
#include <cd/physics/UnifiedPhysicsEngine.hpp>

#include <memory>
#include <unordered_map>

namespace cd::physics
{

class RockstarPhysicsWorldAdapter final : public IPhysicsWorld
{
public:
    RockstarPhysicsWorldAdapter() = default;
    ~RockstarPhysicsWorldAdapter() override = default;

    void set_gravity(cd::math::Vec3f g) noexcept override
    {
        engine_.world_3d().set_gravity(g);
    }

    [[nodiscard]] cd::math::Vec3f gravity() const noexcept override
    {
        return engine_.world_3d().gravity();
    }

    [[nodiscard]] cd::core::Result<BodyHandle> create_body(const BodyDesc& desc) override
    {
        if (desc.type == BodyType::kDynamic && desc.mass <= 0.0F)
        {
            return std::unexpected(
                physics_errors::make(
                    physics_errors::Code::kInvalidArgument,
                    "create_body: dynamic body must have mass > 0"));
        }

        v3d::RigidBody3D body {};
        switch (desc.type)
        {
            case BodyType::kStatic:
                body.motion_type = core::BodyMotionType::kStatic;
                break;
            case BodyType::kKinematic:
                body.motion_type = core::BodyMotionType::kKinematic;
                break;
            case BodyType::kDynamic:
                body.motion_type = core::BodyMotionType::kDynamic;
                break;
        }
        body.transform.position = desc.position;
        body.linear_velocity    = desc.linear_velocity;
        body.mass               = (desc.type == BodyType::kDynamic) ? desc.mass : 0.0F;
        body.linear_damping     = desc.linear_damping;
        body.shape              = v3d::SphereShape3D { 0.5F };

        const std::uint32_t id = engine_.world_3d().create_body(body);
        handle_types_[id]      = desc.type;
        return BodyHandle { id, 1u };
    }

    void destroy_body(BodyHandle h) override
    {
        (void)engine_.world_3d().remove_body(h.index());
        handle_types_.erase(h.index());
    }

    [[nodiscard]] std::size_t body_count() const noexcept override
    {
        return engine_.world_3d().body_count();
    }

    [[nodiscard]] cd::math::Vec3f position(BodyHandle h) const override
    {
        if (const auto* b = engine_.world_3d().get_body(h.index()))
        {
            return b->transform.position;
        }
        return {};
    }

    [[nodiscard]] cd::math::Vec3f linear_velocity(BodyHandle h) const override
    {
        if (const auto* b = engine_.world_3d().get_body(h.index()))
        {
            return b->linear_velocity;
        }
        return {};
    }

    [[nodiscard]] BodyType body_type(BodyHandle h) const override
    {
        if (const auto it = handle_types_.find(h.index()); it != handle_types_.end())
        {
            return it->second;
        }
        return BodyType::kStatic;
    }

    void set_position(BodyHandle h, cd::math::Vec3f p) override
    {
        if (auto* b = engine_.world_3d().get_body(h.index()))
        {
            b->transform.position = p;
        }
    }

    void set_linear_velocity(BodyHandle h, cd::math::Vec3f v) override
    {
        if (auto* b = engine_.world_3d().get_body(h.index()))
        {
            b->linear_velocity = v;
            b->awake = true;
        }
    }

    void apply_impulse(BodyHandle h, cd::math::Vec3f impulse) override
    {
        if (auto* b = engine_.world_3d().get_body(h.index()))
        {
            if (b->motion_type == core::BodyMotionType::kDynamic)
            {
                b->apply_linear_impulse(impulse);
            }
        }
    }

    void apply_force(BodyHandle h, cd::math::Vec3f force) override
    {
        if (auto* b = engine_.world_3d().get_body(h.index()))
        {
            if (b->motion_type == core::BodyMotionType::kDynamic)
            {
                b->apply_force(force);
            }
        }
    }

    void step(float dt) override
    {
        engine_.step_all(dt, 4u, 4u, -0.55F);
    }

    [[nodiscard]] UnifiedPhysicsEngine& unified_engine() noexcept { return engine_; }
    [[nodiscard]] const UnifiedPhysicsEngine& unified_engine() const noexcept { return engine_; }

private:
    UnifiedPhysicsEngine                          engine_ {};
    std::unordered_map<std::uint32_t, BodyType>   handle_types_ {};
};

[[nodiscard]] inline std::unique_ptr<IPhysicsWorld> make_rockstar_physics_world()
{
    return std::make_unique<RockstarPhysicsWorldAdapter>();
}

}  // namespace cd::physics
