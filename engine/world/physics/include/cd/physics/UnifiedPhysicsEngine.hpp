// =============================================================================
// CHROMODYNAMIC — cd/physics/UnifiedPhysicsEngine.hpp
// Module #32 of 32: Top-level Unified 2D + 3D Rockstar-Grade Physics Coordinator.
//
// Aggregates the 31 granular single-responsibility physics modules across:
//   * cd::physics::core     (7 modules: types, 32x32 filter matrix, 2D/3D AABB,
//                            2D/3D Dynamic SAH BVH, Union-Find IslandBuilder)
//   * cd::physics::v2d      (9 modules: 2D shapes, manifold, analytical+SAT
//                            narrowphase, TOI sweep CCD, RigidBody2D, Box2D v3
//                            Soft-Step TGS solver, 5 joint types, 2D Character,
//                            PhysicsWorld2D)
//   * cd::physics::v3d      (10 modules: 3D shapes, 4-point manifold, 3D GJK+EPA,
//                            15-axis OBB SAT narrowphase, 3D CCD TOI, 6-DOF
//                            RigidBody3D with gyroscopic torque, TGS friction-cone
//                            solver, 6 joint types, 3D CharacterMover, PhysicsWorld3D)
//   * cd::physics::rockstar (5 modules: Euphoria Active Ragdoll PD+Capture-Point,
//                            Pacejka Magic-Formula Vehicle + 24-cell Deformation,
//                            Voronoi Stress-Graph Fracture & Debris, Archimedes +
//                            Gerstner Wave Buoyancy, XPBD Cloth & Pressurized SoftBody)
// =============================================================================
#pragma once

#include <cd/physics/IPhysicsWorld.hpp>

// ---- 1. Core Spatial & Broadphase Modules (7) -------------------------------
#include <cd/physics/core/CollisionFilterMatrix.hpp>
#include <cd/physics/core/DynamicBvh2D.hpp>
#include <cd/physics/core/DynamicBvh3D.hpp>
#include <cd/physics/core/IslandBuilder.hpp>
#include <cd/physics/core/PhysicsTypes.hpp>
#include <cd/physics/core/SpatialAabb2D.hpp>
#include <cd/physics/core/SpatialAabb3D.hpp>

// ---- 2. 2D Physics Engine Modules (9) ---------------------------------------
#include <cd/physics/v2d/CharacterController2D.hpp>
#include <cd/physics/v2d/ContactManifold2D.hpp>
#include <cd/physics/v2d/Joints2D.hpp>
#include <cd/physics/v2d/Narrowphase2D.hpp>
#include <cd/physics/v2d/PhysicsWorld2D.hpp>
#include <cd/physics/v2d/RigidBody2D.hpp>
#include <cd/physics/v2d/Shapes2D.hpp>
#include <cd/physics/v2d/SoftStepSolver2D.hpp>
#include <cd/physics/v2d/SweepCcd2D.hpp>

// ---- 3. 3D Physics Engine Modules (10) --------------------------------------
#include <cd/physics/v3d/CharacterMover3D.hpp>
#include <cd/physics/v3d/ContactManifold3D.hpp>
#include <cd/physics/v3d/GjkEpa3D.hpp>
#include <cd/physics/v3d/Joints3D.hpp>
#include <cd/physics/v3d/Narrowphase3D.hpp>
#include <cd/physics/v3d/PhysicsWorld3D.hpp>
#include <cd/physics/v3d/RigidBody3D.hpp>
#include <cd/physics/v3d/Shapes3D.hpp>
#include <cd/physics/v3d/SweepCcd3D.hpp>
#include <cd/physics/v3d/TgsContactSolver3D.hpp>

// ---- 4. Rockstar RAGE / Euphoria & Advanced Simulation Modules (5) ----------
#include <cd/physics/rockstar/ActiveRagdollEuphoria3D.hpp>
#include <cd/physics/rockstar/BuoyancyFluid3D.hpp>
#include <cd/physics/rockstar/FractureDestruction3D.hpp>
#include <cd/physics/rockstar/PacejkaVehicleDynamics3D.hpp>
#include <cd/physics/rockstar/XpbdClothSoftBody3D.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace cd::physics
{

struct UnifiedPhysicsTelemetry
{
    std::uint32_t bodies_2d          { 0u };
    std::uint32_t contacts_2d        { 0u };
    std::uint32_t islands_2d         { 0u };
    std::uint32_t bodies_3d          { 0u };
    std::uint32_t contacts_3d        { 0u };
    std::uint32_t islands_3d         { 0u };
    std::uint32_t active_ragdolls    { 0u };
    std::uint32_t active_vehicles    { 0u };
    std::uint32_t fracture_systems   { 0u };
    std::uint32_t xpbd_soft_bodies   { 0u };
    std::uint64_t total_steps        { 0u };
};

class UnifiedPhysicsEngine
{
public:
    static constexpr std::uint32_t kModularHeaderCount = 32u;

    UnifiedPhysicsEngine() = default;

    // ---- Sub-World Accessors ------------------------------------------------

    [[nodiscard]] v2d::PhysicsWorld2D& world_2d() noexcept { return world_2d_; }
    [[nodiscard]] const v2d::PhysicsWorld2D& world_2d() const noexcept { return world_2d_; }

    [[nodiscard]] v3d::PhysicsWorld3D& world_3d() noexcept { return world_3d_; }
    [[nodiscard]] const v3d::PhysicsWorld3D& world_3d() const noexcept { return world_3d_; }

    [[nodiscard]] rockstar::BuoyancyFluidField3D& fluid_field_3d() noexcept { return fluid_field_3d_; }
    [[nodiscard]] const rockstar::BuoyancyFluidField3D& fluid_field_3d() const noexcept { return fluid_field_3d_; }

    void clear()
    {
        world_2d_            = v2d::PhysicsWorld2D {};
        world_3d_            = v3d::PhysicsWorld3D {};
        fluid_field_3d_      = rockstar::BuoyancyFluidField3D {};
        ragdolls_3d_.clear();
        vehicles_3d_.clear();
        fracture_systems_3d_.clear();
        xpbd_solvers_3d_.clear();
        total_steps_         = 0u;
    }

    // ---- Rockstar Subsystem Registration ------------------------------------

    [[nodiscard]] std::uint32_t create_euphoria_ragdoll(
        cd::math::Vec3f root_pos = { 0.0F, 1.0F, 0.0F },
        float total_mass_kg = 78.0F)
    {
        rockstar::ActiveRagdollEuphoria3D ragdoll {};
        ragdoll.configure_humanoid_rig(root_pos, total_mass_kg);
        ragdolls_3d_.push_back(ragdoll);
        return static_cast<std::uint32_t>(ragdolls_3d_.size() - 1u);
    }

    [[nodiscard]] rockstar::ActiveRagdollEuphoria3D* get_euphoria_ragdoll(std::uint32_t idx) noexcept
    {
        return idx < ragdolls_3d_.size() ? &ragdolls_3d_[idx] : nullptr;
    }

    [[nodiscard]] const rockstar::ActiveRagdollEuphoria3D* get_euphoria_ragdoll(std::uint32_t idx) const noexcept
    {
        return idx < ragdolls_3d_.size() ? &ragdolls_3d_[idx] : nullptr;
    }

    [[nodiscard]] std::uint32_t create_pacejka_vehicle(
        const rockstar::VehicleConfig3D& cfg = {})
    {
        vehicles_3d_.emplace_back(cfg);
        return static_cast<std::uint32_t>(vehicles_3d_.size() - 1u);
    }

    [[nodiscard]] rockstar::PacejkaVehicleDynamics3D* get_pacejka_vehicle(std::uint32_t idx) noexcept
    {
        return idx < vehicles_3d_.size() ? &vehicles_3d_[idx] : nullptr;
    }

    [[nodiscard]] const rockstar::PacejkaVehicleDynamics3D* get_pacejka_vehicle(std::uint32_t idx) const noexcept
    {
        return idx < vehicles_3d_.size() ? &vehicles_3d_[idx] : nullptr;
    }

    [[nodiscard]] std::uint32_t create_fracture_system()
    {
        fracture_systems_3d_.emplace_back();
        return static_cast<std::uint32_t>(fracture_systems_3d_.size() - 1u);
    }

    [[nodiscard]] rockstar::FractureDestructionSystem3D* get_fracture_system(std::uint32_t idx) noexcept
    {
        return idx < fracture_systems_3d_.size() ? &fracture_systems_3d_[idx] : nullptr;
    }

    [[nodiscard]] std::uint32_t create_xpbd_solver()
    {
        xpbd_solvers_3d_.emplace_back();
        return static_cast<std::uint32_t>(xpbd_solvers_3d_.size() - 1u);
    }

    [[nodiscard]] rockstar::XpbdClothSoftBodySolver3D* get_xpbd_solver(std::uint32_t idx) noexcept
    {
        return idx < xpbd_solvers_3d_.size() ? &xpbd_solvers_3d_[idx] : nullptr;
    }

    // ---- Unified Deterministic Step -----------------------------------------

    void step_all(
        float dt,
        std::uint32_t substeps_2d = 4u,
        std::uint32_t substeps_3d = 4u,
        float floor_y = 0.0F)
    {
        if (dt <= 0.0F)
        {
            return;
        }

        world_2d_.step(dt, substeps_2d);
        world_3d_.step(dt, substeps_3d);

        for (auto& ragdoll : ragdolls_3d_)
        {
            ragdoll.step(dt, floor_y);
        }

        for (auto& vehicle : vehicles_3d_)
        {
            vehicle.step(rockstar::VehicleInputState {}, dt);
        }

        for (auto& frac : fracture_systems_3d_)
        {
            frac.step_debris(dt, floor_y);
        }

        for (auto& xpbd : xpbd_solvers_3d_)
        {
            xpbd.step(dt, 4u, 4u);
        }

        ++total_steps_;
    }

    [[nodiscard]] UnifiedPhysicsTelemetry telemetry() const noexcept
    {
        UnifiedPhysicsTelemetry t {};
        t.bodies_2d        = static_cast<std::uint32_t>(world_2d_.body_count());
        t.contacts_2d      = static_cast<std::uint32_t>(world_2d_.active_manifolds().size());
        t.islands_2d       = static_cast<std::uint32_t>(world_2d_.island_count());
        t.bodies_3d        = static_cast<std::uint32_t>(world_3d_.body_count());
        t.contacts_3d      = static_cast<std::uint32_t>(world_3d_.active_manifolds().size());
        t.islands_3d       = static_cast<std::uint32_t>(world_3d_.island_count());
        t.active_ragdolls  = static_cast<std::uint32_t>(ragdolls_3d_.size());
        t.active_vehicles  = static_cast<std::uint32_t>(vehicles_3d_.size());
        t.fracture_systems = static_cast<std::uint32_t>(fracture_systems_3d_.size());
        t.xpbd_soft_bodies = static_cast<std::uint32_t>(xpbd_solvers_3d_.size());
        t.total_steps      = total_steps_;
        return t;
    }

private:
    v2d::PhysicsWorld2D                              world_2d_ {};
    v3d::PhysicsWorld3D                              world_3d_ {};
    rockstar::BuoyancyFluidField3D                   fluid_field_3d_ {};
    std::vector<rockstar::ActiveRagdollEuphoria3D>   ragdolls_3d_ {};
    std::vector<rockstar::PacejkaVehicleDynamics3D>  vehicles_3d_ {};
    std::vector<rockstar::FractureDestructionSystem3D> fracture_systems_3d_ {};
    std::vector<rockstar::XpbdClothSoftBodySolver3D> xpbd_solvers_3d_ {};
    std::uint64_t                                    total_steps_ { 0u };
};

}  // namespace cd::physics
