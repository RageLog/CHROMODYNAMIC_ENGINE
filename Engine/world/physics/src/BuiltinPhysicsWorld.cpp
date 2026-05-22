// =============================================================================
// CHROMODYNAMIC — cd/physics/BuiltinPhysicsWorld.cpp
//
// Reference implementation of IPhysicsWorld. Single-threaded semi-implicit
// Euler integrator. No collision detection (delegate to a future Jolt
// backend); this is the engine's "always-available" physics layer for
// projectile motion, character locomotion, and analytical animations.
// =============================================================================
#include <cd/physics/IPhysicsWorld.hpp>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <utility>

namespace cd::physics
{

namespace
{

struct BodyRecord
{
    BodyDesc desc {};
    cd::math::Vec3f accumulated_force { 0.0F, 0.0F, 0.0F };
};

class BuiltinPhysicsWorld final : public IPhysicsWorld
{
public:
    // ---- World config ---------------------------------------------------

    void set_gravity(cd::math::Vec3f g) noexcept override
    {
        gravity_ = g;
    }

    [[nodiscard]] cd::math::Vec3f gravity() const noexcept override
    {
        return gravity_;
    }

    // ---- Body lifecycle -------------------------------------------------

    [[nodiscard]] cd::core::Result<BodyHandle> create_body(const BodyDesc& desc) override
    {
        if (desc.type == BodyType::kDynamic && desc.mass <= 0.0F)
        {
            return std::unexpected(
                physics_errors::make(
                    physics_errors::Code::kInvalidArgument,
                    "create_body: dynamic body must have mass > 0"
                )
            );
        }
        const auto id = next_id_++;
        BodyRecord rec {};
        rec.desc = desc;
        bodies_.emplace(id, std::move(rec));
        return BodyHandle { id, 1u };
    }

    void destroy_body(BodyHandle h) override
    {
        bodies_.erase(h.index());
    }

    [[nodiscard]] std::size_t body_count() const noexcept override
    {
        return bodies_.size();
    }

    // ---- Per-body queries ----------------------------------------------

    [[nodiscard]] cd::math::Vec3f position(BodyHandle h) const override
    {
        auto it = bodies_.find(h.index());
        return it != bodies_.end() ? it->second.desc.position : cd::math::Vec3f {};
    }

    [[nodiscard]] cd::math::Vec3f linear_velocity(BodyHandle h) const override
    {
        auto it = bodies_.find(h.index());
        return it != bodies_.end() ? it->second.desc.linear_velocity : cd::math::Vec3f {};
    }

    [[nodiscard]] BodyType body_type(BodyHandle h) const override
    {
        auto it = bodies_.find(h.index());
        return it != bodies_.end() ? it->second.desc.type : BodyType::kStatic;
    }

    // ---- Per-body mutators ---------------------------------------------

    void set_position(BodyHandle h, cd::math::Vec3f p) override
    {
        if (auto it = bodies_.find(h.index()); it != bodies_.end())
        {
            it->second.desc.position = p;
        }
    }

    void set_linear_velocity(BodyHandle h, cd::math::Vec3f v) override
    {
        if (auto it = bodies_.find(h.index()); it != bodies_.end())
        {
            it->second.desc.linear_velocity = v;
        }
    }

    void apply_impulse(BodyHandle h, cd::math::Vec3f impulse) override
    {
        auto it = bodies_.find(h.index());
        if (it == bodies_.end())
            return;
        if (it->second.desc.type != BodyType::kDynamic)
            return;
        const float inv_m = 1.0F / it->second.desc.mass;
        auto& v = it->second.desc.linear_velocity;
        v.x += impulse.x * inv_m;
        v.y += impulse.y * inv_m;
        v.z += impulse.z * inv_m;
    }

    void apply_force(BodyHandle h, cd::math::Vec3f force) override
    {
        auto it = bodies_.find(h.index());
        if (it == bodies_.end())
            return;
        if (it->second.desc.type != BodyType::kDynamic)
            return;
        auto& f = it->second.accumulated_force;
        f.x += force.x;
        f.y += force.y;
        f.z += force.z;
    }

    // ---- Step -----------------------------------------------------------

    void step(float dt) override
    {
        // Semi-implicit (symplectic) Euler:
        //   v_{n+1} = (v_n + a · dt) · (1 - damping · dt)
        //   x_{n+1} = x_n + v_{n+1} · dt
        // Better long-term stability than explicit Euler for the same cost,
        // standard choice in real-time physics.
        if (dt <= 0.0F)
            return;
        for (auto& [_, rec] : bodies_)
        {
            if (rec.desc.type != BodyType::kDynamic)
            {
                rec.accumulated_force = { 0.0F, 0.0F, 0.0F };
                continue;
            }
            const float inv_m = 1.0F / rec.desc.mass;
            // Acceleration from accumulated force + gravity.
            cd::math::Vec3f a {
                rec.accumulated_force.x * inv_m + gravity_.x,
                rec.accumulated_force.y * inv_m + gravity_.y,
                rec.accumulated_force.z * inv_m + gravity_.z,
            };
            auto& v = rec.desc.linear_velocity;
            v.x += a.x * dt;
            v.y += a.y * dt;
            v.z += a.z * dt;
            // Linear damping (factor clamped to [0, 1] to keep step monotone).
            const float damp = std::clamp(1.0F - rec.desc.linear_damping * dt, 0.0F, 1.0F);
            v.x *= damp;
            v.y *= damp;
            v.z *= damp;
            auto& p = rec.desc.position;
            p.x += v.x * dt;
            p.y += v.y * dt;
            p.z += v.z * dt;
            rec.accumulated_force = { 0.0F, 0.0F, 0.0F };
        }
    }

private:
    cd::math::Vec3f gravity_ { 0.0F, -9.81F, 0.0F };
    std::uint32_t next_id_ { 1 };
    std::unordered_map<std::uint32_t, BodyRecord> bodies_ {};
};

}  // namespace

std::unique_ptr<IPhysicsWorld> make_builtin_physics_world()
{
    return std::make_unique<BuiltinPhysicsWorld>();
}

}  // namespace cd::physics
