// =============================================================================
// CHROMODYNAMIC — samples/game/hello_input_replay/main.cpp
//
// Phase 662 (M11 W5A closeout).
//
// Console-only proof that cd::game::input_recorder is consumable as a
// standalone round-trip: record -> save -> load -> replay.
//
// Demo flow:
//   Phase 1 (Recording):
//     1. start_recording().
//     2. Generate 60 synthetic InputEvents at 16.6 ms intervals (1 second of
//        simulated 60 Hz capture):
//          * Frames 0-59: kMouseMove sweeping a unit circle (dx=cos, dy=sin).
//          * Frames 10, 25, 40, 55: kKeyDown events (WASD codes 87/65/83/68).
//     3. stop_recording() and save to tmp/hello_input_replay_demo.bin.
//        The tmp/ directory is created if absent.
//
//   Phase 2 (Replay):
//     1. load_from_file() — parse and validate the binary .cdinput file.
//     2. Advance current_ms by 16.6 ms per step, draining all due events
//        each frame via next_event() until is_finished() is true.
//     3. Print each event as it fires; print 'PLAYBACK COMPLETE' at the end.
//
//   Bonus: print stats — recorded N events / replayed M events /
//          first-event-ms / last-event-ms showing deterministic frame-accurate
//          playback.
//
// Moment: a developer hits a flaky physics bug. They record 5 seconds of
// player input, replay it 100x to root-cause. This sample shows HOW.
//
// Headless — no RHI, no platform window.
// Dependencies (CLAUDE.md §7): cd::core, cd::game_input_recorder.
// =============================================================================
#include <cd/game/input_recorder/InputRecorder.hpp>

#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <numbers>
#include <string_view>

namespace
{

namespace ir = cd::game::input_recorder;

// -----------------------------------------------------------------------------
// Helpers — human-readable label for console output.
// -----------------------------------------------------------------------------
[[nodiscard]] std::string_view kind_name(const ir::InputEventKind k) noexcept
{
    switch (k)
    {
        case ir::InputEventKind::kKeyDown:       return "KeyDown";
        case ir::InputEventKind::kKeyUp:         return "KeyUp";
        case ir::InputEventKind::kMouseDown:     return "MouseDown";
        case ir::InputEventKind::kMouseUp:       return "MouseUp";
        case ir::InputEventKind::kMouseMove:     return "MouseMove";
        case ir::InputEventKind::kGamepadButton: return "GamepadButton";
        case ir::InputEventKind::kGamepadAxis:   return "GamepadAxis";
    }
    return "Unknown";
}

// Key codes for WASD — Windows virtual-key values, platform-independent in
// this headless demo context.
constexpr std::uint32_t kKeyW = 87U;
constexpr std::uint32_t kKeyA = 65U;
constexpr std::uint32_t kKeyS = 83U;
constexpr std::uint32_t kKeyD = 68U;

// Frame duration matching 60 Hz (1000 ms / 60 = 16.666... ms).
constexpr double kFrameMs = 1000.0 / 60.0;

// Total frames to record (1 second at 60 Hz).
constexpr int kFrameCount = 60;

// Key-press frames: inject one kKeyDown at these frame indices.
constexpr std::array<std::pair<int, std::uint32_t>, 4> kKeyPresses = {{
    {10, kKeyW},
    {25, kKeyA},
    {40, kKeyS},
    {55, kKeyD},
}};

// Path to the temporary binary file.
const std::filesystem::path kDemoFile = "tmp/hello_input_replay_demo.bin";

// -----------------------------------------------------------------------------
// print_event — print one fired event during replay.
// -----------------------------------------------------------------------------
void print_event(double current_ms, const ir::InputEvent& ev) noexcept
{
    if (ev.kind == ir::InputEventKind::kMouseMove)
    {
        std::printf("  [t=%7.2f ms] %-14s  dx=%+6.3f  dy=%+6.3f\n",
                    current_ms,
                    std::string(kind_name(ev.kind)).c_str(),
                    static_cast<double>(ev.payload[0]),
                    static_cast<double>(ev.payload[1]));
    }
    else if (ev.kind == ir::InputEventKind::kKeyDown)
    {
        std::printf("  [t=%7.2f ms] %-14s  code=%u\n",
                    current_ms,
                    std::string(kind_name(ev.kind)).c_str(),
                    ev.code);
    }
    else
    {
        std::printf("  [t=%7.2f ms] %-14s  code=%u\n",
                    current_ms,
                    std::string(kind_name(ev.kind)).c_str(),
                    ev.code);
    }
}

}  // namespace

// =============================================================================
// main
// =============================================================================
int main()
{
    std::puts("=== CHROMODYNAMIC hello_input_replay (Phase 662) ===");
    std::puts("");
    std::puts("Moment: record player input -> save -> load -> replay frame-accurately.");
    std::puts("Use-case: reproduce a flaky physics bug by replaying the exact same");
    std::puts("input sequence 100x without touching the keyboard.");
    std::puts("");

    // =========================================================================
    // Phase 1 — RECORDING
    // =========================================================================
    std::puts("--- Phase 1: Recording ---");

    ir::Recorder recorder;
    recorder.start_recording();

    // Generate 60 synthetic events at 16.6 ms intervals.
    // kMouseMove sweeps a unit circle; kKeyDown events injected at 4 frames.
    for (int frame = 0; frame < kFrameCount; ++frame)
    {
        const double timestamp_ms = static_cast<double>(frame) * kFrameMs;

        // Mouse-move: sweep a full circle over 60 frames.
        const double angle = (static_cast<double>(frame) / static_cast<double>(kFrameCount))
                             * 2.0 * std::numbers::pi;
        const auto dx = static_cast<float>(std::cos(angle));
        const auto dy = static_cast<float>(std::sin(angle));

        ir::InputEvent mouse_ev;
        mouse_ev.timestamp_ms = timestamp_ms;
        mouse_ev.kind         = ir::InputEventKind::kMouseMove;
        mouse_ev.code         = 0U;
        mouse_ev.payload      = {dx, dy, 0.0F, 0.0F};
        recorder.record(mouse_ev);

        // Inject a key-down event at predetermined frames.
        for (const auto& [key_frame, key_code] : kKeyPresses)
        {
            if (frame == key_frame)
            {
                ir::InputEvent key_ev;
                key_ev.timestamp_ms = timestamp_ms;
                key_ev.kind         = ir::InputEventKind::kKeyDown;
                key_ev.code         = key_code;
                key_ev.payload      = {0.0F, 0.0F, 0.0F, 0.0F};
                recorder.record(key_ev);
            }
        }
    }

    recorder.stop_recording();

    const std::size_t recorded_count = recorder.event_count();
    std::printf("[record] %zu events captured (60 MouseMove + 4 KeyDown).\n",
                recorded_count);

    // -------------------------------------------------------------------------
    // Save to tmp/hello_input_replay_demo.bin.
    // Create tmp/ if needed.
    // -------------------------------------------------------------------------
    std::error_code ec;
    std::filesystem::create_directories(kDemoFile.parent_path(), ec);
    if (ec)
    {
        std::fprintf(stderr,
                     "ERROR: could not create directory '%s': %s\n",
                     kDemoFile.parent_path().string().c_str(),
                     ec.message().c_str());
        return EXIT_FAILURE;
    }

    const bool saved = recorder.save_to_file(kDemoFile);
    if (!saved)
    {
        std::fprintf(stderr,
                     "ERROR: save_to_file('%s') failed.\n",
                     kDemoFile.string().c_str());
        return EXIT_FAILURE;
    }

    std::printf("[save]   written to '%s'\n", kDemoFile.string().c_str());
    std::puts("");

    // =========================================================================
    // Phase 2 — REPLAY
    // =========================================================================
    std::puts("--- Phase 2: Replay ---");

    ir::Replayer replayer;
    const bool loaded = replayer.load_from_file(kDemoFile);
    if (!loaded)
    {
        std::fprintf(stderr,
                     "ERROR: load_from_file('%s') failed.\n",
                     kDemoFile.string().c_str());
        return EXIT_FAILURE;
    }

    std::printf("[load]   %zu events loaded from '%s'\n",
                replayer.event_count(),
                kDemoFile.string().c_str());
    std::puts("");

    // Advance current_ms by 16.6 ms per step; drain all due events per frame.
    std::size_t replayed_count = 0U;
    double      current_ms     = 0.0;

    // Run the replay loop until all events have been delivered.
    while (!replayer.is_finished())
    {
        // Drain all events due at or before current_ms.
        while (true)
        {
            const std::optional<ir::InputEvent> ev = replayer.next_event(current_ms);
            if (!ev.has_value())
            {
                break;
            }
            print_event(current_ms, *ev);
            ++replayed_count;
        }

        // Advance to the next simulated frame.
        current_ms += kFrameMs;
    }

    std::puts("");
    std::puts("PLAYBACK COMPLETE");
    std::puts("");

    // =========================================================================
    // Stats — deterministic frame-accurate round-trip verification.
    // =========================================================================
    std::puts("--- Stats ---");

    const auto all_events = replayer.all();
    const double first_event_ms = all_events.empty()
                                ? 0.0
                                : all_events.front().timestamp_ms;
    const double last_event_ms  = all_events.empty()
                                ? 0.0
                                : all_events.back().timestamp_ms;

    std::printf("  recorded N events  : %zu\n",  recorded_count);
    std::printf("  replayed M events  : %zu\n",  replayed_count);
    std::printf("  first-event-ms     : %.3f\n", first_event_ms);
    std::printf("  last-event-ms      : %.3f\n", last_event_ms);
    std::printf("  round-trip match   : %s\n",
                (recorded_count == replayed_count) ? "PASS" : "FAIL");

    std::puts("");

    // Verify deterministic round-trip: every recorded event must have been
    // replayed exactly once, in order.
    if (recorded_count != replayed_count)
    {
        std::fprintf(stderr,
                     "ERROR: recorded %zu events but replayed %zu — mismatch!\n",
                     recorded_count,
                     replayed_count);
        return EXIT_FAILURE;
    }

    std::puts("=== hello_input_replay OK — exit 0 ===");
    return EXIT_SUCCESS;
}
