// =============================================================================
// CHROMODYNAMIC — samples/game/hello_game/main.cpp
//
// Phase 479 (G1.5 closeout of ADR-20260530-gameplay-library-family).
//
// Minimal console "game" that ties together the four Phase-G1 libraries:
//
//   1. cd::gameplay_time              — fixed-tick TimeKeeper main loop.
//   2. cd::gameplay_input_binding     — ActionMap binds "jump" to keyboard
//                                       Space AND gamepad A simultaneously.
//   3. cd::game_save                  — atomic slot save / restore of a few
//                                       entity transforms (slot "slot_01").
//   4. cd::game_settings              — persistent volume (float) + language
//                                       (string) across runs (.ini next to
//                                       the exe so the round-trip is
//                                       observable from the shell).
//
// Run flow (each frame of a short fixed-tick loop):
//
//   * inject a synthetic input snapshot that "presses" the Jump key once
//     mid-loop so the callback rising-edge is visibly fired exactly once;
//   * step the TimeKeeper with the canonical 1/60 dt; print frame index +
//     elapsed gameplay seconds for the first / last frame so the demo
//     proves the clock is ticking and pause works;
//   * after the loop, snapshot three "entity transforms" into a tiny
//     binary blob, write slot "slot_01", then load it back and confirm
//     the bytes round-trip;
//   * bump the volume by +0.05 and toggle language between "en" and "tr"
//     on every run, then save the .ini — re-running the sample prints
//     the previous-run values BEFORE overwriting them, proving
//     cross-run persistence.
//
// Headless: console output only — no platform window, no RHI. The sample
// is the build-gate for G1; "compiles + exits 0" is the contract.
// =============================================================================
#include <cd/core/Result.hpp>
#include <cd/game/save/Save.hpp>
#include <cd/game/settings/Settings.hpp>
#include <cd/gameplay/input_binding/InputBinding.hpp>
#include <cd/gameplay/time/Time.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace
{

namespace gt  = cd::gameplay::time;
namespace gib = cd::gameplay::input_binding;
namespace gs  = cd::game::save;
namespace gst = cd::game::settings;

// ---------------------------------------------------------------------------
// Synthetic scancode + gamepad codes — production code would source these
// from cd::input::KeyCode / GamepadButton, but the sample stays headless so
// we hand-pick stable ints. Match the values used in the Phase 462 unit
// tests so the binding semantics are recognisable.
// ---------------------------------------------------------------------------
constexpr int kKeySpace = 32;   ///< ASCII space — sample keyboard convention.
constexpr int kPadA     = 1;    ///< Gamepad "A" — Xbox naming, matches test.

// ---------------------------------------------------------------------------
// Entity transform mini-POD — three floats for position, one for yaw. The
// world has a deliberately tiny entity count so the saved blob fits in one
// std::array<std::byte, N> and the on-disk format is obvious to inspect.
// ---------------------------------------------------------------------------
struct EntityTransform
{
    float position_x { 0.0F };
    float position_y { 0.0F };
    float position_z { 0.0F };
    float yaw_radians { 0.0F };
};

constexpr std::size_t kEntityCount = 3;

[[nodiscard]] std::vector<std::byte>
serialize_world(const std::array<EntityTransform, kEntityCount>& world)
{
    std::vector<std::byte> bytes(sizeof(EntityTransform) * kEntityCount);
    std::memcpy(bytes.data(), world.data(), bytes.size());
    return bytes;
}

[[nodiscard]] bool
deserialize_world(std::span<const std::byte>                 bytes,
                  std::array<EntityTransform, kEntityCount>& out)
{
    if (bytes.size() != sizeof(EntityTransform) * kEntityCount)
    {
        return false;
    }
    std::memcpy(out.data(), bytes.data(), bytes.size());
    return true;
}

// ---------------------------------------------------------------------------
// Pick a settings file path that lives next to whatever working directory
// the sample was launched from — keeps the cross-run persistence visible
// from a normal shell prompt and avoids polluting the user's real
// preferences folder.
// ---------------------------------------------------------------------------
[[nodiscard]] std::string settings_path()
{
    const auto cwd = std::filesystem::current_path();
    return (cwd / "hello_game_settings.ini").string();
}

// ---------------------------------------------------------------------------
// Pick a sandboxed save root next to the exe so the sample never writes
// into the developer's real CHROMODYNAMIC user-data path. The contents
// survive across runs but a `rm -rf` of the working dir wipes them.
// ---------------------------------------------------------------------------
[[nodiscard]] std::filesystem::path save_root()
{
    return std::filesystem::current_path() / "hello_game_saves";
}

}  // namespace

// =============================================================================
// main — drives the four subsystems through one synthetic frame loop, then
// rounds the demo off with a save-roundtrip and a settings-persist step.
//
// Console output is the deliverable; the build gate (per task brief) is the
// compile, not a ctest. Anything non-zero from this function indicates a
// real failure in one of the G1 libraries that should be triaged before
// closing G1.
// =============================================================================
int main()
{
    std::puts("=== CHROMODYNAMIC hello_game (G1 closeout, Phase 479) ===");

    // -----------------------------------------------------------------------
    // 1. Settings — load any prior run's prefs, print them, then bump.
    //    The .ini lives next to the working directory so multiple runs are
    //    observable from the shell (`type hello_game_settings.ini`).
    // -----------------------------------------------------------------------
    gst::Settings settings;
    const std::string ini_path = settings_path();

    // Live-broadcast observer wired BEFORE load so subscribers fire on
    // apply (Phase 471 ChangeCallback contract — see Settings.hpp §observer).
    settings.on_change("audio.master_volume", [](const gst::Value& v) {
        if (const auto* f = std::get_if<float>(&v))
        {
            std::printf("  [observer] audio.master_volume -> %.3f\n",
                        static_cast<double>(*f));
        }
    });
    settings.on_change("game.language", [](const gst::Value& v) {
        if (const auto* s = std::get_if<std::string>(&v))
        {
            std::printf("  [observer] game.language -> %s\n", s->c_str());
        }
    });

    const bool loaded = settings.load(ini_path);
    if (loaded)
    {
        std::printf("Settings: loaded prior run from %s\n", ini_path.c_str());
    }
    else
    {
        std::printf("Settings: first run (no %s yet)\n", ini_path.c_str());
    }

    const float       prev_volume   = settings.get<float>("audio.master_volume").value_or(0.50F);
    const std::string prev_language = settings.get<std::string>("game.language").value_or(std::string{"en"});

    std::printf("  prior volume   = %.3f\n", static_cast<double>(prev_volume));
    std::printf("  prior language = %s\n", prev_language.c_str());

    // Bump for this run: +0.05 volume (clamped to <= 1.0F) and toggle language.
    float       next_volume   = prev_volume + 0.05F;
    if (next_volume > 1.0F)
    {
        next_volume = 0.50F;  // wrap back so repeated runs stay in [0.50, 1.00]
    }
    const std::string next_language = (prev_language == "en") ? "tr" : "en";

    settings.set<float>("audio.master_volume", next_volume);
    settings.set<std::string>("game.language", next_language);

    if (!settings.save(ini_path))
    {
        std::fprintf(stderr, "Settings: save to %s FAILED\n", ini_path.c_str());
        return EXIT_FAILURE;
    }
    std::printf("Settings: wrote %s (volume=%.3f, language=%s)\n",
                ini_path.c_str(),
                static_cast<double>(next_volume),
                next_language.c_str());

    // -----------------------------------------------------------------------
    // 2. Input — bind "jump" to BOTH keyboard space and gamepad A. The
    //    rising-edge callback fires exactly once per press transition, so
    //    we expect a single fire across the synthetic loop below.
    // -----------------------------------------------------------------------
    gib::ActionMap actions;
    if (!actions.bind_button("jump", "keyboard", kKeySpace))
    {
        std::fprintf(stderr, "ActionMap: bind keyboard Space FAILED\n");
        return EXIT_FAILURE;
    }
    if (!actions.bind_button("jump", "gamepad", kPadA))
    {
        std::fprintf(stderr, "ActionMap: bind gamepad A FAILED\n");
        return EXIT_FAILURE;
    }

    int jump_fire_count = 0;
    actions.on_action("jump", [&](const std::string& name) {
        ++jump_fire_count;
        std::printf("  [action] %s rising edge (fire #%d)\n",
                    name.c_str(), jump_fire_count);
    });

    std::printf("Input: bound 'jump' to keyboard Space + gamepad A "
                "(binding_count=%zu)\n",
                actions.binding_count("jump"));

    // -----------------------------------------------------------------------
    // 3. Time + main loop — fixed dt of 1/60 for 60 frames (one nominal
    //    second). At frame 30 we inject a "jump" press to verify the
    //    rising-edge callback fires; at frame 45 we pause for the rest of
    //    the loop to show elapsed_seconds freezes.
    // -----------------------------------------------------------------------
    gt::TimeKeeper clock;
    constexpr double kDt          = 1.0 / 60.0;
    constexpr int    kFrameCount  = 60;
    constexpr int    kJumpFrame   = 30;
    constexpr int    kPauseFrame  = 45;

    for (int frame = 0; frame < kFrameCount; ++frame)
    {
        // Build a synthetic input snapshot for this frame. Empty by default
        // — only frame `kJumpFrame` presses Space so the callback fires
        // exactly once (the next frame's empty snapshot releases the key,
        // so a subsequent press would be a rising edge again).
        gib::RawInputSnapshot snap;
        if (frame == kJumpFrame)
        {
            snap.press("keyboard", kKeySpace);
        }
        actions.update(snap);

        // Pause partway through to demonstrate gameplay elapsed freezing
        // while frame_index still advances.
        if (frame == kPauseFrame)
        {
            clock.pause();
            std::printf("  [time] frame %d -> pause\n", frame);
        }

        clock.tick(kDt);

        if (frame == 0 || frame == kJumpFrame || frame == kPauseFrame
                       || frame == kFrameCount - 1)
        {
            const gt::GameTime t = clock.get();
            std::printf("  [time] frame=%2d elapsed=%.4fs frame_index=%llu paused=%s\n",
                        frame,
                        t.elapsed_seconds,
                        static_cast<unsigned long long>(t.frame_index),
                        clock.is_paused() ? "yes" : "no");
        }
    }

    if (jump_fire_count != 1)
    {
        std::fprintf(stderr, "Jump callback fired %d times, expected 1\n",
                     jump_fire_count);
        return EXIT_FAILURE;
    }

    // -----------------------------------------------------------------------
    // 4. Save — capture three entity transforms into slot_01, then load
    //    them back and assert byte-for-byte equality. The save root is
    //    sandboxed so we never collide with the user's real save folder.
    // -----------------------------------------------------------------------
    gs::SaveSystem saves(save_root());

    const std::array<EntityTransform, kEntityCount> world {{
        { 1.0F,  2.0F,  3.0F, 0.000F },
        { 4.5F, -1.5F,  0.0F, 1.571F },
        { 0.0F,  0.0F, 10.0F, 3.141F },
    }};

    const auto blob = serialize_world(world);
    auto save_res = saves.save("slot_01",
                               std::span<const std::byte>{ blob.data(), blob.size() },
                               gs::SaveFormat::kBinary,
                               "hello_game roundtrip");
    if (!save_res.has_value())
    {
        std::fprintf(stderr, "Save: write slot_01 FAILED\n");
        return EXIT_FAILURE;
    }
    std::printf("Save: wrote slot_01 (%zu bytes) under %s\n",
                blob.size(),
                save_root().string().c_str());

    auto load_res = saves.load("slot_01");
    if (!load_res.has_value())
    {
        std::fprintf(stderr, "Save: load slot_01 FAILED\n");
        return EXIT_FAILURE;
    }

    std::array<EntityTransform, kEntityCount> restored {};
    if (!deserialize_world(std::span<const std::byte>{ load_res->data(), load_res->size() },
                           restored))
    {
        std::fprintf(stderr, "Save: deserialise slot_01 FAILED "
                             "(unexpected byte count %zu)\n",
                     load_res->size());
        return EXIT_FAILURE;
    }

    if (std::memcmp(world.data(), restored.data(), sizeof(world)) != 0)
    {
        std::fprintf(stderr, "Save: round-trip MISMATCH\n");
        return EXIT_FAILURE;
    }

    std::printf("Save: round-trip OK — entity[1] = (%.2f, %.2f, %.2f) yaw=%.3f\n",
                static_cast<double>(restored[1].position_x),
                static_cast<double>(restored[1].position_y),
                static_cast<double>(restored[1].position_z),
                static_cast<double>(restored[1].yaw_radians));

    std::puts("=== hello_game OK ===");
    return EXIT_SUCCESS;
}
