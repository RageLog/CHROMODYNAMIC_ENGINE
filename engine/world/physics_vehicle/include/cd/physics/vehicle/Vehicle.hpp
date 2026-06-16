// =============================================================================
// CHROMODYNAMIC — cd/physics/vehicle/Vehicle.hpp
// Phase 670 — cd::physics::vehicle Sprint-1 (CPU bicycle model).
// Phase 692 — Sprint-2: use_jolt flag + JoltAdapter delegation.
//
// Provides a 4-wheel car/truck vehicle model with:
//   * WheelConfig  — per-wheel geometry, suspension, and steering parameters.
//   * EngineConfig — torque curve, gear ratios, RPM limits.
//   * VehicleConfig — chassis + wheel + engine assembly.
//   * VehicleState — read-only per-frame simulation output.
//   * Vehicle      — configure / set_input / tick / state API.
//
// Sprint-1 scope — CPU-side math only (no Jolt/terrain interaction):
//   * Simplified bicycle model: front axle steers, rear axle drives (or all
//     four when all wheels have is_driven=true).
//   * Longitudinal speed integration via semi-implicit Euler.
//   * Kinematic-bicycle lateral response: yaw_rate = v * tan(delta) / wheelbase
//     and lateral_accel = v * yaw_rate (linear small-slip; a Pacejka slip-angle
//     tyre model is deferred — see ADR-20260616-band3-world-scope §2.4).
//   * Gear selection based on RPM threshold crossing.
//   * Anti-roll and suspension stiffness influence handled as tuning constants
//     (not full spring-damper integration — deferred to Sprint-2 + Jolt).
//
// Sprint-2 (Phase 692):
//   * VehicleConfig::use_jolt (default false) — when true, Vehicle::tick()
//     delegates to cd::physics::vehicle::JoltAdapter instead of the bicycle
//     model. The adapter is configured via Vehicle::configure_jolt().
//   * The public API contract (WheelConfig, EngineConfig, VehicleConfig,
//     VehicleState, Vehicle) is stable across sprints.
//
// Sprint-3 will wire JPH::WheeledVehicleController per-wheel constraints
// once the vcpkg jolt-physics port lands (ADR-2026xxxx-physics-jolt-bringup).
//
// MOMENT: A racing-game dev drops VehicleConfig in, gets a controllable car
// that responds to throttle/brake/steer with realistic gear-shift behaviour —
// without writing wheel physics from scratch.
//
// SOTA references:
//   * Pacejka "Magic Formula" tyre model (simplified longitudinal-only here).
//   * SAE 950970 — vehicle dynamics fundamentals (gear-ratio torque chain).
//   * Gregor Veble, "Physics of Racing" series (freely available).
//   * Unity 5 WheelCollider documentation (API surface comparison).
// =============================================================================
#pragma once

#include <array>
#include <cstdint>
#include <memory>

// Forward declarations (Sprint-2 additions).
namespace cd::physics { class IPhysicsWorld; }

namespace cd::physics::vehicle
{

// Forward declaration (avoids including JoltAdapter.hpp in public header).
class JoltAdapter;

// ---------------------------------------------------------------------------
// WheelConfig — per-wheel authored parameters.
// ---------------------------------------------------------------------------

struct WheelConfig
{
    /// Wheel rolling radius (m). Typical passenger car: 0.31–0.36 m.
    float radius { 0.33F };

    /// Unsprung mass of the wheel assembly (kg). Affects inertia, not used
    /// for terrain coupling until Sprint-2.
    float mass { 18.0F };

    /// Distance from chassis attachment to wheel centre at rest (m).
    float suspension_rest_length { 0.25F };

    /// Suspension spring stiffness (N/m). Stiffer = less body roll.
    float suspension_stiffness { 22000.0F };

    /// Suspension damping coefficient (N·s/m). Critically damp at ~2*sqrt(k*m).
    float damping { 4000.0F };

    /// Maximum steering angle for this wheel (radians). 0 = non-steering
    /// (typically rear wheels).
    float steering_angle_max { 0.0F };

    /// True if this wheel receives drive torque from the engine.
    bool is_driven { false };
};

// ---------------------------------------------------------------------------
// EngineConfig — powertrain authored parameters.
// ---------------------------------------------------------------------------

struct EngineConfig
{
    /// Peak engine torque output (N·m). Petrol road-car range: 150–600 N·m.
    float max_torque_nm { 300.0F };

    /// Gear ratios [0..5] where index 0 = 1st gear.
    /// Ratio = engine_rpm / wheel_rpm. Typical 6-speed: {3.5, 2.1, 1.4, 1.0, 0.8, 0.67}.
    std::array<float, 6> gear_ratios { 3.5F, 2.1F, 1.4F, 1.0F, 0.8F, 0.67F };

    /// Final-drive (differential) ratio applied after the gearbox.
    float final_drive { 3.7F };

    /// Idle RPM — engine does not drop below this under throttle.
    float idle_rpm { 800.0F };

    /// Redline (maximum RPM). Gear shift is triggered when RPM exceeds this.
    float max_rpm { 6500.0F };
};

// ---------------------------------------------------------------------------
// VehicleConfig — full vehicle assembly parameters.
// ---------------------------------------------------------------------------

struct VehicleConfig
{
    /// Total chassis mass (kg). Does NOT include wheel unsprung mass.
    float chassis_mass_kg { 1400.0F };

    /// Chassis AABB half-extents [length, width, height] (m). Used for
    /// moment-of-inertia estimation and debug visualisation.
    std::array<float, 3> chassis_dimensions { 2.4F, 1.0F, 0.7F };

    /// Wheel configs ordered [FL, FR, RL, RR].
    std::array<WheelConfig, 4> wheels {};

    EngineConfig engine {};

    /// When true, Vehicle::tick() delegates chassis dynamics to
    /// cd::physics::vehicle::JoltAdapter (Sprint-2+). The adapter must
    /// be armed by calling Vehicle::configure_jolt() before the first
    /// tick; if not armed, Vehicle falls back to the bicycle model
    /// regardless of this flag.
    ///
    /// Default false — preserves Sprint-1 behaviour when no Jolt world
    /// is available or desired (unit tests, headless tools).
    bool use_jolt { false };
};

// ---------------------------------------------------------------------------
// VehicleState — per-frame read-only simulation output.
// ---------------------------------------------------------------------------

struct VehicleState
{
    /// Forward speed (km/h). Negative = reversing.
    float speed_kph { 0.0F };

    /// Current engine RPM.
    float rpm { 0.0F };

    /// Current gear index [0 = 1st, 5 = 6th].
    uint8_t gear { 0 };

    /// Normalised throttle input in use this frame [0, 1].
    float throttle { 0.0F };

    /// Normalised brake input in use this frame [0, 1].
    float brake { 0.0F };

    /// Normalised steer input in use this frame [-1, 1].
    float steer { 0.0F };

    /// Body yaw rate (rad/s) from the kinematic bicycle model. Positive = the
    /// nose turns toward +steer. Computed as v * tan(delta) / wheelbase, where
    /// delta is the front-wheel steer angle. Zero at standstill or zero steer.
    /// This is the linear (small-slip) lateral response; a Pacejka slip-angle
    /// tyre model is deferred (ADR-20260616-band3-world-scope §2.4).
    float yaw_rate_rad_s { 0.0F };

    /// Lateral (centripetal) acceleration (m/s^2) in the body frame from the
    /// same kinematic bicycle model: a_lat = v * yaw_rate = v^2 * tan(delta)/L.
    /// Useful for tyre-load transfer, camera shake, and grip-limit checks.
    float lateral_accel_ms2 { 0.0F };

    /// True when each wheel is considered in contact with the ground.
    /// Sprint-1: always true (no terrain query yet; set by suspension model).
    bool wheels_grounded[4] { true, true, true, true };
};

// ---------------------------------------------------------------------------
// Vehicle — the main simulation object.
// ---------------------------------------------------------------------------

class Vehicle
{
public:
    // Constructor + destructor are user-declared and defined in Vehicle.cpp
    // so that unique_ptr<JoltAdapter> can see the complete JoltAdapter type
    // at the deletion point (PIMPL pattern, CLAUDE.md §1).
    Vehicle() noexcept;
    ~Vehicle() noexcept;

    Vehicle(const Vehicle&) = delete;
    Vehicle& operator=(const Vehicle&) = delete;
    Vehicle(Vehicle&&) noexcept = default;
    Vehicle& operator=(Vehicle&&) noexcept = default;

    // ---- Setup -------------------------------------------------------------

    /// Replace current configuration. Resets all dynamic state (speed, RPM,
    /// gear) to initial values. Safe to call multiple times.
    void configure(const VehicleConfig& cfg) noexcept;

    /// Arm the JoltAdapter for this vehicle (Sprint-2).
    /// Must be called before tick() when VehicleConfig::use_jolt == true.
    ///
    /// @param world  The live IPhysicsWorld (owned externally; lifetime must
    ///               exceed this Vehicle). For the stub backend pass the
    ///               result of cd::physics_jolt::make_jolt_physics_world();
    ///               for tests a plain cd::physics::make_builtin_physics_world()
    ///               suffices since IPhysicsWorld is backend-agnostic.
    ///
    /// @returns true  when the chassis body was successfully registered.
    ///          false when the world rejected the body; in that case tick()
    ///                silently falls back to the bicycle model.
    bool configure_jolt(cd::physics::IPhysicsWorld& world) noexcept;

    // ---- Per-frame input ---------------------------------------------------

    /// Set driver inputs for the next tick.
    ///   @param throttle  [0, 1]  — 0 = no throttle, 1 = wide-open throttle.
    ///   @param brake     [0, 1]  — 0 = no brake,    1 = full ABS braking.
    ///   @param steer     [-1, 1] — -1 = full left,   1 = full right.
    void set_input(float throttle, float brake, float steer) noexcept;

    // ---- Simulation --------------------------------------------------------

    /// Advance the vehicle simulation by `dt` seconds using semi-implicit
    /// Euler integration.  Gear shifts occur when RPM crosses the configured
    /// threshold.
    void tick(float dt) noexcept;

    // ---- Output ------------------------------------------------------------

    /// Read the last simulated vehicle state. Undefined if tick() has never
    /// been called.
    [[nodiscard]] const VehicleState& state() const noexcept { return state_; }

private:
    // ---- Helpers -----------------------------------------------------------

    /// Compute wheel angular velocity (rad/s) from current speed.
    [[nodiscard]] float wheel_omega() const noexcept;

    /// Compute engine RPM from wheel omega, gear, and final drive.
    [[nodiscard]] float engine_rpm_from_wheel(float wheel_omega_rad_s) const noexcept;

    /// Advance gear up if RPM > max_rpm; down if RPM < idle_rpm (hysteresis
    /// built in via idle_rpm guard). Returns new gear index.
    [[nodiscard]] uint8_t auto_shift(float rpm, uint8_t current_gear) const noexcept;

    /// Effective wheelbase (m) = front-to-rear axle distance, derived from the
    /// wheel attachment longitudinal span when available, else from the chassis
    /// length. Always strictly positive.
    [[nodiscard]] float wheelbase() const noexcept;

    /// Kinematic-bicycle lateral response for a forward speed `v_ms` (m/s) and
    /// the current steer input. Writes yaw_rate_rad_s + lateral_accel_ms2 into
    /// `out`. Linear small-slip model (no Pacejka); deterministic.
    void compute_lateral(float v_ms, VehicleState& out) const noexcept;

    // ---- State -------------------------------------------------------------

    VehicleConfig cfg_ {};

    /// Longitudinal velocity (m/s). Positive = forward.
    float velocity_ms_ { 0.0F };

    /// Pending driver inputs (set by set_input, consumed by tick).
    float throttle_ { 0.0F };
    float brake_    { 0.0F };
    float steer_    { 0.0F };

    /// Current gear [0 = 1st gear].
    uint8_t gear_ { 0 };

    VehicleState state_ {};

    // ---- Sprint-2: JoltAdapter (heap-allocated to keep header PIMPL-clean) --
    // Non-null only after a successful configure_jolt() call.
    std::unique_ptr<JoltAdapter> jolt_adapter_;

    /// Non-owning pointer to the world passed to configure_jolt().
    cd::physics::IPhysicsWorld* jolt_world_ { nullptr };
};

}  // namespace cd::physics::vehicle
