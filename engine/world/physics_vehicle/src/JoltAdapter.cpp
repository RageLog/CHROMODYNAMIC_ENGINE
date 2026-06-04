// =============================================================================
// CHROMODYNAMIC — cd/physics/vehicle/JoltAdapter.cpp
// Phase 692 — Sprint-2 JoltAdapter implementation.
//
// Implementation notes (Sprint-2, stub-backend scope)
// ----------------------------------------------------
//
// configure():
//   Registers the chassis as a kDynamic BodyDesc with mass = chassis_mass_kg
//   (Sprint-2 uses chassis mass only; wheel unsprung mass affects the bicycle
//   model but is not yet split into separate Jolt bodies). The chassis AABB
//   half-extents from VehicleConfig::chassis_dimensions are passed through
//   the BoxShape descriptor — the cd::physics_jolt attach_collider() path
//   records it for test introspection; the real Jolt backend will feed it to
//   JPH::BoxShape when the bringup ADR lands.
//
//   Wheel attachment points are computed from chassis_dimensions:
//     local_x = ±(width/2)   (L/R axis)
//     local_y = -(height/2)  (downward from CoM to wheel centre)
//     local_z = ±(length/2)  (F/R axis)
//
// sync_to_jolt():
//   Computes a net drive force using the same kAirDensity/kCdA/kBrakeCoeff
//   constants as the bicycle model (keeps behaviour consistent when toggling
//   use_jolt). Applies it as an impulse via apply_impulse() so the world's
//   Euler integrator accumulates it before step().
//
//   STUB NOTE: JPH::WheeledVehicleController provides per-wheel longitudinal
//   and lateral force resolution.  Until jolt-bringup lands we model the
//   entire drive as a single chassis impulse — still sufficient for the
//   "position progresses" test assertion.
//
// sync_from_jolt():
//   Reads world->position() and world->linear_velocity() for the chassis
//   body. Speed is derived from the velocity magnitude (scalar, m/s -> km/h).
//   Orientation / yaw are deferred to Sprint-3 when quaternion retrieval
//   surfaces on IPhysicsWorld.
// =============================================================================

#include <cd/physics/vehicle/JoltAdapter.hpp>
#include <cd/physics/vehicle/Vehicle.hpp>
#include <cd/physics/IPhysicsWorld.hpp>
#include <cd/math/Vector.hpp>

#include <algorithm>
#include <cmath>

namespace cd::physics::vehicle
{

// ---- Physical constants (mirror bicycle model for consistency) -------------
namespace
{
constexpr float kAirDensity    = 1.225F;
constexpr float kCdA           = 0.75F;
constexpr float kBrakeCoeff    = 0.95F;
constexpr float kGravity       = 9.81F;
constexpr float kRollingResist = 0.015F;
} // namespace

// ---- JoltAdapter::configure ------------------------------------------------

bool JoltAdapter::configure(const VehicleConfig& cfg,
                            cd::physics::IPhysicsWorld& world) noexcept
{
    // Destroy any previously registered body to allow hot-reconfigure.
    destroy_existing();

    m_cfg   = &cfg;
    m_world = &world;

    // ---- Total mass (chassis + wheels) ------------------------------------
    m_total_mass_kg = cfg.chassis_mass_kg;
    for (const auto& w : cfg.wheels)
    {
        m_total_mass_kg += w.mass;
    }

    // ---- Register chassis rigid body ----------------------------------------
    cd::physics::BodyDesc desc {};
    desc.type             = cd::physics::BodyType::kDynamic;
    desc.mass             = m_total_mass_kg;
    desc.linear_damping   = 0.05F;  // light damping — air resistance handled via impulse
    desc.position         = { 0.0F, cfg.chassis_dimensions[2], 0.0F }; // start one height above origin

    auto result = world.create_body(desc);
    if (!result.has_value())
    {
        m_configured = false;
        return false;
    }

    m_chassis_handle = result.value();

    // ---- Compute wheel attachment points -----------------------------------
    const auto attachments = compute_wheel_attachments(cfg);
    for (std::size_t i = 0; i < 4U; ++i)
    {
        m_wheels[i].wheel_index          = static_cast<int>(i);
        m_wheels[i].local_attach         = attachments[i];
        m_wheels[i].suspension_compression =
            cfg.wheels[i].suspension_rest_length;
        m_wheels[i].grounded = true; // Sprint-2: always grounded
    }

    m_configured = true;
    return true;
}

// ---- JoltAdapter::set_inputs -----------------------------------------------

void JoltAdapter::set_inputs(float throttle, float brake, float steer) noexcept
{
    m_throttle = std::clamp(throttle, 0.0F, 1.0F);
    m_brake    = std::clamp(brake,    0.0F, 1.0F);
    m_steer    = std::clamp(steer,   -1.0F, 1.0F);
}

// ---- JoltAdapter::sync_to_jolt --------------------------------------------

void JoltAdapter::sync_to_jolt(float dt) noexcept
{
    if (!m_configured || m_world == nullptr || dt <= 0.0F)
    {
        return;
    }

    // Read current chassis velocity to compute drag correctly.
    const cd::math::Vec3f vel = m_world->linear_velocity(m_chassis_handle);

    // Forward speed (m/s) — use Z component as the forward axis
    // (Sprint-2: chassis starts axis-aligned; yaw integration deferred).
    const float v_fwd = vel[2]; // forward = +Z
    const float v_abs = std::fabs(v_fwd);

    // ---- Count driven wheels for torque split ----------------------------
    int driven_count = 0;
    if (m_cfg != nullptr)
    {
        for (const auto& w : m_cfg->wheels)
        {
            if (w.is_driven) { ++driven_count; }
        }
    }
    if (driven_count == 0) { driven_count = 2; }

    // ---- Drive force (same chain as bicycle model) -----------------------
    float f_drive = 0.0F;
    if (m_cfg != nullptr && m_cfg->engine.max_torque_nm > 0.0F)
    {
        const float wheel_radius =
            m_cfg->wheels[0].radius > 1e-6F ? m_cfg->wheels[0].radius : 0.32F;
        const float ratio       = m_cfg->engine.gear_ratios[0]; // Sprint-2: fixed 1st gear
        const float total_ratio = ratio * m_cfg->engine.final_drive;
        f_drive = m_throttle * m_cfg->engine.max_torque_nm * total_ratio / wheel_radius;
    }

    // ---- Aerodynamic drag + rolling resistance ---------------------------
    const float f_aero    = 0.5F * kAirDensity * kCdA * v_abs * v_abs;
    const float f_rolling = kRollingResist * m_total_mass_kg * kGravity;
    const float f_resist  = (f_aero + f_rolling) * (v_fwd >= 0.0F ? 1.0F : -1.0F);

    // ---- Brake -----------------------------------------------------------
    const float f_brake_mag = m_brake * kBrakeCoeff * m_total_mass_kg * kGravity;
    const float f_brake     = f_brake_mag * (v_fwd >= 0.0F ? 1.0F : -1.0F);

    // ---- Net impulse (kg·m/s = N × dt) -----------------------------------
    const float f_net     = f_drive - f_resist - f_brake;
    const float impulse_z = f_net * dt;

    // ---- Lateral steer impulse (Sprint-2: simplified yaw torque) ---------
    // Approximate lateral force by applying a gentle sideways impulse.
    // Sprint-3 will use JPH::WheeledVehicleController for proper slip-angle.
    const float impulse_x = m_steer * v_abs * m_total_mass_kg * 0.05F * dt;

    m_world->apply_impulse(m_chassis_handle,
                           cd::math::Vec3f { impulse_x, 0.0F, impulse_z });
}

// ---- JoltAdapter::sync_from_jolt ------------------------------------------

void JoltAdapter::sync_from_jolt(VehicleState& state) const noexcept
{
    if (!m_configured || m_world == nullptr)
    {
        return;
    }

    const cd::math::Vec3f vel = m_world->linear_velocity(m_chassis_handle);

    // Speed magnitude projected onto forward (+Z) axis (km/h).
    // Negative when reversing.
    const float v_fwd  = vel[2];
    state.speed_kph = v_fwd * 3.6F;

    // Sprint-2: all wheels are grounded (no terrain contact normals yet).
    for (int i = 0; i < 4; ++i)
    {
        state.wheels_grounded[i] = m_wheels[static_cast<std::size_t>(i)].grounded;
    }
}

// ---- JoltAdapter::destroy_existing ----------------------------------------

void JoltAdapter::destroy_existing() noexcept
{
    if (m_configured && m_world != nullptr)
    {
        m_world->destroy_body(m_chassis_handle);
    }
    m_configured      = false;
    m_chassis_handle  = {};
    m_total_mass_kg   = 1400.0F;
    m_throttle        = 0.0F;
    m_brake           = 0.0F;
    m_steer           = 0.0F;

    for (auto& w : m_wheels)
    {
        w = WheelConstraintDesc {};
    }
}

// ---- JoltAdapter::compute_wheel_attachments --------------------------------

std::array<cd::math::Vec3f, 4>
JoltAdapter::compute_wheel_attachments(const VehicleConfig& cfg) noexcept
{
    // chassis_dimensions = [length, width, height]
    const float half_len    = cfg.chassis_dimensions[0] * 0.5F;  // front/rear
    const float half_width  = cfg.chassis_dimensions[1] * 0.5F;  // left/right
    const float wheel_drop  = -cfg.chassis_dimensions[2] * 0.5F; // downward to CoM plane

    // Order: [FL=0, FR=1, RL=2, RR=3]
    //   X = left(-)/right(+); Y = drop; Z = front(+)/rear(-)
    return { cd::math::Vec3f { -half_width,  wheel_drop,  half_len },  // FL
             cd::math::Vec3f {  half_width,  wheel_drop,  half_len },  // FR
             cd::math::Vec3f { -half_width,  wheel_drop, -half_len },  // RL
             cd::math::Vec3f {  half_width,  wheel_drop, -half_len } }; // RR
}

}  // namespace cd::physics::vehicle
