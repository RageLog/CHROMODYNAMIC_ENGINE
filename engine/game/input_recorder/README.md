# cd::game::input_recorder

HL2-style **input record/replay**: capture player input events with
deterministic timestamps to a binary `.cdinput` file; replay them
frame-accurately via `Replayer::next_event()`. The library enables:

* **Deterministic regression testing.** Commit a `.cdinput` as a
  golden fixture; the replay produces an identical simulation pass
  every CI run.
* **Demo capture.** Record a 30-second movement sequence, replay
  100× to reproduce a flaky physics / animation interaction.
* **CI smoke.** Run the same input sequence on every PR to guard
  against subtle simulation drift (clock drift, RNG seed leaks,
  job-system reordering).

`cd::core` is the only dependency. No math, no ECS, no render. The
library is a pure state machine + binary serialiser; the engine routes
replayed events to the appropriate input backend from the outside.

## Public surface

```cpp
namespace cd::game::input_recorder {

enum class EventKind : uint8_t
{
    kKeyDown, kKeyUp,
    kMouseMove, kMouseButton,
    kGamepadButton, kGamepadAxis,
};

struct InputEvent
{
    uint64_t   t_ns;            // monotonic, recorder-anchored
    EventKind  kind;
    uint32_t   code_or_axis;
    float      value;           // axis position / mouse delta / 0|1 for buttons
};

class Recorder
{
public:
    void                                  start(std::filesystem::path);
    void                                  push(InputEvent);
    cd::expected<void, Error>             finish();           // flush + close
};

class Replayer
{
public:
    cd::expected<void, Error>             open(std::filesystem::path);
    [[nodiscard]] std::optional<InputEvent>  next_event();    // monotonic
    [[nodiscard]] bool                    finished() const noexcept;
};

}
```

## Binary format

```
[8 B  magic  "CDINPUT\x01"]
[4 B  event_count]
for event in events:
    [8 B  t_ns      ]
    [1 B  kind      ]
    [4 B  code_or_axis]
    [4 B  value (float)]
```

Little-endian throughout. The format is intentionally simple — no
compression, no per-event delta-encoding. A 60-FPS recording with
8 events/frame for 30 s is 8 × 60 × 30 × 17 = 244 KB; trivially
small to commit alongside golden tests.

## Determinism contract

`Recorder::push` is monotonic — `t_ns` must be non-decreasing. The
engine surfaces the monotonic clock via `cd::frame_timing`; passing
the same monotonic source to both record and replay sites is the
caller's responsibility.

`Replayer::next_event` returns events in the order they were
written; it does **not** internally re-clock to wall time. The caller
walks a frame, asks `next_event` repeatedly, and stops when the
returned `t_ns` exceeds the current simulation tick — same pattern as
draining a per-frame event queue.

## Usage

```cpp
// Record
cd::game::input_recorder::Recorder rec;
rec.start("regression/movement-spike.cdinput");
on_input([&](Event ev) {
    rec.push({ now_ns(), to_kind(ev.kind), ev.code, ev.value });
});
rec.finish();

// Replay
cd::game::input_recorder::Replayer rep;
rep.open("regression/movement-spike.cdinput");
while (auto e = rep.next_event())
{
    if (e->t_ns > current_sim_t_ns) break;
    route_to_input_backend(*e);
}
```
