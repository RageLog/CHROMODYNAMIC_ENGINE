// =============================================================================
// CHROMODYNAMIC - cd/game/input_recorder/InputRecorder.hpp
// Phase 651 - cd::game::input_recorder (M10 W4B: HL2-style input record/replay)
//
// Record player input events to disk with deterministic timestamps, then
// replay them frame-accurately. Primary use-cases:
//
//   1. Debug replay: a developer records a 30-second movement sequence and
//      replays it 100x to reproduce a flaky physics or animation interaction.
//   2. Demo capture: HL2-style per-session demo file (.cdinput) that can be
//      packaged, shared, and played back in any build of the engine.
//   3. Regression testing: golden-input files committed to test/ as fixtures;
//      CI replays them and compares simulation state hashes.
//
// Binary file format (little-endian, version 1):
//   [0..3]  magic   : "CDIR"  (4 bytes)
//   [4]     version : 0x01    (1 byte)
//   [5..12] count   : uint64_t event count
//   [13..N] events  : packed array of InputEventRecord (32 bytes each)
//
// Each InputEventRecord on disk (32 bytes):
//   [0..7]   timestamp_ms : double (8 bytes)
//   [8..11]  kind         : uint32_t
//   [12..15] code         : uint32_t
//   [16..31] payload      : 4 × float (16 bytes)
//
// Threading: NOT thread-safe. Mutate from the owning thread.
//
// Dependencies (CLAUDE.md §7): cd::core only. No math, no ECS, no render.
//
// Design references:
//   * Valve Corporation. "Half-Life 2 Demo Recording System." Source SDK
//     documentation, 2004. https://developer.valvesoftware.com/wiki/Demo_Recording_Tools
//   * id Software. "Quake 3 Arena Demo Format." Q3A source release, 1999.
//     https://github.com/id-Software/Quake-III-Arena
//   * Blow, Jonathan. "Designing an Input Replay System." GDC 2011 talk notes.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <vector>

namespace cd::game::input_recorder
{

// -----------------------------------------------------------------------------
// InputEventKind - discriminant for InputEvent. Each enumerator represents one
// class of raw input signal. The `code` field further specifies which key,
// button, or axis within that class fired.
// -----------------------------------------------------------------------------
enum class InputEventKind : std::uint32_t
{
    kKeyDown       = 0,  ///< Keyboard key pressed; code = platform key-code.
    kKeyUp         = 1,  ///< Keyboard key released; code = platform key-code.
    kMouseDown     = 2,  ///< Mouse button pressed; code = button index (0=L,1=R,2=M).
    kMouseUp       = 3,  ///< Mouse button released; code = button index.
    kMouseMove     = 4,  ///< Mouse cursor delta; payload[0]=dx, payload[1]=dy (pixels).
    kGamepadButton = 5,  ///< Gamepad button pressed/released; payload[0]=1(down)/0(up).
    kGamepadAxis   = 6,  ///< Gamepad axis update; payload[0]=axis value in [-1,1].
};

// -----------------------------------------------------------------------------
// InputEvent - one timestamped input signal captured during recording.
//
// Fields:
//   timestamp_ms : wall-clock ms from the start of the recording session.
//                  Monotonically increasing within a recording. Used by
//                  Replayer::next_event() to gate delivery against the caller's
//                  current time cursor.
//   kind         : event discriminant (see InputEventKind).
//   code         : device-specific sub-code (key code, button index, axis id).
//   payload      : four floats of additional data (delta-xy, axis value, etc.).
//                  Unused components should be zero-initialised.
// -----------------------------------------------------------------------------
struct InputEvent
{
    double                  timestamp_ms {0.0};
    InputEventKind          kind         {InputEventKind::kKeyDown};
    std::uint32_t           code         {0};
    std::array<float, 4>    payload      {0.0F, 0.0F, 0.0F, 0.0F};
};

// =============================================================================
// Recorder - capture input events and serialise to disk.
//
// Lifecycle:
//   1. start_recording() -- open a new session; any previous buffer is cleared.
//   2. record(ev)        -- append one event. Silently ignored when not active.
//   3. stop_recording()  -- close the session (events are retained in memory).
//   4. save_to_file(path)-- write the binary .cdinput file. Returns false on
//                          I/O error (open failed, write incomplete, etc.).
//   5. event_count()     -- number of events captured in the last session.
//
// Calling record() after stop_recording() or before start_recording() is safe
// (no-op).
// =============================================================================
class Recorder
{
public:
    Recorder()  = default;
    ~Recorder() = default;

    Recorder(const Recorder&)            = delete;
    Recorder& operator=(const Recorder&) = delete;
    Recorder(Recorder&&)                 = default;
    Recorder& operator=(Recorder&&)      = default;

    /// Begin a new recording session. Clears any previous buffer.
    void start_recording();

    /// Append one event to the recording buffer. No-op when not active.
    void record(const InputEvent& ev);

    /// Close the recording session. Events are retained in memory.
    void stop_recording();

    /// Serialise the captured events to a binary .cdinput file.
    /// Returns false if the file could not be created or written completely.
    CD_NODISCARD bool save_to_file(const std::filesystem::path& path) const;

    /// Number of events in the current (or last finished) recording buffer.
    CD_NODISCARD std::size_t event_count() const noexcept;

    /// True while start_recording() is active and stop_recording() has not
    /// been called.
    CD_NODISCARD bool is_recording() const noexcept;

private:
    std::vector<InputEvent> events_   {};
    bool                    active_   {false};
};

// =============================================================================
// Replayer - load a .cdinput file and deliver events to the caller on demand.
//
// Lifecycle:
//   1. load_from_file(path) -- parse and validate the binary file. Returns
//                              false on I/O error or format mismatch.
//   2. next_event(current_ms) -- returns the next undelivered event whose
//                              timestamp_ms <= current_ms, or std::nullopt if
//                              no event is due or all events have been delivered.
//      Call repeatedly until nullopt to drain all events due in one frame.
//   3. all()   -- span over the full event list (for offline inspection).
//   4. reset() -- rewind the cursor to the first event.
//
// Delivery contract: events are delivered in ascending timestamp order
// (guaranteed by the Recorder's append-in-order model and preserved through
// serialisation / deserialisation).
// =============================================================================
class Replayer
{
public:
    Replayer()  = default;
    ~Replayer() = default;

    Replayer(const Replayer&)            = delete;
    Replayer& operator=(const Replayer&) = delete;
    Replayer(Replayer&&)                 = default;
    Replayer& operator=(Replayer&&)      = default;

    /// Load events from a binary .cdinput file. Returns false on failure
    /// (missing file, wrong magic/version, truncated data, etc.).
    CD_NODISCARD bool load_from_file(const std::filesystem::path& path);

    /// Return all loaded events as a read-only span. Empty before load.
    CD_NODISCARD std::span<const InputEvent> all() const noexcept;

    /// Deliver the next event whose timestamp_ms <= current_ms. Returns
    /// std::nullopt when no event is due or all events have been delivered.
    /// Call repeatedly in a loop to drain all due events for the current frame.
    CD_NODISCARD std::optional<InputEvent> next_event(double current_ms);

    /// Rewind the playback cursor to the first event.
    void reset() noexcept;

    /// True when all events have been delivered (cursor past end).
    CD_NODISCARD bool is_finished() const noexcept;

    /// Number of events loaded from the file.
    CD_NODISCARD std::size_t event_count() const noexcept;

private:
    std::vector<InputEvent> events_  {};
    std::size_t             cursor_  {0};
};

}  // namespace cd::game::input_recorder
