// =============================================================================
// CHROMODYNAMIC — cd/physics_jolt/JoltWorld.cpp
// FINALE-3 / W2B-C2 (phase748) — Jolt 5.x real backend + Euler stub fallback.
//
// Compile-time dispatch:
//   CD_PHYSICS_JOLT_REAL=1  → JoltWorldReal (JPH::PhysicsSystem)
//   CD_PHYSICS_JOLT_STUB=1  → JoltWorldStub (semi-implicit Euler delegate)
//
// Vendor boundary (ADR-20260530 §B):
//   * <Jolt/Jolt.h> only included inside this TU, never in the public header.
//   * Public factory returns std::unique_ptr<cd::physics::IPhysicsWorld>.
//   * Downstream callers never see JPH::* symbols.
//
// SOTA references:
//   * Rouwe, "Architecting Jolt Physics for Horizon Forbidden West"
//     (GDC 2022) — PhysicsSystem init sequence and layer filter shapes.
//   * jrouwe/JoltPhysics HelloWorld.cpp — minimal bootstrap template.
//   * ADR-008 (Physics Architecture) — adapter pattern + Jolt default.
//   * ADR-015 (Concurrency / Job System) — single-pool rule.
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

// =============================================================================
// REAL BACKEND — JPH::PhysicsSystem
// =============================================================================
#if defined(CD_PHYSICS_JOLT_REAL)

// Jolt headers — only in this TU, never in the public header.
#include <Jolt/Jolt.h>

JPH_SUPPRESS_WARNINGS

#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemSingleThreaded.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Vehicle/VehicleCollisionTester.h>
#include <Jolt/Physics/Vehicle/VehicleConstraint.h>
#include <Jolt/Physics/Vehicle/WheeledVehicleController.h>
#include <Jolt/RegisterTypes.h>

#include <atomic>
#include <thread>

namespace cd::physics_jolt
{

namespace
{

// ---------------------------------------------------------------------------
// JoltGlobalInit — reference-counted one-time Jolt global initializer.
//
// Jolt requires RegisterDefaultAllocator + Factory + RegisterTypes to be
// called ONCE per process (not per-PhysicsSystem). This RAII guard uses an
// atomic refcount so multiple JoltWorldReal instances in the same process
// share a single init/shutdown cycle.
// ---------------------------------------------------------------------------
class JoltGlobalInit
{
public:
    JoltGlobalInit()
    {
        const auto prev = s_refcount_.fetch_add(1, std::memory_order_acq_rel);
        if (prev == 0)
        {
            JPH::RegisterDefaultAllocator();
            JPH::Factory::sInstance = new JPH::Factory {};  // NOLINT(cppcoreguidelines-owning-memory)
            JPH::RegisterTypes();
        }
    }

    ~JoltGlobalInit()
    {
        const auto prev = s_refcount_.fetch_sub(1, std::memory_order_acq_rel);
        if (prev == 1)
        {
            JPH::UnregisterTypes();
            delete JPH::Factory::sInstance;  // NOLINT(cppcoreguidelines-owning-memory)
            JPH::Factory::sInstance = nullptr;
        }
    }

    JoltGlobalInit(const JoltGlobalInit&) = delete;
    JoltGlobalInit& operator=(const JoltGlobalInit&) = delete;

private:
    static std::atomic<int> s_refcount_;
};

// Out-of-line definition — zero-initialised before any dynamic init runs.
std::atomic<int> JoltGlobalInit::s_refcount_ { 0 };

// ---------------------------------------------------------------------------
// Broadphase / object-layer setup (minimal 2-layer scheme matching Jolt's
// HelloWorld recommendation — "non-moving" and "moving").
// ---------------------------------------------------------------------------
namespace layers
{
    static constexpr JPH::ObjectLayer kNonMoving = 0;
    static constexpr JPH::ObjectLayer kMoving    = 1;
    static constexpr JPH::uint        kCount     = 2;
}  // namespace layers

namespace bplayers
{
    static constexpr JPH::BroadPhaseLayer kNonMoving { 0 };
    static constexpr JPH::BroadPhaseLayer kMoving    { 1 };
    static constexpr JPH::uint            kCount     = 2;
}  // namespace bplayers

class BPLayerInterface final : public JPH::BroadPhaseLayerInterface
{
public:
    [[nodiscard]] JPH::uint GetNumBroadPhaseLayers() const override
    {
        return bplayers::kCount;
    }

    [[nodiscard]] JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer layer) const override
    {
        JPH_ASSERT(layer < layers::kCount);
        return (layer == layers::kNonMoving) ? bplayers::kNonMoving : bplayers::kMoving;
    }

#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
    [[nodiscard]] const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer layer) const override
    {
        return (layer.GetValue() == 0) ? "NON_MOVING" : "MOVING";
    }
#endif
};

class ObjVsBPFilter final : public JPH::ObjectVsBroadPhaseLayerFilter
{
public:
    [[nodiscard]] bool ShouldCollide(
        JPH::ObjectLayer obj, JPH::BroadPhaseLayer bp) const override
    {
        switch (obj)
        {
            case layers::kNonMoving:
                return (bp.GetValue() == bplayers::kMoving.GetValue());
            case layers::kMoving:
                return true;
            default:
                JPH_ASSERT(false);
                return false;
        }
    }
};

class ObjPairFilter final : public JPH::ObjectLayerPairFilter
{
public:
    [[nodiscard]] bool ShouldCollide(JPH::ObjectLayer a, JPH::ObjectLayer b) const override
    {
        switch (a)
        {
            case layers::kNonMoving:
                return (b == layers::kMoving);
            case layers::kMoving:
                return true;
            default:
                JPH_ASSERT(false);
                return false;
        }
    }
};

// ---------------------------------------------------------------------------
// Map cd::physics::BodyType to Jolt motion type + layer
// ---------------------------------------------------------------------------
[[nodiscard]] JPH::EMotionType to_jph_motion(cd::physics::BodyType t) noexcept
{
    switch (t)
    {
        case cd::physics::BodyType::kStatic:
            return JPH::EMotionType::Static;
        case cd::physics::BodyType::kKinematic:
            return JPH::EMotionType::Kinematic;
        case cd::physics::BodyType::kDynamic:
        default:
            return JPH::EMotionType::Dynamic;
    }
}

[[nodiscard]] JPH::ObjectLayer to_jph_layer(cd::physics::BodyType t) noexcept
{
    return (t == cd::physics::BodyType::kStatic) ? layers::kNonMoving : layers::kMoving;
}

// ---------------------------------------------------------------------------
// Handle table — maps cd::physics::BodyHandle::index() to JPH::BodyID
// ---------------------------------------------------------------------------
class HandleTable
{
public:
    void insert(std::uint32_t idx, JPH::BodyID id) { table_[idx] = id; }
    void erase(std::uint32_t idx) { table_.erase(idx); }

    [[nodiscard]] JPH::BodyID find(std::uint32_t idx) const noexcept
    {
        auto it = table_.find(idx);
        return (it != table_.end()) ? it->second : JPH::BodyID {};
    }

    [[nodiscard]] std::size_t size() const noexcept { return table_.size(); }

private:
    std::unordered_map<std::uint32_t, JPH::BodyID> table_;
};

// ---------------------------------------------------------------------------
// JoltWorldReal — JPH::PhysicsSystem-backed IPhysicsWorld
// ---------------------------------------------------------------------------
class JoltWorldReal final : public cd::physics::IPhysicsWorld
{
public:
    explicit JoltWorldReal(JoltBackendConfig cfg)
        : config_ { cfg }
        // global_init_ must be first: increments refcount before any JPH call.
        , temp_alloc_ { 10 * 1024 * 1024 }  // 10 MB (Jolt HelloWorld recommendation)
    {
        // JobSystemSingleThreaded takes only inMaxJobs (Jolt 5.x API).
        // Single-threaded for bring-up; phase749 routes through IJobSystem.
        job_system_.Init(JPH::cMaxPhysicsJobs);

        physics_.Init(
            cfg.max_bodies,
            cfg.num_body_mutexes,
            cfg.max_body_pairs,
            cfg.max_contact_constraints,
            bp_layer_iface_,
            obj_vs_bp_filter_,
            obj_pair_filter_
        );

        // Default Earth gravity
        physics_.SetGravity(JPH::Vec3 { 0.0F, -9.81F, 0.0F });
    }

    ~JoltWorldReal() override = default;
    // JoltGlobalInit dtor decrements refcount; last world out shuts Jolt down.

    // ---- World-level configuration -----------------------------------------

    void set_gravity(cd::math::Vec3f g) noexcept override
    {
        physics_.SetGravity(JPH::Vec3 { g.x, g.y, g.z });
    }

    [[nodiscard]] cd::math::Vec3f gravity() const noexcept override
    {
        const auto g = physics_.GetGravity();
        return { g.GetX(), g.GetY(), g.GetZ() };
    }

    // ---- Body lifecycle ----------------------------------------------------

    [[nodiscard]] cd::core::Result<cd::physics::BodyHandle>
    create_body(const cd::physics::BodyDesc& desc) override
    {
        // Build a default unit-box shape for all new bodies.
        // Real callers will override via attach_collider(); this gives every
        // body a non-zero AABB so the broadphase can track it.
        JPH::BoxShapeSettings shape_settings { JPH::Vec3 { 0.5F, 0.5F, 0.5F } };
        shape_settings.mConvexRadius = 0.0F;
        auto shape_result = shape_settings.Create();
        if (shape_result.HasError())
        {
            return std::unexpected(cd::physics::physics_errors::make(
                cd::physics::physics_errors::Code::kBackendError,
                "JoltWorldReal::create_body: shape creation failed"));
        }

        const auto motion = to_jph_motion(desc.type);
        const auto layer  = to_jph_layer(desc.type);

        JPH::BodyCreationSettings settings {
            shape_result.Get(),
            JPH::RVec3 { static_cast<JPH::Real>(desc.position.x),
                         static_cast<JPH::Real>(desc.position.y),
                         static_cast<JPH::Real>(desc.position.z) },
            JPH::Quat::sIdentity(),
            motion,
            layer
        };

        settings.mLinearVelocity =
            JPH::Vec3 { desc.linear_velocity.x, desc.linear_velocity.y, desc.linear_velocity.z };

        if (desc.type == cd::physics::BodyType::kDynamic)
        {
            settings.mMassPropertiesOverride.mMass = desc.mass > 0.0F ? desc.mass : 1.0F;
            settings.mOverrideMassProperties =
                JPH::EOverrideMassProperties::CalculateInertia;
            settings.mLinearDamping  = desc.linear_damping > 0.0F
                                           ? desc.linear_damping
                                           : config_.default_linear_damping;
            settings.mAngularDamping = config_.default_angular_damping;
        }

        JPH::BodyInterface& bi = physics_.GetBodyInterface();
        JPH::Body* body = bi.CreateBody(settings);
        if (body == nullptr)
        {
            return std::unexpected(cd::physics::physics_errors::make(
                cd::physics::physics_errors::Code::kBackendError,
                "JoltWorldReal::create_body: body pool exhausted"));
        }

        bi.AddBody(body->GetID(), JPH::EActivation::Activate);

        // Store rigid component + map handle -> JPH::BodyID
        const auto idx = static_cast<std::uint32_t>(next_idx_++);
        handles_.insert(idx, body->GetID());
        rigid_components_.emplace(idx, body_desc_to_component(desc, config_));

        return cd::physics::BodyHandle { idx };
    }

    void destroy_body(cd::physics::BodyHandle h) override
    {
        const JPH::BodyID jid = handles_.find(h.index());
        if (!jid.IsInvalid())
        {
            JPH::BodyInterface& bi = physics_.GetBodyInterface();
            bi.RemoveBody(jid);
            bi.DestroyBody(jid);
            handles_.erase(h.index());
            rigid_components_.erase(h.index());
            colliders_.erase(h.index());
        }
        // Remove joints referencing this body
        joints_.erase(
            std::remove_if(
                joints_.begin(), joints_.end(),
                [&](const components::JointComponent& j) noexcept {
                    return j.body_a.index() == h.index() || j.body_b.index() == h.index();
                }),
            joints_.end());
    }

    [[nodiscard]] std::size_t body_count() const noexcept override
    {
        return handles_.size();
    }

    // ---- Per-body queries --------------------------------------------------

    [[nodiscard]] cd::math::Vec3f position(cd::physics::BodyHandle h) const override
    {
        const JPH::BodyID jid = handles_.find(h.index());
        if (jid.IsInvalid())
            return {};
        const auto p = physics_.GetBodyInterface().GetCenterOfMassPosition(jid);
        return { p.GetX(), p.GetY(), p.GetZ() };
    }

    [[nodiscard]] cd::math::Vec3f linear_velocity(cd::physics::BodyHandle h) const override
    {
        const JPH::BodyID jid = handles_.find(h.index());
        if (jid.IsInvalid())
            return {};
        const auto v = physics_.GetBodyInterface().GetLinearVelocity(jid);
        return { v.GetX(), v.GetY(), v.GetZ() };
    }

    [[nodiscard]] cd::physics::BodyType body_type(cd::physics::BodyHandle h) const override
    {
        auto it = rigid_components_.find(h.index());
        return (it != rigid_components_.end()) ? it->second.type : cd::physics::BodyType::kStatic;
    }

    // ---- Per-body mutation -------------------------------------------------

    void set_position(cd::physics::BodyHandle h, cd::math::Vec3f p) override
    {
        const JPH::BodyID jid = handles_.find(h.index());
        if (!jid.IsInvalid())
        {
            physics_.GetBodyInterface().SetPosition(
                jid,
                JPH::RVec3 { static_cast<JPH::Real>(p.x),
                             static_cast<JPH::Real>(p.y),
                             static_cast<JPH::Real>(p.z) },
                JPH::EActivation::DontActivate);
        }
    }

    void set_linear_velocity(cd::physics::BodyHandle h, cd::math::Vec3f v) override
    {
        const JPH::BodyID jid = handles_.find(h.index());
        if (!jid.IsInvalid())
        {
            physics_.GetBodyInterface().SetLinearVelocity(
                jid, JPH::Vec3 { v.x, v.y, v.z });
        }
    }

    void apply_impulse(cd::physics::BodyHandle h, cd::math::Vec3f impulse) override
    {
        // Kinematic bodies must not be affected by external impulses
        // (ADR-008 motion-type rule + test expectation).
        auto it = rigid_components_.find(h.index());
        if (it != rigid_components_.end()
            && it->second.type == cd::physics::BodyType::kKinematic)
        {
            return;
        }
        const JPH::BodyID jid = handles_.find(h.index());
        if (!jid.IsInvalid())
        {
            physics_.GetBodyInterface().AddImpulse(
                jid, JPH::Vec3 { impulse.x, impulse.y, impulse.z });
        }
    }

    void apply_force(cd::physics::BodyHandle h, cd::math::Vec3f force) override
    {
        const JPH::BodyID jid = handles_.find(h.index());
        if (!jid.IsInvalid())
        {
            physics_.GetBodyInterface().AddForce(
                jid, JPH::Vec3 { force.x, force.y, force.z });
        }
    }

    // ---- Simulation --------------------------------------------------------

    void step(float dt) override
    {
        // Jolt's Update() performs broadphase, narrowphase, constraint solver,
        // and integration in one call. collisionSteps=1 is correct for 60 Hz.
        physics_.Update(dt, 1, &temp_alloc_, &job_system_);
    }

    // ---- Stub-compatible side-band (colliders / joints / config) -----------

    void register_collider(
        cd::physics::BodyHandle owner, const components::ColliderComponent& col)
    {
        colliders_[owner.index()].push_back(col);
        // TODO(phase749): swap the body's shape via BodyInterface::SetShape once
        //   the shape-cache mapper is wired (shape_desc_to_jph not yet impl).
    }

    [[nodiscard]] std::size_t collider_count(cd::physics::BodyHandle owner) const noexcept
    {
        auto it = colliders_.find(owner.index());
        return it == colliders_.end() ? 0U : it->second.size();
    }

    void register_joint(const components::JointComponent& j)
    {
        joints_.push_back(j);
        // TODO(phase749): wire into JPH::ConstraintManager (ADR-20260530 §C).
    }

    [[nodiscard]] std::size_t joint_count() const noexcept
    {
        return joints_.size();
    }

    [[nodiscard]] const JoltBackendConfig& config() const noexcept
    {
        return config_;
    }

    // ---- Vehicle constraint side-band (phase 786) --------------------------

    /// Build a JPH::VehicleConstraint from a plain descriptor and register it.
    /// Returns the opaque slot index (>0) or 0 on failure.
    [[nodiscard]] std::uint32_t register_vehicle_constraint(
        cd::physics::BodyHandle chassis,
        const VehicleConstraintDesc& vd)
    {
        const JPH::BodyID chassis_id = handles_.find(chassis.index());
        if (chassis_id.IsInvalid())
            return 0U;

        // Use the no-lock interface to obtain the Body pointer; this is safe
        // here because we are on the main/single-threaded path (configure is
        // not called from within a physics step).
        JPH::Body* body = physics_.GetBodyLockInterfaceNoLock().TryGetBody(chassis_id);
        if (body == nullptr)
            return 0U;

        // ---- Build per-wheel settings (WheelSettingsWV) --------------------
        JPH::VehicleConstraintSettings vs;
        vs.mUp      = JPH::Vec3 { 0.0F, 1.0F, 0.0F };
        vs.mForward = JPH::Vec3 { 0.0F, 0.0F, 1.0F };

        for (int i = 0; i < 4; ++i)
        {
            const auto& wd = vd.wheels[i];
            auto* ws = new JPH::WheelSettingsWV;  // NOLINT(cppcoreguidelines-owning-memory)
            ws->mPosition          = JPH::Vec3 { wd.local[0], wd.local[1], wd.local[2] };
            ws->mRadius            = wd.radius;
            ws->mWidth             = wd.width;
            ws->mMaxSteerAngle     = wd.max_steer;
            // Suspension: translate spring-stiffness to frequency/damping ratio.
            // Jolt uses SpringSettings{ESpringMode::FrequencyAndDamping, freq, damp}.
            // Natural frequency: f = sqrt(k/m) / (2*pi). We approximate with
            // a sensible default for a ~1400 kg vehicle (1.5–2.5 Hz typical).
            ws->mSuspensionMinLength   = std::max(0.0F, wd.suspension_rest - 0.05F);
            ws->mSuspensionMaxLength   = wd.suspension_rest + 0.05F;
            ws->mSuspensionSpring.mFrequency = 1.5F;  // Hz — stable for car-scale
            ws->mSuspensionSpring.mDamping   = 0.5F;  // critically damped
            ws->mMaxBrakeTorque        = 1500.0F;
            ws->mMaxHandBrakeTorque    = wd.is_driven ? 4000.0F : 0.0F;
            vs.mWheels.push_back(ws);
        }

        // ---- Build WheeledVehicleController settings -----------------------
        auto* ctrl_settings = new JPH::WheeledVehicleControllerSettings;  // NOLINT(cppcoreguidelines-owning-memory)

        ctrl_settings->mEngine.mMaxTorque = vd.max_torque_nm;
        ctrl_settings->mEngine.mMinRPM    = vd.idle_rpm;
        ctrl_settings->mEngine.mMaxRPM    = vd.max_rpm;

        ctrl_settings->mTransmission.mMode = JPH::ETransmissionMode::Auto;
        ctrl_settings->mTransmission.mGearRatios.clear();
        for (float r : vd.gear_ratios)
        {
            ctrl_settings->mTransmission.mGearRatios.push_back(r * vd.final_drive);
        }

        // Identify driven wheels and build differentials.
        // Each driven axle gets its own open differential.
        // Rear axle = indices 2,3; front axle = indices 0,1.
        bool rear_driven  = vd.wheels[2].is_driven || vd.wheels[3].is_driven;
        bool front_driven = vd.wheels[0].is_driven || vd.wheels[1].is_driven;
        float torque_split = (rear_driven && front_driven) ? 0.5F : 1.0F;

        if (rear_driven)
        {
            JPH::VehicleDifferentialSettings diff {};
            diff.mLeftWheel         = 2;
            diff.mRightWheel        = 3;
            diff.mEngineTorqueRatio = torque_split;
            ctrl_settings->mDifferentials.push_back(diff);
        }
        if (front_driven)
        {
            JPH::VehicleDifferentialSettings diff {};
            diff.mLeftWheel         = 0;
            diff.mRightWheel        = 1;
            diff.mEngineTorqueRatio = torque_split;
            ctrl_settings->mDifferentials.push_back(diff);
        }
        if (!rear_driven && !front_driven)
        {
            // Fallback: rear-wheel drive.
            JPH::VehicleDifferentialSettings diff {};
            diff.mLeftWheel  = 2;
            diff.mRightWheel = 3;
            diff.mEngineTorqueRatio = 1.0F;
            ctrl_settings->mDifferentials.push_back(diff);
        }

        vs.mController = ctrl_settings;

        // ---- Create constraint + collision tester --------------------------
        // VehicleCollisionTesterRay on the kNonMoving layer so wheels detect
        // static terrain. Cast direction is world -Y (gravity down).
        JPH::Ref<JPH::VehicleCollisionTester> tester =
            new JPH::VehicleCollisionTesterRay(layers::kNonMoving);  // NOLINT(cppcoreguidelines-owning-memory)

        JPH::Ref<JPH::VehicleConstraint> constraint =
            new JPH::VehicleConstraint(*body, vs);  // NOLINT(cppcoreguidelines-owning-memory)
        constraint->SetVehicleCollisionTester(tester);

        physics_.AddConstraint(constraint);
        physics_.AddStepListener(constraint);

        // ---- Store in slot table ------------------------------------------
        const std::uint32_t slot = next_vehicle_slot_++;
        vehicle_constraints_.emplace(slot, VehicleSlot { constraint, tester });
        return slot;
    }

    void set_vehicle_driver_input(std::uint32_t slot,
                                  float forward, float right, float brake) noexcept
    {
        auto it = vehicle_constraints_.find(slot);
        if (it == vehicle_constraints_.end()) return;

        auto* ctrl = static_cast<JPH::WheeledVehicleController*>(
            it->second.constraint->GetController());
        if (ctrl == nullptr) return;

        ctrl->SetDriverInput(forward, right, brake, 0.0F /* handbrake */);
    }

    [[nodiscard]] bool get_vehicle_wheel_contact(std::uint32_t slot, int wheel_idx) const noexcept
    {
        auto it = vehicle_constraints_.find(slot);
        if (it == vehicle_constraints_.end()) return false;
        const auto& wheels = it->second.constraint->GetWheels();
        if (wheel_idx < 0 || static_cast<std::size_t>(wheel_idx) >= wheels.size())
            return false;
        return wheels[static_cast<std::size_t>(wheel_idx)]->HasContact();
    }

    void remove_vehicle_constraint(std::uint32_t slot) noexcept
    {
        auto it = vehicle_constraints_.find(slot);
        if (it == vehicle_constraints_.end()) return;

        physics_.RemoveStepListener(it->second.constraint);
        physics_.RemoveConstraint(it->second.constraint);
        vehicle_constraints_.erase(it);
    }

private:
    // ---- Vehicle constraint slot entry -------------------------------------
    struct VehicleSlot
    {
        JPH::Ref<JPH::VehicleConstraint>         constraint;
        JPH::Ref<JPH::VehicleCollisionTester>    tester;
    };

    // global_init_ MUST be first member: ctor increments the Jolt global
    // refcount (RegisterDefaultAllocator + Factory + RegisterTypes) before any
    // other JPH objects are constructed; dtor decrements after all others die.
    JoltGlobalInit global_init_ {};

    JoltBackendConfig config_ {};

    // Broadphase / layer infrastructure
    BPLayerInterface  bp_layer_iface_ {};
    ObjVsBPFilter     obj_vs_bp_filter_ {};
    ObjPairFilter     obj_pair_filter_ {};

    // Jolt core objects
    JPH::TempAllocatorImpl       temp_alloc_;
    JPH::JobSystemSingleThreaded job_system_;
    JPH::PhysicsSystem           physics_;

    // Handle mapping + component side-band
    HandleTable  handles_ {};
    std::uint64_t next_idx_ { 0U };
    std::unordered_map<std::uint32_t, components::RigidBodyComponent> rigid_components_ {};
    std::unordered_map<std::uint32_t, std::vector<components::ColliderComponent>> colliders_ {};
    std::vector<components::JointComponent> joints_ {};

    // Vehicle constraint table
    std::unordered_map<std::uint32_t, VehicleSlot> vehicle_constraints_ {};
    std::uint32_t next_vehicle_slot_ { 1U };  // 0 reserved for kInvalid
};

}  // namespace

// ---------------------------------------------------------------------------
// Public free functions — real backend
// ---------------------------------------------------------------------------

components::RigidBodyComponent body_desc_to_component(
    const cd::physics::BodyDesc& desc, const JoltBackendConfig& cfg) noexcept
{
    components::RigidBodyComponent rb {};
    rb.type = desc.type;
    rb.mass = desc.mass;
    rb.linear_damping  = desc.linear_damping > 0.0F ? desc.linear_damping : cfg.default_linear_damping;
    rb.angular_damping = cfg.default_angular_damping;
    return rb;
}

namespace
{
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
}  // namespace

std::uint64_t hash_shape_desc(const components::ShapeDesc& shape) noexcept
{
    constexpr std::uint64_t kFnvOffsetBasis = 0xcbf29ce484222325ULL;
    constexpr std::uint64_t kBoxTag    = 1;
    constexpr std::uint64_t kSphereTag = 2;
    constexpr std::uint64_t kCapsuleTag = 3;
    constexpr std::uint64_t kMeshTag   = 4;
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
                const auto v_addr = reinterpret_cast<std::uintptr_t>(s.vertices);
                const auto i_addr = reinterpret_cast<std::uintptr_t>(s.indices);
                h = fnv1a64_bytes(&v_addr, sizeof(v_addr), h);
                return fnv1a64_bytes(&i_addr, sizeof(i_addr), h);
            }
            else
            {
                static_assert(!std::is_same_v<T, T>, "unhandled ShapeDesc variant");
                return kFnvOffsetBasis;
            }
        },
        shape);
}

std::unique_ptr<cd::physics::IPhysicsWorld> make_jolt_physics_world(const JoltBackendConfig& cfg)
{
    return std::make_unique<JoltWorldReal>(cfg);
}

bool is_stub_backend() noexcept
{
    return false;
}

namespace
{
[[nodiscard]] JoltWorldReal* as_jolt(cd::physics::IPhysicsWorld& w) noexcept
{
    return dynamic_cast<JoltWorldReal*>(&w);
}
[[nodiscard]] const JoltWorldReal* as_jolt(const cd::physics::IPhysicsWorld& w) noexcept
{
    return dynamic_cast<const JoltWorldReal*>(&w);
}
}  // namespace

bool attach_collider(
    cd::physics::IPhysicsWorld& world,
    cd::physics::BodyHandle owner,
    const components::ColliderComponent& collider)
{
    auto* impl = as_jolt(world);
    if (impl == nullptr) return false;
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
    if (impl == nullptr) return false;
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
    if (impl == nullptr) return JoltBackendConfig {};
    return impl->config();
}

// ---- Vehicle constraint free functions (real backend) ----------------------

VehicleConstraintHandle create_vehicle_constraint(
    cd::physics::IPhysicsWorld& world,
    cd::physics::BodyHandle chassis,
    const VehicleConstraintDesc& desc)
{
    auto* impl = as_jolt(world);
    if (impl == nullptr)
        return VehicleConstraintHandle::kInvalid;
    const std::uint32_t slot = impl->register_vehicle_constraint(chassis, desc);
    return VehicleConstraintHandle { slot };
}

void set_vehicle_driver_input(
    cd::physics::IPhysicsWorld& world,
    VehicleConstraintHandle handle,
    float forward,
    float right,
    float brake) noexcept
{
    if (!handle.is_valid()) return;
    auto* impl = as_jolt(world);
    if (impl == nullptr) return;
    impl->set_vehicle_driver_input(handle.index, forward, right, brake);
}

bool get_vehicle_wheel_contact(
    const cd::physics::IPhysicsWorld& world,
    VehicleConstraintHandle handle,
    int wheel_index) noexcept
{
    if (!handle.is_valid()) return false;
    const auto* impl = as_jolt(world);
    if (impl == nullptr) return false;
    return impl->get_vehicle_wheel_contact(handle.index, wheel_index);
}

void destroy_vehicle_constraint(
    cd::physics::IPhysicsWorld& world,
    VehicleConstraintHandle handle) noexcept
{
    if (!handle.is_valid()) return;
    auto* impl = as_jolt(world);
    if (impl == nullptr) return;
    impl->remove_vehicle_constraint(handle.index);
}

}  // namespace cd::physics_jolt

// =============================================================================
// STUB BACKEND — semi-implicit Euler delegate
// =============================================================================
#else  // !CD_PHYSICS_JOLT_REAL  (i.e. CD_PHYSICS_JOLT_STUB=1 or neither)

namespace cd::physics_jolt
{

namespace
{

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

// ---------------------------------------------------------------------------
// JoltWorldStub — delegates all dynamics to the built-in Euler reference.
// ---------------------------------------------------------------------------
class JoltWorldStub final : public cd::physics::IPhysicsWorld
{
public:
    explicit JoltWorldStub(JoltBackendConfig cfg) noexcept
        : config_ { cfg }
        , inner_ { cd::physics::make_builtin_physics_world() }
    {
    }

    void set_gravity(cd::math::Vec3f g) noexcept override { inner_->set_gravity(g); }

    [[nodiscard]] cd::math::Vec3f gravity() const noexcept override
    {
        return inner_->gravity();
    }

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

    void step(float dt) override { inner_->step(dt); }

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

    void register_joint(const components::JointComponent& j) { joints_.push_back(j); }

    [[nodiscard]] std::size_t joint_count() const noexcept { return joints_.size(); }

    [[nodiscard]] const JoltBackendConfig& config() const noexcept { return config_; }

private:
    JoltBackendConfig config_ {};
    std::unique_ptr<cd::physics::IPhysicsWorld> inner_ {};
    std::unordered_map<std::uint32_t, components::RigidBodyComponent> rigid_components_ {};
    std::unordered_map<std::uint32_t, std::vector<components::ColliderComponent>> colliders_ {};
    std::vector<components::JointComponent> joints_ {};
};

}  // namespace

// ---------------------------------------------------------------------------
// Public free functions — stub backend
// ---------------------------------------------------------------------------

components::RigidBodyComponent body_desc_to_component(
    const cd::physics::BodyDesc& desc, const JoltBackendConfig& cfg) noexcept
{
    components::RigidBodyComponent rb {};
    rb.type = desc.type;
    rb.mass = desc.mass;
    rb.linear_damping  = desc.linear_damping > 0.0F ? desc.linear_damping : cfg.default_linear_damping;
    rb.angular_damping = cfg.default_angular_damping;
    return rb;
}

std::uint64_t hash_shape_desc(const components::ShapeDesc& shape) noexcept
{
    constexpr std::uint64_t kFnvOffsetBasis = 0xcbf29ce484222325ULL;
    constexpr std::uint64_t kBoxTag    = 1;
    constexpr std::uint64_t kSphereTag = 2;
    constexpr std::uint64_t kCapsuleTag = 3;
    constexpr std::uint64_t kMeshTag   = 4;
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
                const auto v_addr = reinterpret_cast<std::uintptr_t>(s.vertices);
                const auto i_addr = reinterpret_cast<std::uintptr_t>(s.indices);
                h = fnv1a64_bytes(&v_addr, sizeof(v_addr), h);
                return fnv1a64_bytes(&i_addr, sizeof(i_addr), h);
            }
            else
            {
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
    if (impl == nullptr) return false;
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
    if (impl == nullptr) return false;
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
    if (impl == nullptr) return JoltBackendConfig {};
    return impl->config();
}

// ---- Vehicle constraint free functions (stub backend — no-op) --------------

VehicleConstraintHandle create_vehicle_constraint(
    cd::physics::IPhysicsWorld& /*world*/,
    cd::physics::BodyHandle     /*chassis*/,
    const VehicleConstraintDesc& /*desc*/)
{
    return VehicleConstraintHandle::kInvalid;
}

void set_vehicle_driver_input(
    cd::physics::IPhysicsWorld& /*world*/,
    VehicleConstraintHandle     /*handle*/,
    float /*forward*/,
    float /*right*/,
    float /*brake*/) noexcept
{
    // Stub: no-op. Driver inputs are applied by JoltAdapter's impulse path.
}

bool get_vehicle_wheel_contact(
    const cd::physics::IPhysicsWorld& /*world*/,
    VehicleConstraintHandle           /*handle*/,
    int                               /*wheel_index*/) noexcept
{
    return false;  // Stub: no terrain query; JoltAdapter sets grounded=true.
}

void destroy_vehicle_constraint(
    cd::physics::IPhysicsWorld& /*world*/,
    VehicleConstraintHandle     /*handle*/) noexcept
{
    // Stub: no-op.
}

}  // namespace cd::physics_jolt

#endif  // CD_PHYSICS_JOLT_REAL
