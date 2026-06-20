// =============================================================================
// CHROMODYNAMIC — cd/physics/vehicle/Vehicle.cpp
// Phase 670 — cd::physics::vehicle Sprint-1 implementation.
// Phase 692 — Sprint-2: configure_jolt() + Jolt delegation path in tick().
//
// Implementation notes
// --------------------
//
// Bicycle model (longitudinal):
//   The vehicle is treated as a 1-DOF point mass moving along a forward axis.
//
// Lateral model (dynamic linear-tyre single-track) — see compute_lateral():
//   The kinematic yaw rate v*tan(delta)/L is corrected by the steady-state
//   linear-tyre single-track model. With axle cornering stiffness C_f, C_r and
//   CoM-to-axle distances l_f, l_r the understeer gradient is
//       K = m/L * (l_r/C_f - l_f/C_r)   [rad per m/s^2]
//   and the achievable lateral acceleration is the kinematic demand clamped to
//   the friction circle a_lat_max = mu * g. This is bounded, closed-form and
//   deterministic — no per-frame ODE integration of the (v_y, r) state needed.
//
// SEAL — full Pacejka "Magic Formula" tyre model:
//   A complete transient tyre model (per-wheel vertical load from suspension +
//   longitudinal load transfer, combined longitudinal/lateral slip on the
//   traction circle, relaxation-length first-order lag, camber thrust) needs
//   per-wheel normal forces that only the Jolt ray-cast suspension contact can
//   supply (the gated Sprint-3 path) and is a multi-week tyre-model effort. It
//   is intentionally NOT implemented here; the linear-tyre + friction-circle
//   model above is the tractable, fully-tested subset. See README §Sprint-3.
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
#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <numbers>

namespace cd::physics::vehicle
{

// ---- Vehicle ctor/dtor (defined here so ~unique_ptr<JoltAdapter> sees the
//      complete type; JoltAdapter.hpp is included above). PIMPL pattern. ---

Vehicle::Vehicle() noexcept = default;
Vehicle::~Vehicle() noexcept = default;

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

        // Lateral response from the read-back forward speed (same kinematic
        // bicycle model as the CPU path) so yaw_rate/lateral_accel are
        // populated regardless of which integrator drove the chassis.
        compute_lateral(speed_ms, state_);

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
    // Guard against a degenerate (zero/negative) total mass: an unphysical
    // config must not divide by zero or fling the body. Treat as inert.
    const float f_net = f_drive - f_resist - f_brake;
    const float accel = total_mass > 1e-6F ? f_net / total_mass : 0.0F;

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

    // ---- Lateral response (kinematic bicycle, linear small-slip) ------------
    compute_lateral(velocity_ms_, state_);

    // Sprint-1: all wheels grounded (no terrain query).
    for (bool& grounded : state_.wheels_grounded)
    {
        grounded = true;
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

// ---- Vehicle::wheelbase ----------------------------------------------------

float Vehicle::wheelbase() const noexcept
{
    // chassis_dimensions = [length, width, height]; the wheelbase is the
    // front-to-rear axle distance. With axles at ~the chassis ends, a length-
    // derived estimate (0.85 * chassis length) is a good default and avoids a
    // separate authoring field. Clamp to a small positive floor so the lateral
    // formula never divides by zero for a degenerate config.
    const float length_based = 0.85F * cfg_.chassis_dimensions[0];
    return std::max(length_based, 0.5F);
}

// ---- Vehicle::axle_distances -----------------------------------------------

std::array<float, 2> Vehicle::axle_distances() const noexcept
{
    // CoM-to-axle longitudinal distances {l_f, l_r}. Axle longitudinal offsets
    // are not separately authored, so assume a static 50/50 split about the
    // CoM: l_f = l_r = wheelbase / 2. Sums to wheelbase() by construction.
    const float half_wb = 0.5F * wheelbase();
    return { half_wb, half_wb };
}

// ---- Vehicle::axle_cornering_stiffness -------------------------------------

std::array<float, 2> Vehicle::axle_cornering_stiffness() const noexcept
{
    // Front axle = wheels that can steer (steering_angle_max > 0); rear axle =
    // the rest. Sum the per-wheel cornering stiffness so a 2-tyre axle is twice
    // a single tyre. Clamp each contribution to be non-negative.
    float c_front = 0.0F;
    float c_rear  = 0.0F;
    for (const auto& w : cfg_.wheels)
    {
        const float c = std::max(w.cornering_stiffness, 0.0F);
        if (w.steering_angle_max > 0.0F)
        {
            c_front += c;
        }
        else
        {
            c_rear += c;
        }
    }
    return { c_front, c_rear };
}

// ---- Vehicle::compute_lateral ----------------------------------------------

void Vehicle::compute_lateral(float v_ms, VehicleState& out) const noexcept
{
    // Front-wheel steer angle delta = steer_input * max steer angle of a
    // steering (non-zero steering_angle_max) front wheel. Use the first wheel
    // that can steer; fall back to the FL config value.
    float max_steer = cfg_.wheels[0].steering_angle_max;
    for (const auto& w : cfg_.wheels)
    {
        if (w.steering_angle_max > 0.0F)
        {
            max_steer = w.steering_angle_max;
            break;
        }
    }

    const float delta = steer_ * max_steer;  // radians, signed

    // ---- (1) Kinematic bicycle (geometry) ---------------------------------
    //   yaw_rate    = v * tan(delta) / wheelbase
    //   lateral_acc = v * yaw_rate          (= v^2 * tan(delta) / wheelbase)
    // At standstill (v ~ 0) both are zero; reversing (v < 0) flips yaw sign,
    // which is the physically correct behaviour for a kinematic bicycle.
    // wheelbase() is clamped strictly positive, so this never divides by zero.
    const float wheelbase_m = wheelbase();
    const float tan_delta   = std::tan(delta);
    const float yaw_kin     = (v_ms * tan_delta) / wheelbase_m;
    const float lat_kin     = v_ms * yaw_kin;

    // ---- (2) Static understeer gradient -----------------------------------
    // K = m/L * (l_r/C_f - l_f/C_r)  [rad per m/s^2]. Front/rear cornering
    // stiffness or mass may be degenerate (zero) for an unphysical config;
    // guard every divisor so K stays finite (treated as neutral there).
    float total_mass = cfg_.chassis_mass_kg;
    for (const auto& w : cfg_.wheels)
    {
        total_mass += w.mass;
    }
    const auto dist  = axle_distances();
    const auto stiff = axle_cornering_stiffness();
    const float l_f = dist[0];
    const float l_r = dist[1];
    const float c_f = stiff[0];
    const float c_r = stiff[1];

    float k_understeer = 0.0F;
    if (total_mass > 1e-6F && c_f > 1e-3F && c_r > 1e-3F)
    {
        k_understeer = (total_mass / wheelbase_m) * (l_r / c_f - l_f / c_r);
    }

    // ---- (3) Friction-circle grip limit -----------------------------------
    // a_lat_max = mu * g, using the minimum friction coefficient across wheels
    // (the weakest tyre governs the slide onset). Always positive.
    float mu = cfg_.wheels[0].friction_coefficient;
    for (const auto& w : cfg_.wheels)
    {
        mu = std::min(mu, w.friction_coefficient);
    }
    mu = std::max(mu, 0.0F);
    const float a_lat_max = mu * kGravity;

    // ---- (4) Dynamic correction: clamp demand to the traction circle -------
    // The kinematic demand is the lateral acceleration the geometry asks for.
    // Real tyres can only deliver up to a_lat_max; beyond that the vehicle
    // slides and the centripetal acceleration saturates. The achievable yaw
    // rate is scaled by the same factor so a_lat = v * yaw stays consistent.
    const float demand_abs = std::fabs(lat_kin);
    const float grip_ratio = a_lat_max > 1e-6F ? demand_abs / a_lat_max : 0.0F;
    const bool  limited     = grip_ratio >= 1.0F;

    float lat_acc = lat_kin;
    float yaw_rate = yaw_kin;
    if (limited && demand_abs > 1e-6F)
    {
        const float scale = a_lat_max / demand_abs;  // < 1 at saturation
        lat_acc  = lat_kin * scale;
        yaw_rate = yaw_kin * scale;
    }

    // ---- (5) Steady-state slip angles (linear-tyre) -----------------------
    // The lateral tyre force per axle balances the centripetal demand split by
    // the static load: F_f = m * l_r/L * a_lat, F_r = m * l_f/L * a_lat. The
    // slip angle is alpha = F / C. Sign follows the achieved lateral accel.
    float alpha_f = 0.0F;
    float alpha_r = 0.0F;
    if (total_mass > 1e-6F && c_f > 1e-3F && c_r > 1e-3F)
    {
        const float f_front = total_mass * (l_r / wheelbase_m) * lat_acc;
        const float f_rear  = total_mass * (l_f / wheelbase_m) * lat_acc;
        alpha_f = f_front / c_f;
        alpha_r = f_rear / c_r;
    }

    out.yaw_rate_rad_s        = yaw_rate;
    out.lateral_accel_ms2     = lat_acc;
    out.slip_angle_front_rad  = alpha_f;
    out.slip_angle_rear_rad   = alpha_r;
    out.understeer_gradient   = k_understeer;
    out.max_lateral_accel_ms2 = a_lat_max;
    out.lateral_grip_ratio    = grip_ratio;
    out.is_traction_limited   = limited;
}

}  // namespace cd::physics::vehicle
