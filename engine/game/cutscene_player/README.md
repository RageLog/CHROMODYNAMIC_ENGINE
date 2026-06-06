# cd::game::cutscene_player

Data-driven cutscene timeline player. A `Cutscene` is a sequence of
`CutscenePhase` entries; each phase has a duration and a list of
`CutsceneEvent` entries to fire at specific offsets within the phase.
The player ticks an internal clock, surfaces `CutsceneEvent`s through a
poll API, and signals end-of-cutscene through state transitions.

The library is **pure data + state machine** — no audio, no ECS, no
render. Designers author cutscenes as JSON; the engine fires the
events through whatever side-effect sinks it cares about (call a
camera animation, trigger a dialogue line, fade the screen, etc.).

## Sprints

| Sprint | Phase | Surface |
|--------|-------|---------|
| 1 | `phase610 / M7 W4B`  | `CutscenePlayer` state machine + event polling. |
| 2 | `phase703 / M15 W4B` | JSON round-trip (`save_to_json` / `load_from_json`). |

## Data model

```cpp
namespace cd::game::cutscene_player {

struct CutsceneEvent
{
    float        offset_seconds;     // relative to phase start
    std::string  id;                 // e.g. "audio.play.line_007"
    std::string  payload;            // opaque to the player; caller-defined
};

struct CutscenePhase
{
    float                          duration_seconds;
    std::vector<CutsceneEvent>     events;
};

struct Cutscene
{
    std::string                    id;
    std::vector<CutscenePhase>     phases;
};

}
```

## Player surface

```cpp
namespace cd::game::cutscene_player {

enum class State : uint8_t { kIdle, kPlaying, kFinished };

class CutscenePlayer
{
public:
    void                                   play(Cutscene);
    void                                   stop();
    void                                   tick(float dt);
    [[nodiscard]] State                    state()      const noexcept;
    [[nodiscard]] float                    time_seconds() const noexcept;

    // Drains every event whose offset has been crossed since the
    // last call. Caller fires the side-effects.
    [[nodiscard]] std::vector<CutsceneEvent> drain_events();
};

}
```

`tick(dt)` advances the clock and accumulates any `CutsceneEvent`s
whose `(phase_start + offset_seconds) ≤ current_time`. `drain_events()`
returns and clears the pending list — the design matches the
`cd::frame_timing` poll-and-clear pattern.

## JSON round-trip (sprint 2)

```cpp
cd::expected<void, Error>      save_to_json(const Cutscene&,
                                            const std::filesystem::path&);
cd::expected<Cutscene, Error>  load_from_json(const std::filesystem::path&);
```

The on-disk schema is human-editable. Designer hot-reload uses
`load_from_json` + `play()` on every save.

## Dependencies

* `cd::core` — Defines, expected, error infra.
* `cd::asset_json` — Sprint-2 only; the `.cpp` is gated so the
  Sprint-1 surface stays portable to environments without
  `cd::asset_json` (test-only builds).
