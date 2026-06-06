# cd::animation::ik

CCD (Cyclic Coordinate Descent) **inverse-kinematics solver**.

Sidecar to `cd::anim` — the two libraries split responsibility cleanly:

| Library             | Concern                                           |
|---------------------|---------------------------------------------------|
| `cd::anim`          | Forward kinematics: skeleton + clip + pose + LBS. |
| `cd::animation::ik` | Inverse kinematics: WHERE joints should point.    |

No dependency on `cd::anim` — the IK library is a pure math sidecar
over joint chain inputs. No OS API calls.

## Sprints

| Sprint | Phase | Surface |
|--------|-------|---------|
| 1 | `phase702` | Pure CCD, no joint-angle constraints. Single-threaded. |
| 2 | `phase727` | `JointLimits` per-joint Euler clamp + `JointLimitPresets` factory. |
| 3 | queued    | FABRIK alternative solver, multi-chain, swing/twist decomposition. |

## CCD algorithm

```
  Input:  Joint chain  J0 → J1 → … → Jn (root → end effector)
          Target world position T

  Repeat until ||end_pos − T|| < ε or iters == max:
      For i = n−1 down to 1:
          v_end = end_pos − J[i]
          v_tgt = T       − J[i]
          θ = signed_angle(v_end, v_tgt) around plane normal
          if JointLimits enabled:
              θ = clamp(θ, J[i].limit.min, J[i].limit.max)
          J[i].rotation = rotation(axis, θ) * J[i].rotation
          recompute downstream end_pos
```

Convergence is empirical — typically 8–16 iterations for a 5-joint
arm reaching a reasonable target. Hard-to-reach targets (beyond the
chain's max extension) converge to the closest reachable point.

## Public surface

```cpp
namespace cd::animation::ik {

struct Joint
{
    cd::math::Vec3f       position;
    cd::math::Quaternion  rotation;
};

struct JointLimits
{
    cd::math::Vec3f  euler_min;        // per-axis radians
    cd::math::Vec3f  euler_max;
    bool             enabled;
};

struct ChainConfig
{
    uint32_t   max_iters    { 16 };
    float      tolerance    { 0.001F };   // metres
};

class CcdSolver
{
public:
    void                          set_chain(std::span<Joint> joints,    // in/out
                                            std::span<const JointLimits> limits);
    void                          solve(cd::math::Vec3f target,
                                        ChainConfig config = {});
    [[nodiscard]] bool            converged() const noexcept;
    [[nodiscard]] float           residual()  const noexcept;
};

namespace JointLimitPresets
{
    JointLimits human_shoulder();
    JointLimits human_elbow();      // 1 DOF, hinge-only
    JointLimits human_knee();       // 1 DOF, hinge-only
    JointLimits human_finger();
}

}
```

## Joint limit presets

The `JointLimitPresets` factory ships sensible defaults so a designer
can wire a humanoid IK chain without authoring Euler bounds by hand.
Values match the bioRxiv 2018 anthropometric range-of-motion data
referenced by the IK clamp in [W8-AY-bake-budget-revert](../../docs/ADR/ADR-20260529-W8-AY-bake-budget-revert.md).

## Sprint-3 roadmap

FABRIK (Forward And Backward Reaching IK) is the planned alternative
solver — converges faster than CCD on long chains, especially when
the end effector starts far from the target. Will ship behind a
`Solver::kFabrik` enum on `ChainConfig`, with the CCD path retained
for backwards compatibility and for the joint-limit-clamp case (where
CCD remains the simpler integration).

## Dependencies

* `cd::core` — `Defines.hpp`.

`cd::math` is included transitively through `cd::core::Vec3f` /
`Quaternion` — no separate link line.
