// =============================================================================
// CHROMODYNAMIC — cd/physics/IPhysicsWorld.hpp
// Phase 4 / Sprint S4.3 — physics-engine abstraction layer.
//
// `IPhysicsWorld` is the backend-agnostic interface every physics
// implementation (built-in semi-implicit Euler, future Jolt, Bullet,
// PhysX) plugs into. The engine talks to physics exclusively through
// this surface so swapping solvers is a one-line factory change.
//
// Resource model:
//   * Rigid bodies are referenced by phantom-tagged `BodyHandle`.
//   * The world owns body lifetime; callers `create_body` /
//     `destroy_body`. Sleeping / kinematic / static states are encoded
//     in the body type, not in separate APIs.
//   * Forces and impulses go in through small descriptor structs to
//     keep the ABI cheap to call per-frame.
//
// SOTA references: Jolt physics [Bos 2021], Bullet 3, PhysX SDK,
// rapier3d. All converge on a "build the world, accumulate forces,
// step(dt)" pattern — that is the contract here.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <cd/core/ErrorCode.hpp>
#include <cd/core/Handle.hpp>
#include <cd/core/Result.hpp>
#include <cd/math/Vector.hpp>

#include <cstdint>
#include <memory>
#include <string_view>

namespace cd::physics
{

// ---- Error domain -----------------------------------------------------------

namespace physics_errors
{
inline constexpr std::uint32_t kDomain = 0x000E;

enum class Code : std::uint32_t
{
    kOk = 0,
    kInvalidArgument = 1,
    kUnknownBody = 2,
    kBackendError = 3,
};

[[nodiscard]] inline cd::core::ErrorCode make(Code c, std::string_view m = {}) noexcept
{
    return cd::core::ErrorCode { kDomain, static_cast<std::uint32_t>(c), m };
}
}  // namespace physics_errors

// ---- Handles + descriptions -----------------------------------------------

struct BodyTag
{
};

using BodyHandle = cd::core::Handle<BodyTag>;

enum class BodyType : std::uint8_t
{
    kStatic,     ///< Immovable; affects others but is never integrated.
    kKinematic,  ///< Moves by direct teleport / set_velocity; not by forces.
    kDynamic,    ///< Full rigid-body dynamics: forces / impulses / gravity.
};

struct BodyDesc
{
    BodyType type { BodyType::kDynamic };
    cd::math::Vec3f position { 0.0F, 0.0F, 0.0F };
    cd::math::Vec3f linear_velocity { 0.0F, 0.0F, 0.0F };
    /// kg. Ignored for non-dynamic bodies. Must be > 0 for kDynamic.
    float mass { 1.0F };
    /// Velocity damping per second (0 = none, 1 = fully damped in one second).
    float linear_damping { 0.0F };
};

// ---- Interface --------------------------------------------------------------

class IPhysicsWorld
{
public:
    IPhysicsWorld() noexcept = default;
    virtual ~IPhysicsWorld() = default;
    IPhysicsWorld(const IPhysicsWorld&) = delete;
    IPhysicsWorld& operator=(const IPhysicsWorld&) = delete;
    IPhysicsWorld(IPhysicsWorld&&) = delete;
    IPhysicsWorld& operator=(IPhysicsWorld&&) = delete;

    // ---- World-level configuration --------------------------------------

    /// World gravity applied to every dynamic body each step. Default
    /// `{0, -9.81, 0}` (Earth, Y-up).
    virtual void set_gravity(cd::math::Vec3f g) noexcept = 0;
    [[nodiscard]] virtual cd::math::Vec3f gravity() const noexcept = 0;

    // ---- Body lifecycle -------------------------------------------------

    [[nodiscard]] virtual cd::core::Result<BodyHandle> create_body(const BodyDesc& desc) = 0;
    virtual void destroy_body(BodyHandle h) = 0;
    [[nodiscard]] virtual std::size_t body_count() const noexcept = 0;

    // ---- Per-body queries (read) ----------------------------------------

    [[nodiscard]] virtual cd::math::Vec3f position(BodyHandle h) const = 0;
    [[nodiscard]] virtual cd::math::Vec3f linear_velocity(BodyHandle h) const = 0;
    [[nodiscard]] virtual BodyType body_type(BodyHandle h) const = 0;

    // ---- Per-body mutation ----------------------------------------------

    virtual void set_position(BodyHandle h, cd::math::Vec3f p) = 0;
    virtual void set_linear_velocity(BodyHandle h, cd::math::Vec3f v) = 0;
    /// Apply an instantaneous impulse (kg·m/s). For kKinematic / kStatic
    /// bodies this is a no-op.
    virtual void apply_impulse(BodyHandle h, cd::math::Vec3f impulse) = 0;
    /// Accumulate a continuous force (N) until the next step. Cleared at
    /// step() time. No-op on non-dynamic bodies.
    virtual void apply_force(BodyHandle h, cd::math::Vec3f force) = 0;

    // ---- Simulation -----------------------------------------------------

    /// Integrate the world by `dt` seconds. The MVP solver uses semi-
    /// implicit (symplectic) Euler — drift-free for positional state,
    /// stable for the simple use cases driving the engine today. Backends
    /// can override with substep / RK4 / variational integrators without
    /// breaking the contract.
    virtual void step(float dt) = 0;
};

// ---- Factories --------------------------------------------------------------

/// Build the engine's built-in physics world. Implements the IPhysicsWorld
/// contract with a semi-implicit Euler integrator, no collision detection
/// (positions are integrated independently — `set_position` is the way to
/// enforce constraints today). Plug-in Jolt / Bullet backends are a
/// follow-up; the factory signature does not change.
[[nodiscard]] std::unique_ptr<IPhysicsWorld> make_builtin_physics_world();

}  // namespace cd::physics
