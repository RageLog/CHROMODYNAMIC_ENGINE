# cd::game::ai_director

High-level AI pacing controller in the Left 4 Dead / Skyrim Radiant
Quest tradition. Tracks a single floating-point `intensity` value in
`[0, 1]`, classifies it into a coarse `TensionState`
(`kIdle / kBuildUp / kPeak / kRelief`), and decides when to spawn
`EncounterTemplate` records keyed to the current pacing level.

The Director is a **pure state machine** — `cd::core` is the only
dependency. The caller wires ECS / audio / render side-effects on
encounter activation; this library never reaches across the game/engine
boundary.

## Tension model

```
  intensity     state         behaviour
  ───────────   ───────       ─────────────────────────────────────────
  [0.00, 0.25)  kIdle         calm; natural decay
  [0.25, 0.60)  kBuildUp      mounting threat; Director primes
  [0.60, 0.85)  kPeak         high-pressure; Director fires encounters
  [0.85, 1.00]  kRelief       accelerated decay regardless of input
```

`kRelief` latches on the first frame `intensity` reaches `0.85`. It
forces decay until `intensity` falls below the `kBuildUp` threshold,
then drops back to `kIdle`. This mirrors the original L4D "Director"
post-peak rest pattern.

## Public surface

```cpp
namespace cd::game::ai_director {

enum class TensionState : std::uint8_t { kIdle, kBuildUp, kPeak, kRelief };

struct EncounterTemplate
{
    std::uint32_t tier;                   // 0 = easiest
    float         intensity_contribution; // added to intensity on spawn
    std::string   id;
};

class AiDirector
{
public:
    void                       set_decay_rate(float per_second);
    void                       feed_event(float intensity_delta);
    void                       tick(float dt);
    [[nodiscard]] float        intensity()      const noexcept;
    [[nodiscard]] TensionState state()          const noexcept;

    void                       register_encounter(EncounterTemplate);
    [[nodiscard]] std::optional<EncounterTemplate>
                               request_next_encounter() noexcept;
};

}
```

### Event flow

```cpp
AiDirector d;
d.set_decay_rate(0.10F);                       // intensity drops 0.10/s
d.register_encounter({ 0, 0.20F, "scout"   }); // tier 0, +20%
d.register_encounter({ 2, 0.40F, "horde"   }); // tier 2, +40%
d.register_encounter({ 4, 0.65F, "boss"    }); // tier 4, +65%

d.feed_event(+0.15F);                          // player took damage
d.tick(0.016F);                                // every game tick
if (d.state() == TensionState::kPeak)
    if (auto e = d.request_next_encounter())
        spawn_encounter_in_world(*e);          // caller's responsibility
```

## Encounter scheduling

`request_next_encounter()` walks every registered template and returns
the **highest-tier** one whose `tier ≤ floor(intensity × max_tier)`. A
simple linear mapping that automatically scales challenge to the current
pacing without per-frame author intervention. Calling it consumes the
returned encounter from a one-shot queue.

## Threading

NOT thread-safe. Drive from the game-logic thread; read results on the
same thread. The state-machine surface is intentionally tiny — wrap in
a mutex at the caller if a worker job pumps events.

## References

* Booth, M. *The AI Systems of Left 4 Dead.* AIIDE 2009 / Valve.
* Bethesda Game Studios. *Radiant Quest System.* GDC 2012.
* Yannakakis & Togelius. *Artificial Intelligence and Games.*
  Springer 2018, Chapter 5 (dynamic difficulty adjustment).
