// =============================================================================
// CHROMODYNAMIC — cd/physics_jolt/JoltWorld.cpp
// Phase 525 / T0.3 — Jolt backend (STUB scope).
//
// Implementation note (vcpkg / FetchContent status, Run 525):
//   This TU ships the **STUB** backend per the brief's escape hatch:
//   the vcpkg `jolt-physics` port was not promoted into `vcpkg.json`
//   active dependencies during this phase (engine policy requires that
//   a vcpkg dep is only added after `find_package` has been wired AND
//   verified — see `vcpkg.json` `$x-policy-note`). The FetchContent
//   tier for Jolt is also gated on a follow-up bringup ADR that records
//   the exact tag + CMake flags (per ADR-20260530 §F).
//
//   Until that ADR ships, this stub:
//     * Implements `cd::physics::IPhysicsWorld` by delegating to the
//       built-in semi-implicit Euler reference world. All public
//       behaviour the interface promises (gravity, impulses, kinematic
//       gating, body count) continues to work end-to-end, so downstream
//       libraries can develop against `cd::physics_jolt` today.
//     * Exposes the public surface (`JoltWorld.hpp`) that the real
//       implementation will keep, including config struct, component
//       descriptors (BoxShape / SphereShape / CapsuleShape / MeshShape,
//       JointKind, RigidBodyComponent / ColliderComponent / JointComponent),
//       and the `JoltJobAdapter` job-routing shim.
//     * Tracks a tiny "registered colliders / joints" side-band so the
//       component mappers can be exercised under test without a live
//       Jolt physics step.
//
//   When the real backend lands the public header stays unchanged; only
//   this TU swaps `IPhysicsWorld` delegation for `JPH::PhysicsSystem`
//   ownership and the mappers start returning live `JPH::*Settings`.
// =============================================================================
#include <cd/physics_jolt/JoltWorld.hpp>

#include <cd/physics/IPhysicsWorld.hpp>

#include <algorithm>
#include <cstdint>
#include <functional>
#include <memory>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

namespace cd::physics_jolt
{

namespace
{

// FNV-1a 64-bit — small, predictable, no <functional> hash collisions on
// our tiny POD shape payloads. The real Jolt backend keys the shape cache
// off the same hash so test fixtures can compare apples-to-apples.
[[nodiscard]] constexpr std::uint64_t fnv1a64_step(std::uint64_t h, std::uint8_t byte) noexcept
{
    return (h ^ static_cast<std::uint64_t>(byte)) * 0x100000001B3ULL;
}

[[nodiscard]] std::uint64_t fnv1a64_bytes(
    const void* data, std::size_t bytes, std::uint64_t seed) noexcept
{
    const auto* p = static_cast<const std::uint8_t*>(data);
    std::uint64_t h = seed;
    for (std::size_t i = 0; i < bytes; ++i)
    {
        h = fnv1a64_step(h, p[i]);
    }
    return h;
}

// -----------------------------------------------------------------------------
// JoltWorldStub — IPhysicsWorld implementation that defers all dynamics to
// the engine's built-in semi-implicit Euler reference. Extra book-keeping
// tracks the Jolt-side descriptor tables so component mappers can be
// exercised without a live JPH::PhysicsSystem.
// -----------------------------------------------------------------------------
class JoltWorldStub final : public cd::physics::IPhysicsWorld
{
public:
    explicit JoltWorldStub(JoltBackendConfig cfg) noexcept
        : config_ { cfg }
        , inner_ { cd::physics::make_builtin_physics_world() }
    {
    }

    // ---- World-level configuration --------------------------------------

    void set_gravity(cd::math::Vec3f g) noexcept override
    {
        inner_->set_gravity(g);
    }

    [[nodiscard]] cd::math::Vec3f gravity() const noexcept override
    {
        return inner_->gravity();
    }

    // ---- Body lifecycle -------------------------------------------------

    [[nodiscard]] cd::core::Result<cd::physics::BodyHandle>
    create_body(const cd::physics::BodyDesc& desc) override
    {
        auto r = inner_->create_body(desc);
        if (r.has_value())
        {
            rigid_components_.emplace(r->index(), body_desc_to_component(desc, config_));
        }
        return r;
    }

    void destroy_body(cd::physics::BodyHandle h) override
    {
        rigid_components_.erase(h.index());
        // Detach joints whose endpoints reference the removed body.
        joints_.erase(
            std::remove_if(
                joints_.begin(), joints_.end(),
                [&](const components::JointComponent& j) noexcept {
                    return j.body_a.index() == h.index() || j.body_b.index() == h.index();
                }),
            joints_.end());
        inner_->destroy_body(h);
    }

    [[nodiscard]] std::size_t body_count() const noexcept override
    {
        return inner_->body_count();
    }

    // ---- Per-body queries ----------------------------------------------

    [[nodiscard]] cd::math::Vec3f position(cd::physics::BodyHandle h) const override
    {
        return inner_->position(h);
    }

    [[nodiscard]] cd::math::Vec3f linear_velocity(cd::physics::BodyHandle h) const override
    {
        return inner_->linear_velocity(h);
    }

    [[nodiscard]] cd::physics::BodyType body_type(cd::physics::BodyHandle h) const override
    {
        return inner_->body_type(h);
    }

    // ---- Per-body mutation ----------------------------------------------

    void set_position(cd::physics::BodyHandle h, cd::math::Vec3f p) override
    {
        inner_->set_position(h, p);
    }

    void set_linear_velocity(cd::physics::BodyHandle h, cd::math::Vec3f v) override
    {
        inner_->set_linear_velocity(h, v);
    }

    void apply_impulse(cd::physics::BodyHandle h, cd::math::Vec3f impulse) override
    {
        inner_->apply_impulse(h, impulse);
    }

    void apply_force(cd::physics::BodyHandle h, cd::math::Vec3f force) override
    {
        inner_->apply_force(h, force);
    }

    // ---- Simulation -----------------------------------------------------

    void step(float dt) override
    {
        // The real backend would: gather impulses from constraints, run
        // broadphase + narrowphase, integrate via Jolt's constraint solver.
        // The stub forwards to the Euler reference — joints (recorded
        // below) are a no-op for now and ship in P3.
        inner_->step(dt);
    }

    // ---- Stub-specific side-band APIs (header-private, friend-accessed
    //      via the free helpers below for test introspection). ---------

    void register_collider(
        cd::physics::BodyHandle owner, const components::ColliderComponent& col)
    {
        colliders_[owner.index()].push_back(col);
    }

    [[nodiscard]] std::size_t collider_count(cd::physics::BodyHandle owner) const noexcept
    {
        auto it = colliders_.find(owner.index());
        return it == colliders_.end() ? 0U : it->second.size();
    }

    void register_joint(const components::JointComponent& j)
    {
        joints_.push_back(j);
    }

    [[nodiscard]] std::size_t joint_count() const noexcept
    {
        return joints_.size();
    }

    [[nodiscard]] const JoltBackendConfig& config() const noexcept
    {
        return config_;
    }

private:
    JoltBackendConfig config_ {};
    std::unique_ptr<cd::physics::IPhysicsWorld> inner_ {};
    std::unordered_map<std::uint32_t, components::RigidBodyComponent> rigid_components_ {};
    std::unordered_map<std::uint32_t, std::vector<components::ColliderComponent>> colliders_ {};
    std::vector<components::JointComponent> joints_ {};
};

}  // namespace

// ---- Public free functions -------------------------------------------------

components::RigidBodyComponent body_desc_to_component(
    const cd::physics::BodyDesc& desc, const JoltBackendConfig& cfg) noexcept
{
    components::RigidBodyComponent rb {};
    rb.type = desc.type;
    rb.mass = desc.mass;
    // BodyDesc only carries linear damping today; the Jolt-side angular
    // damping picks up the config default. Real backend will surface
    // angular damping in `BodyDesc` once `cd::scene::RigidBodyComponent`
    // lands (ADR-004 follow-up).
    rb.linear_damping = desc.linear_damping > 0.0F ? desc.linear_damping : cfg.default_linear_damping;
    rb.angular_damping = cfg.default_angular_damping;
    return rb;
}

std::uint64_t hash_shape_desc(const components::ShapeDesc& shape) noexcept
{
    constexpr std::uint64_t kFnvOffsetBasis = 0xcbf29ce484222325ULL;
    constexpr std::uint64_t kBoxTag = 1;
    constexpr std::uint64_t kSphereTag = 2;
    constexpr std::uint64_t kCapsuleTag = 3;
    constexpr std::uint64_t kMeshTag = 4;
    return std::visit(
        [&](const auto& s) -> std::uint64_t {
            using T = std::decay_t<decltype(s)>;
            if constexpr (std::is_same_v<T, components::BoxShape>)
            {
                std::uint64_t h = fnv1a64_step(kFnvOffsetBasis, static_cast<std::uint8_t>(kBoxTag));
                return fnv1a64_bytes(&s.half_extents, sizeof(s.half_extents), h);
            }
            else if constexpr (std::is_same_v<T, components::SphereShape>)
            {
                std::uint64_t h = fnv1a64_step(kFnvOffsetBasis, static_cast<std::uint8_t>(kSphereTag));
                return fnv1a64_bytes(&s.radius, sizeof(s.radius), h);
            }
            else if constexpr (std::is_same_v<T, components::CapsuleShape>)
            {
                std::uint64_t h = fnv1a64_step(kFnvOffsetBasis, static_cast<std::uint8_t>(kCapsuleTag));
                h = fnv1a64_bytes(&s.half_height, sizeof(s.half_height), h);
                return fnv1a64_bytes(&s.radius, sizeof(s.radius), h);
            }
            else if constexpr (std::is_same_v<T, components::MeshShape>)
            {
                std::uint64_t h = fnv1a64_step(kFnvOffsetBasis, static_cast<std::uint8_t>(kMeshTag));
                h = fnv1a64_bytes(&s.vertex_count, sizeof(s.vertex_count), h);
                h = fnv1a64_bytes(&s.index_count, sizeof(s.index_count), h);
                // Pointer identity participates in the hash so two mesh
                // shapes with the same counts but different buffers compare
                // distinct — matches Jolt cache semantics (mesh shapes are
                // expensive to build, callers must reuse buffers).
                const std::uintptr_t v_addr = reinterpret_cast<std::uintptr_t>(s.vertices);
                const std::uintptr_t i_addr = reinterpret_cast<std::uintptr_t>(s.indices);
                h = fnv1a64_bytes(&v_addr, sizeof(v_addr), h);
                return fnv1a64_bytes(&i_addr, sizeof(i_addr), h);
            }
            else
            {
                // Unreachable: ShapeDesc variant is closed; static_assert
                // keeps it that way if a new variant lands without a hash
                // handler being added above.
                static_assert(!std::is_same_v<T, T>, "unhandled ShapeDesc variant");
                return kFnvOffsetBasis;
            }
        },
        shape);
}

std::unique_ptr<cd::physics::IPhysicsWorld> make_jolt_physics_world(const JoltBackendConfig& cfg)
{
    return std::make_unique<JoltWorldStub>(cfg);
}

bool is_stub_backend() noexcept
{
    return true;
}

namespace
{

// Downcast helper. Returns nullptr when the world isn't a Jolt-backed one
// (foreign IPhysicsWorld implementations stay supported — the free helpers
// degrade to no-ops + zero counts in that case).
[[nodiscard]] JoltWorldStub* as_jolt(cd::physics::IPhysicsWorld& w) noexcept
{
    return dynamic_cast<JoltWorldStub*>(&w);
}

[[nodiscard]] const JoltWorldStub* as_jolt(const cd::physics::IPhysicsWorld& w) noexcept
{
    return dynamic_cast<const JoltWorldStub*>(&w);
}

}  // namespace

bool attach_collider(
    cd::physics::IPhysicsWorld& world,
    cd::physics::BodyHandle owner,
    const components::ColliderComponent& collider)
{
    auto* impl = as_jolt(world);
    if (impl == nullptr)
        return false;
    impl->register_collider(owner, collider);
    return true;
}

std::size_t collider_count(
    const cd::physics::IPhysicsWorld& world, cd::physics::BodyHandle owner) noexcept
{
    const auto* impl = as_jolt(world);
    return impl == nullptr ? 0U : impl->collider_count(owner);
}

bool attach_joint(
    cd::physics::IPhysicsWorld& world, const components::JointComponent& joint)
{
    auto* impl = as_jolt(world);
    if (impl == nullptr)
        return false;
    impl->register_joint(joint);
    return true;
}

std::size_t joint_count(const cd::physics::IPhysicsWorld& world) noexcept
{
    const auto* impl = as_jolt(world);
    return impl == nullptr ? 0U : impl->joint_count();
}

JoltBackendConfig backend_config(const cd::physics::IPhysicsWorld& world) noexcept
{
    const auto* impl = as_jolt(world);
    if (impl == nullptr)
        return JoltBackendConfig {};
    return impl->config();
}

}  // namespace cd::physics_jolt
