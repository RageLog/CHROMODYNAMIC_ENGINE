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

enum class InputEventKind : std::uint32_t
{
    kKeyDown       = 0,  ///< Keyboard key pressed; code = platform key-code.
    kKeyUp         = 1,  ///< Keyboard key released; code = platform key-code.
    kMouseDown     = 2,  ///< Mouse button pressed; code = button index.
    kMouseUp       = 3,  ///< Mouse button released; code = button index.
    kMouseMove     = 4,  ///< Mouse delta; payload[0]=dx, payload[1]=dy (pixels).
    kGamepadButton = 5,  ///< Gamepad button; payload[0]=1(down)/0(up).
    kGamepadAxis   = 6,  ///< Gamepad axis; payload[0]=value in [-1,1].
};

struct InputEvent
{
    double                timestamp_ms {0.0};          // ms from session start
    InputEventKind        kind         {InputEventKind::kKeyDown};
    std::uint32_t         code         {0};
    std::array<float, 4>  payload      {};
};

class Recorder
{
public:
    void        start_recording();
    void        record(const InputEvent& ev);
    void        stop_recording();
    [[nodiscard]] bool        save_to_file(const std::filesystem::path& path) const;
    [[nodiscard]] std::size_t event_count() const noexcept;
    [[nodiscard]] bool        is_recording() const noexcept;
};

class Replayer
{
public:
    [[nodiscard]] bool                      load_from_file(const std::filesystem::path& path);
    [[nodiscard]] std::span<const InputEvent> all() const noexcept;
    [[nodiscard]] std::optional<InputEvent> next_event(double current_ms);
    void        reset() noexcept;
    [[nodiscard]] bool        is_finished()  const noexcept;
    [[nodiscard]] std::size_t event_count()  const noexcept;
};

}  // namespace cd::game::input_recorder
```

## Binary format (CDIR v1, little-endian)

```text
Header (13 bytes):
  [0..3]  magic   : "CDIR"  (4 × char, not null-terminated)
  [4]     version : 0x01    (uint8_t)
  [5..12] count   : uint64_t — number of event records that follow

Body (count × 32 bytes per record):
  [0..7]   timestamp_ms : double   (IEEE 754 binary64)
  [8..11]  kind         : uint32_t (InputEventKind underlying value)
  [12..15] code         : uint32_t
  [16..31] payload      : 4 × float (16 bytes)
```

All multi-byte fields use native byte order (little-endian on x86/ARM64 LE).
The format is intentionally simple — no compression, no delta-encoding.
A 60-FPS recording with 8 events/frame for 30 s produces ≈ 460 KB.

## Determinism contract

`Recorder::record` appends events in insertion order. Timestamps must be
non-decreasing within a session; the library does **not** enforce or sort —
this is the caller's responsibility (use `cd::frame_timing` as the source).

`Replayer::next_event(current_ms)` delivers the next event whose
`timestamp_ms <= current_ms`, advancing an internal cursor by one on each
call. Call repeatedly in a loop to drain all due events for the current
frame tick; stop when `std::nullopt` is returned. `reset()` rewinds the
cursor to allow re-replay from the same loaded buffer.

## Usage

```cpp
// Record
cd::game::input_recorder::Recorder rec;
rec.start_recording();
on_input([&](RawEvent ev) {
    InputEvent ie;
    ie.timestamp_ms = now_ms_from_session_start();
    ie.kind  = to_input_event_kind(ev.kind);
    ie.code  = ev.code;
    rec.record(ie);
});
rec.stop_recording();
rec.save_to_file("regression/movement-spike.cdinput");

// Replay
cd::game::input_recorder::Replayer rep;
rep.load_from_file("regression/movement-spike.cdinput");
while (const auto e = rep.next_event(current_sim_ms))
{
    route_to_input_backend(*e);
}
```
