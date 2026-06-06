# cd::physics::vehicle

CPU-side **4-wheel vehicle model** (bicycle-model + gear-ratio torque
chain) with an optional `JoltAdapter` bridge for terrain contact.

## Sprints

| Sprint | Phase | Surface |
|--------|-------|---------|
| 1 | `phase670` | `Vehicle` — CPU bicycle-model + 6-gear torque chain. |
| 2 | `phase692` | `JoltAdapter` — bridge to `cd::physics::IPhysicsWorld` so chassis responds to terrain. |
| 3 | queued    | `WheelConstraint` per-wheel Jolt body, full ray-cast suspension via `JPH::WheelConstraint*`. |

## Public surface

```cpp
namespace cd::physics::vehicle {

struct WheelConfig
{
    cd::math::Vec3f     offset;            // chassis-local
    float               radius;
    float               suspension_rest;
    float               suspension_stiffness;
    float               suspension_damping;
    bool                steerable;
    bool                driven;
};

struct EngineConfig
{
    float               max_torque_nm;
    std::array<float,6> gear_ratios;
    float               final_drive_ratio;
    float               max_rpm;
    float               idle_rpm;
};

struct VehicleConfig
{
    cd::math::Vec3f               chassis_offset;
    std::array<WheelConfig,4>     wheels;
    EngineConfig                  engine;
    bool                          use_jolt;        // Sprint-2 flag (default false)
};

struct VehicleInput
{
    float throttle;    // [0, 1]
    float brake;       // [0, 1]
    float steer;       // [-1, 1]
    int   gear_shift;  // +1 up / -1 down / 0 none
};

struct VehicleState
{
    float       speed_mps;
    float       rpm;
    uint32_t    gear;
    float       steer_radians;
};

class Vehicle
{
public:
    void                          configure(VehicleConfig);
    void                          configure_jolt(cd::physics::IPhysicsWorld&,
                                                 cd::physics::BodyHandle chassis_body);
    void                          set_input(VehicleInput);
    void                          tick(float dt);

    [[nodiscard]] const VehicleState&  state() const noexcept;
};

class JoltAdapter
{
public:
    void                          configure(cd::physics::IPhysicsWorld&,
                                            cd::physics::BodyHandle chassis_body);
    void                          sync_to_jolt(const Vehicle&);
    void                          sync_from_jolt(Vehicle&);
};

struct WheelConstraintDesc { /* Sprint-3 stub */ };

}
```

## Tick semantics

1. Engine torque curve sampled at current RPM →
   `engine_torque = max_torque * curve(rpm / max_rpm)`.
2. Gear ratio applied → wheel torque = `engine_torque * gear_ratios[gear] * final_drive`.
3. Bicycle model integrates: chassis position + heading update from
   forward speed + steer angle + slip (Pacejka simplified — Sprint-1
   uses linear tyre).
4. RPM updated from wheel rotational velocity (rear-wheel-drive
   assumption — drive flags on `WheelConfig` queued for Sprint-3).
5. If `use_jolt` and `JoltAdapter` configured:
   * `sync_to_jolt(*this)` writes chassis transform into Jolt body.
   * Caller calls `IPhysicsWorld::step()`.
   * `sync_from_jolt(*this)` reads chassis pose + velocity back.

The Sprint-1 path runs **entirely without** Jolt — useful for design
iteration, CI smoke, and unit tests that don't want to bring up the
rigid-body backend.

## Sprint-3 WheelConstraint roadmap

Once the vcpkg `jolt-physics` port exposes `JPH::WheelConstraint`,
each `WheelConfig` produces a per-wheel constraint:

* Ray-cast suspension along the chassis-local `−Y` axis.
* Slip-angle based steering torque via `WheelConstraint::SetSteerAngle`.
* Engine torque distributed to driven wheels via `WheelConstraint::SetEngineTorque`.

Today's `WheelConstraintDesc` is a placeholder POD for the future
descriptor; consumers should not rely on its fields.

## Dependencies

* `cd::physics` — `IPhysicsWorld` + `BodyHandle` (PUBLIC; header
  transitive).
* `cd::physics_jolt` — `is_stub_backend()` used by tests (PRIVATE;
  callers should not need `JoltWorld.hpp` to drive a `Vehicle`).
* `cd::math` — `Vec3f`, transitive via `cd::physics`.
* `cd::core` — base defines, transitive.
