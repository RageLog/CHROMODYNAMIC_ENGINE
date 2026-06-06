# cd::ai::squad

Multi-agent squad coordination layer. Pattern reference: Left 4 Dead
Special Infected coordination + Halo Flood squad logic. A `Squad` owns
a set of agents plus a `Blackboard` (shared knowledge), exposes a
ticking API, and computes a `Formation` snapshot (centroid + radius
+ member count) each frame.

## Sprints

| Sprint | Phase | Surface |
|--------|-------|---------|
| 1 | `phase682 / M13 W5A` | CPU squad / blackboard / formation tick. |
| 2 | `phase712 / M16 W4`  | Opt-in compute-shader formation solver (`GpuBatchSolver`). |

Sprint-2 is **dormant by default** (`CD_AI_SQUAD_ENABLE_GPU=OFF`):
the `.cpp` is not compiled, so `cd::rhi` does not fan into a default
`ninja-debug` build. Flipping the option ON pulls in the shader compile
+ GPU solver path; the matching gtest (`cd_test_squad_gpu_batch`)
`GTEST_SKIP`s when the option is OFF. This split exists because the
M15 attempt OOM'd when `clang-cl` pulled the rhi compile graph into
every default build.

## Public surface

```cpp
namespace cd::ai::squad {

enum class SquadRole : std::uint8_t
{
    kPointman, kFlanker, kSuppressor, kCoverer
};

struct Formation
{
    cd::math::Vec3f centroid;
    float           radius;
    std::uint32_t   member_count;
};

class Blackboard
{
public:
    void  set_target(EntityHandle);
    void  set_threat_level(float);
    void  set_fact(std::string_view key, std::string value);
    /* getters mirror the setters */
};

class Squad
{
public:
    void              add_member(EntityHandle, SquadRole);
    void              remove_member(EntityHandle);
    void              update_member_position(EntityHandle, cd::math::Vec3f);
    void              tick(float dt);                  // recomputes formation + decays threat
    const Formation&  formation() const noexcept;
    Blackboard&       blackboard()  noexcept;
};

#ifdef CD_AI_SQUAD_ENABLE_GPU
class GpuBatchSolver { /* compute-shader formation solver, opt-in */ };
#endif
}
```

## Tick semantics

`Squad::tick(dt)`:

1. Compute the centroid (mean of member positions).
2. Compute the radius (max distance from centroid to any member).
3. Apply exponential decay to `Blackboard::threat_level` with a
   1.5-second half-life.

Order is fixed so an exterior caller can `tick()` once per frame
without races: the formation is a snapshot of the *previous* member
positions plus the *new* threat decay; subsequent member updates take
effect on the next tick.

## Naming-discipline note

This library is `cd::ai::squad` (namespace) but the CMake target is
`game_ai_squad` to disambiguate from any future `cd::ai::*` libraries
landing under `engine/ai/`. The namespace is shorter on purpose so
call sites read `cd::ai::squad::SquadRole::kPointman` rather than
`cd::game::ai_squad::SquadRole::kPointman`.

## References

* Booth, M. *The AI Systems of Left 4 Dead.* AIIDE 2009.
* Yannakakis & Togelius. *Artificial Intelligence and Games.*
  Springer, 2018.
