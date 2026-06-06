// =============================================================================
// CHROMODYNAMIC — cd/physics/vehicle/JoltAdapter.cpp
// Phase 692  — Sprint-2 JoltAdapter implementation.
// Phase 786  — FINALE A10: real Jolt 5.x VehicleConstraint link.
//
// Implementation dispatch
// -----------------------
// When cd::physics_jolt::is_stub_backend() == false (real Jolt linked):
//   configure()      — creates chassis body + JPH::VehicleConstraint via the
//                      cd::physics_jolt::create_vehicle_constraint() side-band.
//                      The constraint owns the WheeledVehicleController and is
//                      registered as a PhysicsStepListener automatically.
//   sync_to_jolt()   — calls cd::physics_jolt::set_vehicle_driver_input()
//                      which forwards to WheeledVehicleController::SetDriverInput.
//   sync_from_jolt() — reads chassis velocity via IPhysicsWorld::linear_velocity()
//                      and wheel contact from cd::physics_jolt::get_vehicle_wheel_contact().
//
// When is_stub_backend() == true (Euler fallback):
//   The impulse-based path from Sprint-2 remains active.  configure() still
//   creates a chassis body; sync_to/from use apply_impulse + linear_velocity.
//
// VehicleConstraintDesc is populated from VehicleConfig fields so VehicleConfig
// remains the single authoring surface; no Jolt headers are included here.
//
// MOMENT: car drives on glTF terrain with production physics — wheels detect
//   terrain contact, controller drives engine torque to the driven axle,
//   suspension springs react to height variation.
// =============================================================================

#include <cd/physics/vehicle/JoltAdapter.hpp>
#include <cd/physics/vehicle/Vehicle.hpp>
#include <cd/physics/IPhysicsWorld.hpp>
#include <cd/physics_jolt/JoltWorld.hpp>
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
    // Spawn the chassis one chassis-height above the origin so it lands on the
    // ground plane (or terrain) without immediately penetrating it.
    cd::physics::BodyDesc desc {};
    desc.type             = cd::physics::BodyType::kDynamic;
    desc.mass             = m_total_mass_kg;
    desc.linear_damping   = 0.05F;  // light damping — aerodynamic drag via impulse/controller
    desc.position         = { 0.0F, cfg.chassis_dimensions[2], 0.0F };

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
        m_wheels[i].grounded = true; // default; overwritten in sync_from_jolt when real Jolt active
    }

    // ---- Real Jolt path: create VehicleConstraint + WheeledVehicleController
    if (!cd::physics_jolt::is_stub_backend())
    {
        cd::physics_jolt::VehicleConstraintDesc vd {};
        vd.total_mass_kg = m_total_mass_kg;
        vd.chassis_half[0] = cfg.chassis_dimensions[0] * 0.5F;
        vd.chassis_half[1] = cfg.chassis_dimensions[1] * 0.5F;
        vd.chassis_half[2] = cfg.chassis_dimensions[2] * 0.5F;
        vd.max_torque_nm   = cfg.engine.max_torque_nm;
        vd.idle_rpm        = cfg.engine.idle_rpm;
        vd.max_rpm         = cfg.engine.max_rpm;
        for (std::size_t i = 0; i < 6U; ++i)
        {
            vd.gear_ratios[i] = cfg.engine.gear_ratios[i];
        }
        vd.final_drive = cfg.engine.final_drive;

        for (std::size_t i = 0; i < 4U; ++i)
        {
            const auto& wc = cfg.wheels[i];
            auto& wd       = vd.wheels[i];
            wd.local[0]    = attachments[i].x;
            wd.local[1]    = attachments[i].y;
            wd.local[2]    = attachments[i].z;
            wd.radius      = wc.radius;
            wd.width       = wc.radius * 0.4F;  // approximate from radius
            wd.max_steer   = wc.steering_angle_max;
            wd.suspension_rest      = wc.suspension_rest_length;
            wd.suspension_stiffness = wc.suspension_stiffness;
            wd.suspension_damping   = wc.damping;
            wd.is_driven            = wc.is_driven;
        }

        const auto vc_handle = cd::physics_jolt::create_vehicle_constraint(
            world, m_chassis_handle, vd);
        m_vehicle_constraint_idx = vc_handle.index;
    }
    // Stub path: m_vehicle_constraint_idx remains 0 (invalid); impulse path used.

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

    // ---- Real Jolt path: delegate to WheeledVehicleController -------------
    // The VehicleConstraint is a PhysicsStepListener and drives the engine
    // + wheel contact resolution internally. We only need to push the driver
    // inputs; JPH handles the torque-to-wheel chain.
    if (m_vehicle_constraint_idx != 0U)
    {
        cd::physics_jolt::set_vehicle_driver_input(
            *m_world,
            cd::physics_jolt::VehicleConstraintHandle { m_vehicle_constraint_idx },
            m_throttle,   // forward ∈ [0,1]
            m_steer,      // right   ∈ [-1,1]
            m_brake       // brake   ∈ [0,1]
        );
        return;  // controller handles forces; impulse path skipped
    }

    // ---- Stub/fallback path: manual impulse (Sprint-2 behaviour) ----------

    // Read current chassis velocity to compute drag correctly.
    const cd::math::Vec3f vel = m_world->linear_velocity(m_chassis_handle);

    // Forward speed (m/s) — use Z component as the forward axis
    // (chassis starts axis-aligned; yaw integration deferred to Sprint-3).
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
        const float ratio       = m_cfg->engine.gear_ratios[0]; // fixed 1st gear on fallback path
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

    // ---- Lateral steer impulse (simplified yaw torque) -------------------
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

    // Speed projected onto forward (+Z) axis (km/h). Negative = reversing.
    const float v_fwd  = vel[2];
    state.speed_kph = v_fwd * 3.6F;

    // ---- Real Jolt path: query per-wheel ground contact -------------------
    if (m_vehicle_constraint_idx != 0U)
    {
        const cd::physics_jolt::VehicleConstraintHandle vc_handle {
            m_vehicle_constraint_idx };
        for (int i = 0; i < 4; ++i)
        {
            state.wheels_grounded[i] = cd::physics_jolt::get_vehicle_wheel_contact(
                *m_world, vc_handle, i);
        }
        return;
    }

    // ---- Stub/fallback path: use cached grounded flags --------------------
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
        // Tear down vehicle constraint before the chassis body (the constraint
        // holds a reference to the body; order matters for JPH ref-counting).
        if (m_vehicle_constraint_idx != 0U)
        {
            cd::physics_jolt::destroy_vehicle_constraint(
                *m_world,
                cd::physics_jolt::VehicleConstraintHandle { m_vehicle_constraint_idx });
            m_vehicle_constraint_idx = 0U;
        }
        m_world->destroy_body(m_chassis_handle);
    }
    m_configured             = false;
    m_chassis_handle         = {};
    m_total_mass_kg          = 1400.0F;
    m_throttle               = 0.0F;
    m_brake                  = 0.0F;
    m_steer                  = 0.0F;
    m_vehicle_constraint_idx = 0U;

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
