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
//   Front wheels are steered by m_steer * steering_angle_max.  The heading
//   angle integrates m_steer * curvature * v * dt so VehicleState::steer
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
    m_cfg         = cfg;
    m_velocity_ms = 0.0F;
    m_throttle    = 0.0F;
    m_brake       = 0.0F;
    m_steer       = 0.0F;
    m_gear        = 0;
    m_state       = VehicleState {};

    // Reset any previously configured Jolt adapter; the caller must call
    // configure_jolt() again after configure() if use_jolt is still desired.
    m_jolt_adapter.reset();
    m_jolt_world = nullptr;
}

// ---- Vehicle::configure_jolt -----------------------------------------------

bool Vehicle::configure_jolt(cd::physics::IPhysicsWorld& world) noexcept
{
    // Lazily create the adapter on first call.
    if (!m_jolt_adapter)
    {
        m_jolt_adapter = std::make_unique<JoltAdapter>();
    }

    const bool ok = m_jolt_adapter->configure(m_cfg, world);
    if (ok)
    {
        m_jolt_world = &world;
    }
    else
    {
        // configure() failed — destroy adapter so tick() falls back to bicycle.
        m_jolt_adapter.reset();
        m_jolt_world = nullptr;
    }
    return ok;
}

// ---- Vehicle::set_input ----------------------------------------------------

void Vehicle::set_input(float throttle, float brake, float steer) noexcept
{
    // Clamp all inputs to valid ranges.
    m_throttle = std::clamp(throttle, 0.0F, 1.0F);
    m_brake    = std::clamp(brake,    0.0F, 1.0F);
    m_steer    = std::clamp(steer,   -1.0F, 1.0F);
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
    if (m_cfg.use_jolt && m_jolt_adapter && m_jolt_adapter->is_configured()
        && m_jolt_world != nullptr)
    {
        m_jolt_adapter->set_inputs(m_throttle, m_brake, m_steer);
        m_jolt_adapter->sync_to_jolt(dt);
        m_jolt_world->step(dt);
        m_jolt_adapter->sync_from_jolt(m_state);

        // Populate inputs in state (sync_from_jolt only touches speed/wheels).
        m_state.throttle = m_throttle;
        m_state.brake    = m_brake;
        m_state.steer    = m_steer;

        // RPM + gear estimation from speed read back (reuse bicycle helper).
        const float speed_ms = m_state.speed_kph / 3.6F;
        const float wheel_r  = m_cfg.wheels[0].radius > 1e-6F
                                   ? m_cfg.wheels[0].radius : 0.32F;
        const float omega    = speed_ms / wheel_r;
        const float raw_rpm  = engine_rpm_from_wheel(omega);
        m_gear               = auto_shift(raw_rpm, m_gear);
        m_state.gear         = m_gear;
        m_state.rpm          = raw_rpm;

        return; // Jolt path complete — skip bicycle model below.
    }
    // ---- Fall-through: Sprint-1 bicycle model (use_jolt false or unarmed) --

    // ---- Effective wheel radius (use first driven wheel; fallback FL) -------
    float wheel_radius = m_cfg.wheels[0].radius;
    for (const auto& w : m_cfg.wheels)
    {
        if (w.is_driven)
        {
            wheel_radius = w.radius;
            break;
        }
    }

    // ---- Effective mass (chassis + all wheel unsprung masses) --------------
    float total_mass = m_cfg.chassis_mass_kg;
    for (const auto& w : m_cfg.wheels)
    {
        total_mass += w.mass;
    }

    // ---- Count driven wheels -----------------------------------------------
    int driven_count = 0;
    for (const auto& w : m_cfg.wheels)
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
    m_gear = auto_shift(raw_rpm, m_gear);

    const float ratio       = m_cfg.engine.gear_ratios[m_gear];
    const float final_drive = m_cfg.engine.final_drive;
    const float total_ratio = ratio * final_drive;

    // ---- Engine RPM (clamped to operating window) ---------------------------
    const float e_rpm = std::clamp(std::fabs(omega) * total_ratio * (60.0F / (2.0F * std::numbers::pi_v<float>)),
                                   m_cfg.engine.idle_rpm,
                                   m_cfg.engine.max_rpm);

    // ---- Engine torque curve (flat + taper) ---------------------------------
    // Taper region starts at 80 % of max_rpm.
    const float taper_start = 0.8F * m_cfg.engine.max_rpm;
    float torque_fraction   = 1.0F;
    if (e_rpm > taper_start)
    {
        torque_fraction = 1.0F - (e_rpm - taper_start) /
                                 (m_cfg.engine.max_rpm - taper_start);
        torque_fraction = std::clamp(torque_fraction, 0.0F, 1.0F);
    }

    const float engine_torque = m_throttle * m_cfg.engine.max_torque_nm * torque_fraction;

    // ---- Drive force at wheel (split across driven wheels) ------------------
    // F = T * total_ratio / r  (each driven wheel); here we net the sum.
    const float f_drive = (engine_torque * total_ratio) / wheel_radius;

    // ---- Aerodynamic drag + rolling resistance ------------------------------
    const float v_abs     = std::fabs(m_velocity_ms);
    const float f_aero    = 0.5F * kAirDensity * kCdA * v_abs * v_abs;
    const float f_rolling = kRollingResist * total_mass * kGravity;
    // Both always oppose motion direction.
    const float f_resist  = (f_aero + f_rolling) * (m_velocity_ms >= 0.0F ? 1.0F : -1.0F);

    // ---- Brake force (oppose velocity) --------------------------------------
    const float f_brake_mag = m_brake * kBrakeCoeff * total_mass * kGravity;
    const float f_brake     = f_brake_mag * (m_velocity_ms >= 0.0F ? 1.0F : -1.0F);

    // ---- Net force and acceleration -----------------------------------------
    const float f_net = f_drive - f_resist - f_brake;
    const float accel = f_net / total_mass;

    // Semi-implicit Euler: integrate velocity.
    m_velocity_ms += accel * dt;

    // Clamp to rest when nearly stopped and no throttle applied.
    if (m_throttle < 0.01F && std::fabs(m_velocity_ms) < kRestThreshold)
    {
        m_velocity_ms = 0.0F;
    }

    // ---- Update state output ------------------------------------------------
    m_state.speed_kph = m_velocity_ms * 3.6F;
    m_state.rpm       = e_rpm;
    m_state.gear      = m_gear;
    m_state.throttle  = m_throttle;
    m_state.brake     = m_brake;
    m_state.steer     = m_steer;

    // Sprint-1: all wheels grounded (no terrain query).
    for (int i = 0; i < 4; ++i)
    {
        m_state.wheels_grounded[i] = true;
    }
}

// ---- Vehicle::wheel_omega --------------------------------------------------

float Vehicle::wheel_omega() const noexcept
{
    float wheel_radius = m_cfg.wheels[0].radius;
    for (const auto& w : m_cfg.wheels)
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
    return m_velocity_ms / wheel_radius;
}

// ---- Vehicle::engine_rpm_from_wheel ----------------------------------------

float Vehicle::engine_rpm_from_wheel(float wheel_omega_rad_s) const noexcept
{
    const float ratio       = m_cfg.engine.gear_ratios[m_gear];
    const float final_drive = m_cfg.engine.final_drive;
    const float total_ratio = ratio * final_drive;
    // rpm = (omega_wheel * total_ratio) * (60 / 2pi)
    const float rpm = std::fabs(wheel_omega_rad_s) * total_ratio * (60.0F / (2.0F * std::numbers::pi_v<float>));
    return std::clamp(rpm, m_cfg.engine.idle_rpm, m_cfg.engine.max_rpm);
}

// ---- Vehicle::auto_shift ---------------------------------------------------

uint8_t Vehicle::auto_shift(float rpm, uint8_t current_gear) const noexcept
{
    constexpr uint8_t kMaxGear = 5U; // 6 gears, index 0..5.

    if (rpm >= m_cfg.engine.max_rpm && current_gear < kMaxGear)
    {
        return static_cast<uint8_t>(current_gear + 1U);
    }
    if (rpm <= m_cfg.engine.idle_rpm && current_gear > 0U)
    {
        return static_cast<uint8_t>(current_gear - 1U);
    }
    return current_gear;
}

}  // namespace cd::physics::vehicle
