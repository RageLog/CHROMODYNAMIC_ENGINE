// =============================================================================
// CHROMODYNAMIC — cd/physics/vehicle/JoltAdapter.hpp
// Phase 692 — cd::physics::vehicle Sprint-2 (Jolt rigid-body adapter).
//
// Bridges the Sprint-1 CPU bicycle model to `cd::physics::IPhysicsWorld` /
// `cd::physics_jolt::JoltWorld` so a vehicle chassis responds to terrain via
// real rigid-body collision rather than scripted forward integration.
//
// Sprint-2 scope
// --------------
//   * configure()      — registers chassis as a dynamic body in the world;
//                        creates four BoxShape wheel-constraint bookkeeping
//                        entries (real JPH::WheelConstraint wiring deferred
//                        to Sprint-3 once the jolt-bringup ADR lands and the
//                        vcpkg port resolves).
//   * sync_to_jolt()   — pushes throttle/brake/steer from VehicleState into
//                        per-wheel constraint descriptors + applies a drive
//                        impulse to the chassis body each frame.
//   * sync_from_jolt() — reads back chassis position + velocity from the
//                        world; updates the caller-supplied VehicleState.
//
// Stub-awareness
// --------------
//   `cd::physics_jolt::is_stub_backend()` is checked at runtime. When the
//   stub is active the physics world is a delegating Euler world; collision
//   resolution is still exercised (the inner world runs the gravity step),
//   so "chassis position progresses" assertions remain valid. The comment
//   "STUB: wheel constraints not yet wired" will disappear when Sprint-3
//   links real JPH::WheeledVehicleController.
//
// MOMENT: A racing-game dev drops a glTF terrain into the scene, the car
//   drives over hills via real rigid-body collision — not scripted curves.
//
// SOTA references:
//   * Jolt Physics WheelConstraint sample (Rouwe, jrouwe/JoltPhysics).
//   * ADR-20260530-jolt-physics-integration §F — phased delivery plan.
//   * cd::physics::IPhysicsWorld contract (engine/world/physics/).
// =============================================================================
#pragma once

#include <cd/physics/vehicle/Vehicle.hpp>
#include <cd/physics/IPhysicsWorld.hpp>
#include <cd/math/Vector.hpp>

#include <array>
#include <cstdint>

namespace cd::physics::vehicle
{

// ---------------------------------------------------------------------------
// WheelConstraintDesc — per-wheel bookkeeping held by the adapter.
//
// Sprint-2: the descriptor records the wheel index and the computed attach
// point relative to the chassis centre.  Sprint-3 will add a live
// JPH::WheeledVehicleController* here once the vcpkg jolt port lands.
// ---------------------------------------------------------------------------
struct WheelConstraintDesc
{
    /// Wheel index into VehicleConfig::wheels [0=FL, 1=FR, 2=RL, 3=RR].
    int wheel_index { 0 };

    /// Attachment point in chassis-local space (m). Derived from
    /// chassis_dimensions at configure() time.
    cd::math::Vec3f local_attach { 0.0F, 0.0F, 0.0F };

    /// Current suspension compression (m) — updated each sync_from_jolt().
    /// Sprint-2: seeded from suspension_rest_length; full spring-damper
    /// integration is Sprint-3 (requires terrain contact normal from Jolt).
    float suspension_compression { 0.0F };

    /// True when this wheel is considered grounded (Sprint-2: always true;
    /// Sprint-3 uses JPH::WheelConstraint::HasContact()).
    bool grounded { true };
};

// ---------------------------------------------------------------------------
// JoltAdapter
//
// Lifecycle:
//   1. Call configure(cfg, world) once (or again to hot-reconfigure).
//   2. Each tick: set_inputs(throttle, brake, steer), then
//      sync_to_jolt(dt) to push inputs; let your physics step run;
//      sync_from_jolt(state) to pull back chassis transform.
// ---------------------------------------------------------------------------
class JoltAdapter
{
public:
    JoltAdapter() noexcept = default;
    ~JoltAdapter() noexcept = default;

    JoltAdapter(const JoltAdapter&) = delete;
    JoltAdapter& operator=(const JoltAdapter&) = delete;
    JoltAdapter(JoltAdapter&&) noexcept = default;
    JoltAdapter& operator=(JoltAdapter&&) noexcept = default;

    // ---- Setup -------------------------------------------------------------

    /// Register the chassis as a dynamic rigid body in `world` and build
    /// per-wheel constraint descriptors from `cfg`.
    ///
    /// Safe to call multiple times — each call destroys any previous body
    /// and recreates it so the adapter can be hot-reconfigured.
    ///
    /// @returns true  when the chassis body was successfully created.
    ///          false when the world rejected the body (logged; caller should
    ///                fall back to the bicycle model).
    bool configure(const VehicleConfig& cfg,
                   cd::physics::IPhysicsWorld& world) noexcept;

    // ---- Per-frame inputs -------------------------------------------------

    /// Cache the driver inputs for the next sync_to_jolt() call.
    void set_inputs(float throttle, float brake, float steer) noexcept;

    // ---- Simulation --------------------------------------------------------

    /// Push throttle/brake/steer into the rigid body as forces/impulses.
    /// Call BEFORE world.step(dt).
    void sync_to_jolt(float dt) noexcept;

    /// Read back chassis position + velocity from the world.
    /// Writes speed_kph and wheels_grounded into `state`; leaves
    /// rpm/gear/throttle/brake/steer to be filled by Vehicle::tick().
    /// Call AFTER world.step(dt).
    void sync_from_jolt(VehicleState& state) const noexcept;

    // ---- Introspection (test / editor use) --------------------------------

    /// True after a successful configure() call.
    [[nodiscard]] bool is_configured() const noexcept { return m_configured; }

    /// Body handle registered in the physics world (valid only when
    /// is_configured() == true).
    [[nodiscard]] cd::physics::BodyHandle chassis_handle() const noexcept
    {
        return m_chassis_handle;
    }

    /// Wheel constraint descriptors (4 entries after configure()).
    [[nodiscard]] const std::array<WheelConstraintDesc, 4>& wheel_descs() const noexcept
    {
        return m_wheels;
    }

private:
    // ---- Helpers -----------------------------------------------------------

    /// Destroy any previously registered body.  No-op when not configured.
    void destroy_existing() noexcept;

    /// Compute chassis-local wheel attachment from config dimensions.
    /// Arranges [FL, FR, RL, RR] at ±half-width, ±half-length, −half-height.
    static std::array<cd::math::Vec3f, 4>
    compute_wheel_attachments(const VehicleConfig& cfg) noexcept;

    // ---- State -------------------------------------------------------------

    const VehicleConfig* m_cfg          { nullptr };
    cd::physics::IPhysicsWorld* m_world { nullptr };
    cd::physics::BodyHandle m_chassis_handle {};

    std::array<WheelConstraintDesc, 4> m_wheels {};

    float m_throttle { 0.0F };
    float m_brake    { 0.0F };
    float m_steer    { 0.0F };

    /// Total vehicle mass cached from configure() for force computation.
    float m_total_mass_kg { 1400.0F };

    /// Opaque index into the JoltWorld vehicle-constraint table.
    /// 0 = invalid / not created (maps to VehicleConstraintHandle::kInvalid).
    /// Non-zero means a real JPH::VehicleConstraint is registered in the world.
    std::uint32_t m_vehicle_constraint_idx { 0U };

    bool m_configured { false };
};

}  // namespace cd::physics::vehicle
