// =============================================================================
// CHROMODYNAMIC — cd/physics/vehicle/Vehicle.cpp
// Phase 670 — cd::physics::vehicle Sprint-1 implementation.
// Phase 692 — Sprint-2: configure_jolt() + Jolt delegation path in tick().
//
// Implementation notes
// --------------------
//
// Bicycle model (longitudinal only, Sprint-1):
//   The vehicle is treated as a 1-DOF point mass moving along a forward axis.
//   Lateral dynamics (slip angle, Pacejka lateral force) are deferred to
//   Sprint-2 when the Jolt rigid-body handle provides a contact normal.
//
//   Forward force chain:
//     F_drive = (engine_torque * gear_ratio * final_drive) / wheel_radius
//     F_brake = brake_input * chassis_mass * 9.81 * brake_coeff
//     F_drag  = 0.5 * rho_air * Cd * A * v^2  (rolling resistance)
//     F_net   = F_drive - F_drag - F_brake
//     a       = F_net / (chassis_mass + sum(wheel_mass))
//     v_new   = v_old + a * dt                 (semi-implicit Euler)
//
// Engine torque curve (Sprint-1, flat + idle taper):
//   A flat torque curve between idle_rpm and 0.8*max_rpm, then linear taper
//   to zero at max_rpm approximates a naturally-aspirated petrol engine.
//   The idle floor prevents stalling on zero-throttle creep.
//
// Gear auto-shift:
//   Upshift  when rpm > max_rpm   and gear < max_gear.
//   Downshift when rpm < idle_rpm and gear > 0.
//   A single-frame hysteresis step is sufficient for Sprint-1 (no shift-shock
//   or dual-clutch model).
//
// Steer angle:
//   Front wheels are steered by steer_ * steering_angle_max.  The heading
//   angle integrates steer_ * curvature * v * dt so VehicleState::steer
//   reflects the instantaneous input, not the accumulated yaw.  A full yaw-rate
//   state will be added in Sprint-2 alongside Jolt constraint solving.
// =============================================================================

#include <cd/physics/vehicle/Vehicle.hpp>
#include <cd/physics/vehicle/JoltAdapter.hpp>
#include <cd/physics/IPhysicsWorld.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <numbers>

namespace cd::physics::vehicle
{

// ---- Vehicle ctor/dtor (defined here so ~unique_ptr<JoltAdapter> sees the
//      complete type; JoltAdapter.hpp is included above). PIMPL pattern. ---

Vehicle::Vehicle() noexcept {}
Vehicle::~Vehicle() noexcept {}

// ---- Physical constants (Sprint-1 fixed) -----------------------------------

namespace
{
// Air density (kg/m^3) at sea level 15 °C.
constexpr float kAirDensity     = 1.225F;
// Drag coefficient * frontal area (m^2) — typical saloon car ~ 0.75.
constexpr float kCdA            = 0.75F;
// Rolling-resistance coefficient (dimensionless).
constexpr float kRollingResist  = 0.015F;
// Brake-force coefficient relative to mass*g.
constexpr float kBrakeCoeff     = 0.95F;
// Gravitational acceleration (m/s^2).
constexpr float kGravity        = 9.81F;
// Minimum speed threshold below which the vehicle is clamped to rest (m/s).
constexpr float kRestThreshold  = 0.01F;
// Number of drive wheels assumed for torque-per-wheel split.  Sprint-1 uses
// the number of wheels with is_driven == true; fallback = 2 if none marked.
// (Computed dynamically in configure().)
} // namespace

// ---- Vehicle::configure ----------------------------------------------------

void Vehicle::configure(const VehicleConfig& cfg) noexcept
{
    cfg_         = cfg;
    velocity_ms_ = 0.0F;
    throttle_    = 0.0F;
    brake_       = 0.0F;
    steer_       = 0.0F;
    gear_        = 0;
    state_       = VehicleState {};

    // Reset any previously configured Jolt adapter; the caller must call
    // configure_jolt() again after configure() if use_jolt is still desired.
    jolt_adapter_.reset();
    jolt_world_ = nullptr;
}

// ---- Vehicle::configure_jolt -----------------------------------------------

bool Vehicle::configure_jolt(cd::physics::IPhysicsWorld& world) noexcept
{
    // Lazily create the adapter on first call.
    if (!jolt_adapter_)
    {
        jolt_adapter_ = std::make_unique<JoltAdapter>();
    }

    const bool ok = jolt_adapter_->configure(cfg_, world);
    if (ok)
    {
        jolt_world_ = &world;
    }
    else
    {
        // configure() failed — destroy adapter so tick() falls back to bicycle.
        jolt_adapter_.reset();
        jolt_world_ = nullptr;
    }
    return ok;
}

// ---- Vehicle::set_input ----------------------------------------------------

void Vehicle::set_input(float throttle, float brake, float steer) noexcept
{
    // Clamp all inputs to valid ranges.
    throttle_ = std::clamp(throttle, 0.0F, 1.0F);
    brake_    = std::clamp(brake,    0.0F, 1.0F);
    steer_    = std::clamp(steer,   -1.0F, 1.0F);
}

// ---- Vehicle::tick ---------------------------------------------------------

void Vehicle::tick(float dt) noexcept
{
    if (dt <= 0.0F)
    {
        return;
    }

    // ---- Sprint-2: Jolt delegation path ------------------------------------
    // When use_jolt is requested AND the adapter was successfully armed via
    // configure_jolt(), push inputs into Jolt, step the world, then read back
    // chassis transform. RPM / gear remain bicycle-model derived (Sprint-3
    // will compute them from JPH::WheeledVehicleController::GetInfo()).
    if (cfg_.use_jolt && jolt_adapter_ && jolt_adapter_->is_configured()
        && jolt_world_ != nullptr)
    {
        jolt_adapter_->set_inputs(throttle_, brake_, steer_);
        jolt_adapter_->sync_to_jolt(dt);
        jolt_world_->step(dt);
        jolt_adapter_->sync_from_jolt(state_);

        // Populate inputs in state (sync_from_jolt only touches speed/wheels).
        state_.throttle = throttle_;
        state_.brake    = brake_;
        state_.steer    = steer_;

        // RPM + gear estimation from speed read back (reuse bicycle helper).
        const float speed_ms = state_.speed_kph / 3.6F;
        const float wheel_r  = cfg_.wheels[0].radius > 1e-6F
                                   ? cfg_.wheels[0].radius : 0.32F;
        const float omega    = speed_ms / wheel_r;
        const float raw_rpm  = engine_rpm_from_wheel(omega);
        gear_               = auto_shift(raw_rpm, gear_);
        state_.gear         = gear_;
        state_.rpm          = raw_rpm;

        return; // Jolt path complete — skip bicycle model below.
    }
    // ---- Fall-through: Sprint-1 bicycle model (use_jolt false or unarmed) --

    // ---- Effective wheel radius (use first driven wheel; fallback FL) -------
    float wheel_radius = cfg_.wheels[0].radius;
    for (const auto& w : cfg_.wheels)
    {
        if (w.is_driven)
        {
            wheel_radius = w.radius;
            break;
        }
    }

    // ---- Effective mass (chassis + all wheel unsprung masses) --------------
    float total_mass = cfg_.chassis_mass_kg;
    for (const auto& w : cfg_.wheels)
    {
        total_mass += w.mass;
    }

    // ---- Count driven wheels -----------------------------------------------
    int driven_count = 0;
    for (const auto& w : cfg_.wheels)
    {
        if (w.is_driven)
        {
            ++driven_count;
        }
    }
    if (driven_count == 0)
    {
        driven_count = 2; // Sprint-1 fallback (assume RWD)
    }

    // ---- Engine RPM from current wheel speed --------------------------------
    const float omega = wheel_omega();
    const float raw_rpm = engine_rpm_from_wheel(omega);

    // ---- Auto gear selection ------------------------------------------------
    gear_ = auto_shift(raw_rpm, gear_);

    const float ratio       = cfg_.engine.gear_ratios[gear_];
    const float final_drive = cfg_.engine.final_drive;
    const float total_ratio = ratio * final_drive;

    // ---- Engine RPM (clamped to operating window) ---------------------------
    const float e_rpm = std::clamp(std::fabs(omega) * total_ratio * (60.0F / (2.0F * std::numbers::pi_v<float>)),
                                   cfg_.engine.idle_rpm,
                                   cfg_.engine.max_rpm);

    // ---- Engine torque curve (flat + taper) ---------------------------------
    // Taper region starts at 80 % of max_rpm.
    const float taper_start = 0.8F * cfg_.engine.max_rpm;
    float torque_fraction   = 1.0F;
    if (e_rpm > taper_start)
    {
        torque_fraction = 1.0F - (e_rpm - taper_start) /
                                 (cfg_.engine.max_rpm - taper_start);
        torque_fraction = std::clamp(torque_fraction, 0.0F, 1.0F);
    }

    const float engine_torque = throttle_ * cfg_.engine.max_torque_nm * torque_fraction;

    // ---- Drive force at wheel (split across driven wheels) ------------------
    // F = T * total_ratio / r  (each driven wheel); here we net the sum.
    const float f_drive = (engine_torque * total_ratio) / wheel_radius;

    // ---- Aerodynamic drag + rolling resistance ------------------------------
    const float v_abs     = std::fabs(velocity_ms_);
    const float f_aero    = 0.5F * kAirDensity * kCdA * v_abs * v_abs;
    const float f_rolling = kRollingResist * total_mass * kGravity;
    // Both always oppose motion direction.
    const float f_resist  = (f_aero + f_rolling) * (velocity_ms_ >= 0.0F ? 1.0F : -1.0F);

    // ---- Brake force (oppose velocity) --------------------------------------
    const float f_brake_mag = brake_ * kBrakeCoeff * total_mass * kGravity;
    const float f_brake     = f_brake_mag * (velocity_ms_ >= 0.0F ? 1.0F : -1.0F);

    // ---- Net force and acceleration -----------------------------------------
    const float f_net = f_drive - f_resist - f_brake;
    const float accel = f_net / total_mass;

    // Semi-implicit Euler: integrate velocity.
    velocity_ms_ += accel * dt;

    // Clamp to rest when nearly stopped and no throttle applied.
    if (throttle_ < 0.01F && std::fabs(velocity_ms_) < kRestThreshold)
    {
        velocity_ms_ = 0.0F;
    }

    // ---- Update state output ------------------------------------------------
    state_.speed_kph = velocity_ms_ * 3.6F;
    state_.rpm       = e_rpm;
    state_.gear      = gear_;
    state_.throttle  = throttle_;
    state_.brake     = brake_;
    state_.steer     = steer_;

    // Sprint-1: all wheels grounded (no terrain query).
    for (int i = 0; i < 4; ++i)
    {
        state_.wheels_grounded[i] = true;
    }
}

// ---- Vehicle::wheel_omega --------------------------------------------------

float Vehicle::wheel_omega() const noexcept
{
    float wheel_radius = cfg_.wheels[0].radius;
    for (const auto& w : cfg_.wheels)
    {
        if (w.is_driven)
        {
            wheel_radius = w.radius;
            break;
        }
    }
    if (wheel_radius < 1e-6F)
    {
        return 0.0F;
    }
    return velocity_ms_ / wheel_radius;
}

// ---- Vehicle::engine_rpm_from_wheel ----------------------------------------

float Vehicle::engine_rpm_from_wheel(float wheel_omega_rad_s) const noexcept
{
    const float ratio       = cfg_.engine.gear_ratios[gear_];
    const float final_drive = cfg_.engine.final_drive;
    const float total_ratio = ratio * final_drive;
    // rpm = (omega_wheel * total_ratio) * (60 / 2pi)
    const float rpm = std::fabs(wheel_omega_rad_s) * total_ratio * (60.0F / (2.0F * std::numbers::pi_v<float>));
    return std::clamp(rpm, cfg_.engine.idle_rpm, cfg_.engine.max_rpm);
}

// ---- Vehicle::auto_shift ---------------------------------------------------

uint8_t Vehicle::auto_shift(float rpm, uint8_t current_gear) const noexcept
{
    constexpr uint8_t kMaxGear = 5U; // 6 gears, index 0..5.

    if (rpm >= cfg_.engine.max_rpm && current_gear < kMaxGear)
    {
        return static_cast<uint8_t>(current_gear + 1U);
    }
    if (rpm <= cfg_.engine.idle_rpm && current_gear > 0U)
    {
        return static_cast<uint8_t>(current_gear - 1U);
    }
    return current_gear;
}

}  // namespace cd::physics::vehicle
