// =============================================================================
// CHROMODYNAMIC — cd/physics_jolt/JoltWorld.hpp
// Phase 525 / T0.3 — Jolt physics backend (STUB scope).
//
// Public surface of the `cd::physics_jolt` library. Provides the
// `make_jolt_physics_world()` factory and the strongly-typed config /
// component descriptor structs that any consumer (scene authoring,
// gameplay code, samples, tests) needs.
//
// Vendor boundary policy (ADR-20260530-jolt-physics-integration §B):
//   * NO `Jolt/Jolt.h` may be included from this header — `JPH::*`
//     symbols are PIMPL-hidden inside the .cpp. Downstream consumers
//     never see Jolt types.
//   * The factory returns an owning `std::unique_ptr<cd::physics::IPhysicsWorld>`
//     so callers depend ONLY on the public physics interface
//     (`engine/world/physics/include/cd/physics/IPhysicsWorld.hpp`).
//
// Scope (Run 525):
//   This translation unit ships a STUB backend. The vcpkg `jolt-physics`
//   port and the `Dependencies/jolt/` submodule wiring will land in a
//   follow-up ADR (`ADR-2026xxxx-physics-jolt-bringup`) per the design
//   doc; the current implementation delegates internally to the
//   existing built-in semi-implicit Euler world so all IPhysicsWorld
//   semantics (gravity, impulse, force, kinematic / static gating)
//   continue to be exercised end-to-end. When real Jolt lands, only
//   the .cpp implementation changes — the public surface in this
//   header is final.
//
// The component descriptor structs (RigidBodyComponent, ColliderComponent,
// JointComponent) intentionally live in `cd::physics_jolt::components`
// for the stub phase. They will graduate to `cd::scene::*` (ADR-004
// follow-up) once the scene ECS RTT lands, at which point this header
// re-exports them via using-declarations to keep callers unchanged.
//
// SOTA references:
//   * Rouwe, "Architecting Jolt Physics for Horizon Forbidden West"
//     (Guerrilla / GDC 2022) — vendor adapter shape.
//   * ADR-008 (Physics Architecture) — adapter pattern + Jolt default.
//   * ADR-015 (Concurrency / Job System) — `JoltJobAdapter` rationale.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <cd/math/Vector.hpp>
#include <cd/physics/IPhysicsWorld.hpp>

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <variant>

namespace cd::physics_jolt
{

// ---- Backend configuration --------------------------------------------------

/// Static knobs that pin Jolt's internal limits. Defaults match the
/// `Jolt/Physics/PhysicsSettings.h` recommendations from the official
/// HelloWorld sample. Sized for a mid-AAA scene (~10k bodies) to avoid
/// re-allocation under load.
struct JoltBackendConfig
{
    /// Hard cap on simultaneous rigid bodies (Jolt allocates a pool up-front).
    std::uint32_t max_bodies { 65536U };
    /// Number of mutex shards guarding the broadphase / narrowphase pair cache.
    std::uint32_t num_body_mutexes { 0U };
    /// Maximum simultaneous body-pair contacts the narrowphase may track.
    std::uint32_t max_body_pairs { 65536U };
    /// Contact-manifold cap (a single body-pair may have many points).
    std::uint32_t max_contact_constraints { 10240U };
    /// Worker count requested from the bridged job system. 0 = follow pool.
    std::uint32_t max_concurrent_jobs { 0U };
    /// Number of `JPH::JobSystem` barriers (Jolt-recommended 8).
    std::uint32_t max_barriers { 8U };
    /// Default linear damping applied to fresh dynamic bodies.
    float default_linear_damping { 0.05F };
    /// Default angular damping applied to fresh dynamic bodies.
    float default_angular_damping { 0.05F };
};

// ---- Component descriptors --------------------------------------------------
//
// These mirror the planned `cd::scene::*` components from ADR-20260530 §C.
// They live here during the stub phase so the Jolt-side mappers have a
// strongly typed target without dragging the scene library into the build
// graph. Each one is a plain descriptor — no Jolt symbols leak into it.

namespace components
{

/// One-to-one with the planned `cd::scene::RigidBodyComponent`. Holds the
/// authoring intent; the backend owns the actual body handle.
struct RigidBodyComponent
{
    cd::physics::BodyType type { cd::physics::BodyType::kDynamic };
    /// Kilograms. Ignored for non-dynamic bodies; must be > 0 for `kDynamic`.
    float mass { 1.0F };
    /// Velocity damping per second (clamped to [0, 1] internally).
    float linear_damping { 0.05F };
    /// Angular velocity damping per second.
    float angular_damping { 0.05F };
    /// Coefficient of restitution (0 = perfectly inelastic, 1 = perfectly
    /// elastic). Routed to `JPH::Body::SetRestitution`.
    float restitution { 0.0F };
    /// Coulomb friction coefficient (0..1 typical, can exceed 1).
    float friction { 0.5F };
    /// Continuous collision detection mode flag. `true` -> Jolt
    /// `EMotionQuality::LinearCast`; `false` -> `EMotionQuality::Discrete`.
    bool enable_ccd { false };
    /// Broadphase / object layer (gameplay-side category bits).
    std::uint16_t layer { 0U };
};

/// Plain-old descriptor for each supported Jolt shape variant. The
/// backend will hash + intern the descriptor into a cached `JPH::Shape*`
/// (Jolt's intent: shapes are immutable & ref-counted).
struct BoxShape
{
    cd::math::Vec3f half_extents { 0.5F, 0.5F, 0.5F };
};

struct SphereShape
{
    float radius { 0.5F };
};

struct CapsuleShape
{
    /// Distance between the two hemisphere centers, along the local Y axis.
    float half_height { 0.5F };
    float radius { 0.25F };
};

struct MeshShape
{
    /// Pointer + count to caller-owned vertex buffer (xyz interleaved).
    const float* vertices { nullptr };
    std::size_t vertex_count { 0U };
    /// Pointer + count to caller-owned index buffer (triangle list).
    const std::uint32_t* indices { nullptr };
    std::size_t index_count { 0U };
};

using ShapeDesc = std::variant<BoxShape, SphereShape, CapsuleShape, MeshShape>;

/// Mirrors the planned `cd::scene::ColliderComponent`.
struct ColliderComponent
{
    ShapeDesc shape { BoxShape {} };
    cd::math::Vec3f local_offset { 0.0F, 0.0F, 0.0F };
    /// Hint surfaced to gameplay; sensor bodies emit overlap events instead
    /// of generating contact constraints.
    bool is_trigger { false };
    /// Stable user-supplied material id (looked up in a side table).
    std::uint32_t material_id { 0U };
};

enum class JointKind : std::uint8_t
{
    kFixed,
    kHinge,
    kSlider,
    kDistance,
    kBallSocket,
    kSixDof,
};

/// Mirrors the planned `cd::scene::JointComponent`.
struct JointComponent
{
    JointKind kind { JointKind::kFixed };
    cd::physics::BodyHandle body_a {};
    cd::physics::BodyHandle body_b {};
    cd::math::Vec3f anchor_a { 0.0F, 0.0F, 0.0F };
    cd::math::Vec3f anchor_b { 0.0F, 0.0F, 0.0F };
    /// Optional axis (hinge / slider only). Unit vector expected.
    cd::math::Vec3f axis { 0.0F, 1.0F, 0.0F };
    /// Min/max constraint limit along the active axis. For hinge: radians.
    float limit_min { 0.0F };
    float limit_max { 0.0F };
};

}  // namespace components

// ---- Job adapter ------------------------------------------------------------
//
// Jolt expects a `JPH::JobSystem` implementation. ADR-015 forbids a second
// thread pool, so we route every Jolt job through the engine's own pool.
// During the stub phase we accept a small, header-only "submit callable"
// shape (`std::function<void(JobFn)>`) so callers can pass any pool —
// `cd::concurrency::WorkStealingThreadPool::spawn_detached`, an inline
// executor, or a test mock — without dragging the concurrency library
// into this header. When real Jolt lands the adapter still owns the
// `JPH::JobSystem` derivation; only the storage of the submit hook
// changes (it becomes a non-owning reference to `IJobSystem`).
class JoltJobAdapter
{
public:
    using JobFn = std::function<void()>;
    using SubmitFn = std::function<void(JobFn)>;

    JoltJobAdapter() noexcept = default;

    /// Construct with an explicit submit hook. Pass any callable whose
    /// invocation eventually runs the job on a worker thread; the adapter
    /// counts dispatches so test code can assert routing without spinning
    /// real threads.
    explicit JoltJobAdapter(SubmitFn submit) noexcept
        : submit_ { std::move(submit) }
    {
    }

    /// Push a job through the adapter. When no submit hook is installed the
    /// job runs inline on the calling thread (legal per `JPH::JobSystem`'s
    /// contract — Jolt sometimes runs jobs inline when the pool refuses).
    void dispatch(JobFn job)
    {
        ++dispatched_;
        if (submit_)
        {
            submit_(std::move(job));
        }
        else
        {
            // Inline fallback so the adapter is usable without a pool.
            if (job)
                job();
        }
    }

    [[nodiscard]] std::uint64_t dispatched() const noexcept
    {
        return dispatched_;
    }

    [[nodiscard]] bool has_submit_hook() const noexcept
    {
        return static_cast<bool>(submit_);
    }

private:
    SubmitFn submit_ {};
    std::uint64_t dispatched_ { 0U };
};

// ---- Component-to-backend mapping helpers ----------------------------------
//
// Pure functions so tests can exercise the descriptor -> Jolt translation
// independently of an actual world. The real (non-stub) implementation
// returns `JPH::*` settings structs from these mappers; the stub returns
// the lossless POD form we can validate against.

/// Translate a generic `BodyDesc` from `cd::physics` into a fully-populated
/// rigid-body descriptor using the backend's defaults. Centralises the
/// "what does kStatic mean for damping" decisions in one spot.
[[nodiscard]] components::RigidBodyComponent body_desc_to_component(
    const cd::physics::BodyDesc& desc, const JoltBackendConfig& cfg) noexcept;

/// Pull a stable hash out of a shape descriptor. Real Jolt path uses this
/// as the key into the shape cache; the stub uses it to prove shape
/// equality round-trips.
[[nodiscard]] std::uint64_t hash_shape_desc(const components::ShapeDesc& shape) noexcept;

// ---- Factory ----------------------------------------------------------------

/// Build a Jolt-backed `IPhysicsWorld`. Returns a fully-owning unique_ptr
/// so the caller never sees a raw Jolt symbol. When the stub backend is
/// compiled (`CD_PHYSICS_JOLT_STUB=1`, the default until the vcpkg port
/// lands), the returned world delegates to the built-in semi-implicit
/// Euler reference so the public IPhysicsWorld contract still holds; only
/// the source of jobs (via `JoltJobAdapter`) and the component mapper
/// surface differ from the real backend.
[[nodiscard]] std::unique_ptr<cd::physics::IPhysicsWorld> make_jolt_physics_world(
    const JoltBackendConfig& cfg = {});

/// `true` when the stub backend is in effect. Flips to `false` once the
/// real Jolt source is wired in. Compiled-out branches in samples / tests
/// pivot on this constant so feature gates land cleanly.
[[nodiscard]] bool is_stub_backend() noexcept;

// ---- Side-band component attachment (Jolt-side surface) --------------------
//
// During the stub phase these helpers route through the concrete world
// returned by `make_jolt_physics_world`. They do nothing (and return 0)
// when handed a foreign `IPhysicsWorld` (e.g. the built-in Euler world)
// so caller code stays portable. When the real backend lands these
// forward to `JPH::PhysicsSystem::GetBodyInterface()` / `ConstraintManager`.
//
// Returns `true` when the world accepted the registration.
bool attach_collider(
    cd::physics::IPhysicsWorld& world,
    cd::physics::BodyHandle owner,
    const components::ColliderComponent& collider);

[[nodiscard]] std::size_t collider_count(
    const cd::physics::IPhysicsWorld& world, cd::physics::BodyHandle owner) noexcept;

bool attach_joint(
    cd::physics::IPhysicsWorld& world, const components::JointComponent& joint);

[[nodiscard]] std::size_t joint_count(const cd::physics::IPhysicsWorld& world) noexcept;

/// Read back the resolved config (defaults merged with caller overrides).
/// Returns the engine's default config when the world is not Jolt-backed.
[[nodiscard]] JoltBackendConfig backend_config(
    const cd::physics::IPhysicsWorld& world) noexcept;

// ---- Vehicle constraint side-band (phase 786) ------------------------------
//
// These functions expose a minimal surface for wiring a JPH::VehicleConstraint
// + JPH::WheeledVehicleController through the IPhysicsWorld abstraction.
// All JPH::* symbols are hidden inside JoltWorld.cpp; callers only see the
// opaque VehicleConstraintHandle index and these free functions.
//
// Lifecycle:
//   1. create_body() the chassis first (via IPhysicsWorld), then call
//      create_vehicle_constraint() with the resulting BodyHandle and the
//      plain-data VehicleConstraintDesc to build the JPH constraint.
//   2. Each tick: call set_vehicle_driver_input() BEFORE world.step(dt).
//   3. After world.step(dt): call get_vehicle_wheel_contact() per wheel.
//   4. Call destroy_vehicle_constraint() when done (removes the constraint
//      and step-listener from the PhysicsSystem).
//
// Returns VehicleConstraintHandle::kInvalid on failure (stub backend or
// body not found).

/// Plain-data descriptor for creating a WheeledVehicleController.
/// Mirrors cd::physics::vehicle::VehicleConfig but uses only primitive types
/// so JoltWorld.hpp stays free of the vehicle library header.
struct VehicleConstraintDesc
{
    /// Total chassis + wheel mass (kg).
    float total_mass_kg { 1400.0F };
    /// Chassis half-extents [half_length, half_width, half_height] (m).
    float chassis_half[3] { 1.2F, 0.475F, 0.35F };
    /// Peak engine torque (N·m).
    float max_torque_nm { 300.0F };
    /// Engine idle RPM.
    float idle_rpm { 800.0F };
    /// Engine redline RPM.
    float max_rpm { 6500.0F };
    /// 6 forward gear ratios (index 0 = 1st gear).
    float gear_ratios[6] { 3.5F, 2.1F, 1.4F, 1.0F, 0.8F, 0.67F };
    /// Final-drive ratio.
    float final_drive { 3.7F };
    /// Per-wheel parameters (4 entries: FL=0, FR=1, RL=2, RR=3).
    struct WheelDesc
    {
        /// Attachment point in chassis-local space (m).
        float local[3]       { 0.0F, 0.0F, 0.0F };
        /// Wheel rolling radius (m).
        float radius         { 0.33F };
        /// Wheel width (m).
        float width          { 0.12F };
        /// Maximum steering angle (radians); 0 = non-steering.
        float max_steer      { 0.0F };
        /// Suspension rest length (m).
        float suspension_rest { 0.25F };
        /// Suspension stiffness (N/m) — mapped to frequency in Jolt spring.
        float suspension_stiffness { 22000.0F };
        /// Suspension damping (N·s/m).
        float suspension_damping   { 4000.0F };
        /// True if this wheel receives drive torque.
        bool is_driven       { false };
    } wheels[4];
};

/// Opaque handle to a registered JPH::VehicleConstraint.
/// kInvalid == 0; valid handles are > 0.
struct VehicleConstraintHandle
{
    std::uint32_t index { 0U };
    [[nodiscard]] bool is_valid() const noexcept { return index != 0U; }
    static const VehicleConstraintHandle kInvalid;
};

inline const VehicleConstraintHandle VehicleConstraintHandle::kInvalid { 0U };

/// Create a JPH::VehicleConstraint for chassis body `chassis` in `world`.
/// Registers the constraint as a PhysicsStepListener automatically.
/// Returns VehicleConstraintHandle::kInvalid when the world is not real-Jolt
/// or the chassis body is not found.
[[nodiscard]] VehicleConstraintHandle create_vehicle_constraint(
    cd::physics::IPhysicsWorld& world,
    cd::physics::BodyHandle chassis,
    const VehicleConstraintDesc& desc);

/// Push driver inputs to the WheeledVehicleController for the next step().
/// forward ∈ [0,1]: gas pedal (positive = forwards, negative = reverse in
///   automatic mode Jolt supports passing negative values).
/// right   ∈ [-1,1]: steer right.
/// brake   ∈ [0,1]: brake pedal.
void set_vehicle_driver_input(
    cd::physics::IPhysicsWorld& world,
    VehicleConstraintHandle handle,
    float forward,
    float right,
    float brake) noexcept;

/// Query whether wheel `wheel_index` (0–3) is in ground contact this frame.
/// Returns false when the handle is invalid or the backend is a stub.
[[nodiscard]] bool get_vehicle_wheel_contact(
    const cd::physics::IPhysicsWorld& world,
    VehicleConstraintHandle handle,
    int wheel_index) noexcept;

/// Remove the VehicleConstraint + step-listener from the world and free it.
/// Safe to call with an invalid handle (no-op).
void destroy_vehicle_constraint(
    cd::physics::IPhysicsWorld& world,
    VehicleConstraintHandle handle) noexcept;

}  // namespace cd::physics_jolt
