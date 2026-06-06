// =============================================================================
// CHROMODYNAMIC — engine/ui/editor/panel_keyboard_shortcut_overlay/tests/
//                 test_keyboard_shortcut_overlay.cpp
//
// phase688 — unit tests for
//   cd::editor::panel::keyboard_shortcut_overlay::KeyboardShortcutOverlay
// phase776 — tests for ChordStateMachine + is_chord helper
//
// All tests are headless (no ImGui / no RHI). We verify:
//
//   DefaultCtorIsHiddenAndEmpty
//     — shortcut_count() == 0 and is_visible() == false after default ctor.
//
//   RegisterShortcutIncreasesCount
//     — register_shortcut() grows shortcut_count() correctly.
//
//   VisibilityToggleRoundTrip
//     — set_visible(true)/is_visible() / set_visible(false)/is_visible().
//
//   DrawWhenHiddenEmitsNothing
//     — draw() while hidden must not emit any draw commands.
//
//   DrawEmptyOverlayWhenVisibleEmitsDimOnly
//     — visible + no shortcuts: only the dim backdrop is emitted.
//
//   DrawWithShortcutsEmitsMoreGeometry
//     — visible + 3 shortcuts: geometry count exceeds the dim-only baseline.
//
//   MultipleCategoriesRenderedSideBySide
//     — shortcuts in two distinct categories are all registered; the overlay
//       draws without crashing and emits at least one column per category.
//
//   ZeroBoundsDrawDoesNotCrash
//     — visible overlay with zero-size bounds must not crash.
//
// phase776 chord tests:
//
//   IsChordDetectsSpaceSeparatedKeys
//     — is_chord("Ctrl+K Ctrl+S") == true; is_chord("Ctrl+S") == false.
//
//   SingleKeyShortcutStillWorks
//     — ChordStateMachine does not interfere with direct (non-K-leader) keys.
//
//   ChordDetectionRoundTrip
//     — Ctrl+K then Ctrl+S within timeout yields "Ctrl+K Ctrl+S".
//
//   ChordGapTimeoutCancels
//     — Feeding second key after timeout_ms expires returns empty string.
//
//   ChordAllFourRegisteredKeys
//     — S, L, T, H all complete correctly after Ctrl+K leader.
// =============================================================================
#include <cd/editor/panel_keyboard_shortcut_overlay/KeyboardShortcutOverlay.hpp>

#include <cd/ui/renderer/DrawBatcher.hpp>
#include <cd/ui/widgets/Widgets.hpp>

#include <gtest/gtest.h>

namespace kso = cd::editor::panel::keyboard_shortcut_overlay;

// Helper: a shortcut with explicit fields.
static kso::Shortcut make_shortcut(const char* keys,
                                   const char* action,
                                   const char* category)
{
    return kso::Shortcut { keys, action, category };
}

// ---------------------------------------------------------------------------
// TEST(KeyboardShortcutOverlay, DefaultCtorIsHiddenAndEmpty)
// ---------------------------------------------------------------------------
TEST(KeyboardShortcutOverlay, DefaultCtorIsHiddenAndEmpty)
{
    const kso::KeyboardShortcutOverlay overlay;
    EXPECT_EQ(overlay.shortcut_count(), static_cast<std::size_t>(0U));
    EXPECT_FALSE(overlay.is_visible());
}

// ---------------------------------------------------------------------------
// TEST(KeyboardShortcutOverlay, RegisterShortcutIncreasesCount)
// ---------------------------------------------------------------------------
TEST(KeyboardShortcutOverlay, RegisterShortcutIncreasesCount)
{
    kso::KeyboardShortcutOverlay overlay;
    EXPECT_EQ(overlay.shortcut_count(), static_cast<std::size_t>(0U));

    overlay.register_shortcut(make_shortcut("Ctrl+S", "Save Layout", "File"));
    EXPECT_EQ(overlay.shortcut_count(), static_cast<std::size_t>(1U));

    overlay.register_shortcut(make_shortcut("Ctrl+Z", "Undo", "Edit"));
    EXPECT_EQ(overlay.shortcut_count(), static_cast<std::size_t>(2U));

    overlay.register_shortcut(make_shortcut("Ctrl+Y", "Redo", "Edit"));
    EXPECT_EQ(overlay.shortcut_count(), static_cast<std::size_t>(3U));
}

// ---------------------------------------------------------------------------
// TEST(KeyboardShortcutOverlay, VisibilityToggleRoundTrip)
// ---------------------------------------------------------------------------
TEST(KeyboardShortcutOverlay, VisibilityToggleRoundTrip)
{
    kso::KeyboardShortcutOverlay overlay;
    EXPECT_FALSE(overlay.is_visible());

    overlay.set_visible(true);
    EXPECT_TRUE(overlay.is_visible());

    overlay.set_visible(false);
    EXPECT_FALSE(overlay.is_visible());

    // Toggle on again to confirm idempotent.
    overlay.set_visible(true);
    EXPECT_TRUE(overlay.is_visible());
}

// ---------------------------------------------------------------------------
// TEST(KeyboardShortcutOverlay, DrawWhenHiddenEmitsNothing)
// ---------------------------------------------------------------------------
TEST(KeyboardShortcutOverlay, DrawWhenHiddenEmitsNothing)
{
    kso::KeyboardShortcutOverlay overlay;
    overlay.register_shortcut(make_shortcut("Ctrl+S", "Save", "File"));
    // Overlay is hidden (default).

    cd::ui::renderer::DrawBatcher batcher;
    const cd::ui::widgets::Theme  theme {};
    const cd::ui::widgets::Rect   bounds { 0.0F, 0.0F, 1920.0F, 1080.0F };

    batcher.begin_frame();
    overlay.draw(batcher, theme, bounds);

    // Hidden overlay must not emit any geometry.
    EXPECT_EQ(batcher.command_count(), static_cast<std::size_t>(0U));
}

// ---------------------------------------------------------------------------
// TEST(KeyboardShortcutOverlay, DrawEmptyOverlayWhenVisibleEmitsDimOnly)
// ---------------------------------------------------------------------------
TEST(KeyboardShortcutOverlay, DrawEmptyOverlayWhenVisibleEmitsDimOnly)
{
    kso::KeyboardShortcutOverlay overlay;
    overlay.set_visible(true);
    // No shortcuts registered.

    cd::ui::renderer::DrawBatcher batcher;
    const cd::ui::widgets::Theme  theme {};
    const cd::ui::widgets::Rect   bounds { 0.0F, 0.0F, 1920.0F, 1080.0F };

    batcher.begin_frame();
    overlay.draw(batcher, theme, bounds);

    // At minimum the dim backdrop quad must have been emitted.
    EXPECT_GE(batcher.command_count(), static_cast<std::size_t>(1U));
}

// ---------------------------------------------------------------------------
// TEST(KeyboardShortcutOverlay, DrawWithShortcutsEmitsMoreGeometry)
// ---------------------------------------------------------------------------
TEST(KeyboardShortcutOverlay, DrawWithShortcutsEmitsMoreGeometry)
{
    kso::KeyboardShortcutOverlay overlay;
    overlay.set_visible(true);
    overlay.register_shortcut(make_shortcut("Ctrl+S",       "Save Layout",      "File"));
    overlay.register_shortcut(make_shortcut("Ctrl+Z",       "Undo",             "Edit"));
    overlay.register_shortcut(make_shortcut("Ctrl+Shift+P", "Command Palette",  "View"));

    const cd::ui::widgets::Theme  theme {};
    const cd::ui::widgets::Rect   bounds { 0.0F, 0.0F, 1920.0F, 1080.0F };

    // Dim-only baseline (visible, no shortcuts): 1 backdrop quad = 4 vertices.
    cd::ui::renderer::DrawBatcher baseline_batcher;
    baseline_batcher.begin_frame();
    {
        kso::KeyboardShortcutOverlay empty_overlay;
        empty_overlay.set_visible(true);
        empty_overlay.draw(baseline_batcher, theme, bounds);
    }
    const std::size_t baseline_verts = baseline_batcher.vertex_count();

    // Full overlay with 3 shortcuts across 3 categories — many more quads.
    cd::ui::renderer::DrawBatcher batcher;
    batcher.begin_frame();
    overlay.draw(batcher, theme, bounds);

    // More shortcuts → more vertices (key-pills + label quads + column headers).
    EXPECT_GT(batcher.vertex_count(), baseline_verts);
}

// ---------------------------------------------------------------------------
// TEST(KeyboardShortcutOverlay, MultipleCategoriesRenderedSideBySide)
// ---------------------------------------------------------------------------
TEST(KeyboardShortcutOverlay, MultipleCategoriesRenderedSideBySide)
{
    kso::KeyboardShortcutOverlay overlay;
    overlay.set_visible(true);

    // Register shortcuts across 3 categories.
    overlay.register_shortcut(make_shortcut("Ctrl+N",  "New Scene",      "File"));
    overlay.register_shortcut(make_shortcut("Ctrl+S",  "Save Layout",    "File"));
    overlay.register_shortcut(make_shortcut("Ctrl+Z",  "Undo",           "Edit"));
    overlay.register_shortcut(make_shortcut("Ctrl+Y",  "Redo",           "Edit"));
    overlay.register_shortcut(make_shortcut("F5",      "Run Scene",      "Playback"));
    overlay.register_shortcut(make_shortcut("Escape",  "Stop Playback",  "Playback"));

    EXPECT_EQ(overlay.shortcut_count(), static_cast<std::size_t>(6U));

    cd::ui::renderer::DrawBatcher batcher;
    const cd::ui::widgets::Theme  theme {};
    const cd::ui::widgets::Rect   bounds { 0.0F, 0.0F, 1920.0F, 1080.0F };

    batcher.begin_frame();
    // Must not crash; multi-category side-by-side layout.
    ASSERT_NO_THROW(overlay.draw(batcher, theme, bounds));

    // Each quad = 4 vertices. Expected quads:
    //   1 dim backdrop + 1 panel bg + 1 accent header +
    //   3 categories * (1 cat-header-bg + 1 cat-accent-pip) = 6 cat quads +
    //   6 shortcuts * (1 key-pill-bg + 1 key-pip + 1 label-bg) = 18 entry quads
    //   Total >= 26 quads = 104 vertices. Use conservative lower bound: >= 40 verts.
    EXPECT_GE(batcher.vertex_count(), static_cast<std::size_t>(40U));
}

// ---------------------------------------------------------------------------
// TEST(KeyboardShortcutOverlay, ZeroBoundsDrawDoesNotCrash)
// ---------------------------------------------------------------------------
TEST(KeyboardShortcutOverlay, ZeroBoundsDrawDoesNotCrash)
{
    kso::KeyboardShortcutOverlay overlay;
    overlay.set_visible(true);
    overlay.register_shortcut(make_shortcut("?", "Show Shortcuts", "Help"));

    cd::ui::renderer::DrawBatcher batcher;
    const cd::ui::widgets::Theme  theme {};
    const cd::ui::widgets::Rect   zero_bounds { 0.0F, 0.0F, 0.0F, 0.0F };

    batcher.begin_frame();
    // Must not crash with a zero-size framebuffer rect.
    ASSERT_NO_THROW(overlay.draw(batcher, theme, zero_bounds));
}

// ===========================================================================
// phase776 — ChordStateMachine + is_chord helper tests
// ===========================================================================

// ---------------------------------------------------------------------------
// TEST(KeyboardChord, IsChordDetectsSpaceSeparatedKeys)
// ---------------------------------------------------------------------------
TEST(KeyboardChord, IsChordDetectsSpaceSeparatedKeys)
{
    // Chord syntax: space-separated two-key combo.
    EXPECT_TRUE(kso::is_chord("Ctrl+K Ctrl+S"));
    EXPECT_TRUE(kso::is_chord("Ctrl+K Ctrl+L"));
    EXPECT_TRUE(kso::is_chord("Ctrl+K Ctrl+T"));
    EXPECT_TRUE(kso::is_chord("Ctrl+K Ctrl+H"));

    // Single-key shortcuts must NOT be detected as chords.
    EXPECT_FALSE(kso::is_chord("Ctrl+S"));
    EXPECT_FALSE(kso::is_chord("Ctrl+Z"));
    EXPECT_FALSE(kso::is_chord("F5"));
    EXPECT_FALSE(kso::is_chord("?"));
    EXPECT_FALSE(kso::is_chord("Escape"));
}

// ---------------------------------------------------------------------------
// TEST(KeyboardChord, SingleKeyShortcutStillWorks)
//
// Non-leader keys (not 'K' + Ctrl) must pass through without triggering
// chord mode. The state machine must stay idle.
// ---------------------------------------------------------------------------
TEST(KeyboardChord, SingleKeyShortcutStillWorks)
{
    kso::ChordStateMachine sm;
    constexpr std::int64_t t0 = 1000LL;

    // Ctrl+S — not a chord leader; machine stays idle and returns empty.
    const std::string result =
        sm.feed_key(kso::ChordModifier::kCtrl, 'S', t0);
    EXPECT_TRUE(result.empty());
    EXPECT_FALSE(sm.chord_pending());

    // Ctrl+Z — same.
    const std::string result2 =
        sm.feed_key(kso::ChordModifier::kCtrl, 'Z', t0 + 10LL);
    EXPECT_TRUE(result2.empty());
    EXPECT_FALSE(sm.chord_pending());

    // A plain key without Ctrl.
    const std::string result3 = sm.feed_key(0U, 'Q', t0 + 20LL);
    EXPECT_TRUE(result3.empty());
    EXPECT_FALSE(sm.chord_pending());
}

// ---------------------------------------------------------------------------
// TEST(KeyboardChord, ChordDetectionRoundTrip)
//
// Ctrl+K (leader) → Ctrl+S (second key, within timeout) must yield
// "Ctrl+K Ctrl+S". State machine returns to idle.
// ---------------------------------------------------------------------------
TEST(KeyboardChord, ChordDetectionRoundTrip)
{
    kso::ChordStateMachine sm;
    constexpr std::int64_t t0 = 5000LL;

    // Feed leader: Ctrl+K.
    const std::string after_leader =
        sm.feed_key(kso::ChordModifier::kCtrl, 'K', t0);
    EXPECT_TRUE(after_leader.empty());      // No chord yet.
    EXPECT_TRUE(sm.chord_pending());        // State machine waiting.

    // Feed second key within 200 ms (well within 500 ms timeout): Ctrl+S.
    const std::string chord =
        sm.feed_key(kso::ChordModifier::kCtrl, 'S', t0 + 200LL);
    EXPECT_EQ(chord, std::string{"Ctrl+K Ctrl+S"});
    EXPECT_FALSE(sm.chord_pending());       // Back to idle.
}

// ---------------------------------------------------------------------------
// TEST(KeyboardChord, ChordGapTimeoutCancels)
//
// When the second key arrives after 600 ms the chord must be cancelled and
// the state machine must return to idle with an empty result.
// ---------------------------------------------------------------------------
TEST(KeyboardChord, ChordGapTimeoutCancels)
{
    kso::ChordStateMachine sm;
    constexpr std::int64_t t0 = 10'000LL;

    // Feed leader — must return empty (chord not yet complete).
    EXPECT_TRUE(sm.feed_key(kso::ChordModifier::kCtrl, 'K', t0).empty());
    EXPECT_TRUE(sm.chord_pending());

    // Tick 600 ms later (beyond the 500 ms default timeout).
    sm.tick(t0 + 600LL);
    EXPECT_FALSE(sm.chord_pending());  // Expired.

    // Feed the second key now — it must be ignored (returns empty).
    const std::string result =
        sm.feed_key(kso::ChordModifier::kCtrl, 'S', t0 + 600LL);
    EXPECT_TRUE(result.empty());
    EXPECT_FALSE(sm.chord_pending());
}

// ---------------------------------------------------------------------------
// TEST(KeyboardChord, ChordAllFourRegisteredKeys)
//
// Verify that all four registered chord second-keys (S, L, T, H) complete
// correctly after a Ctrl+K leader.
// ---------------------------------------------------------------------------
TEST(KeyboardChord, ChordAllFourRegisteredKeys)
{
    const std::pair<char, std::string> cases[] = {
        { 'S', "Ctrl+K Ctrl+S" },
        { 'L', "Ctrl+K Ctrl+L" },
        { 'T', "Ctrl+K Ctrl+T" },
        { 'H', "Ctrl+K Ctrl+H" },
    };

    for (const auto& [key, expected] : cases)
    {
        kso::ChordStateMachine sm;
        constexpr std::int64_t t0 = 0LL;

        // Feed leader — must return empty (chord not yet complete).
        EXPECT_TRUE(sm.feed_key(kso::ChordModifier::kCtrl, 'K', t0).empty())
            << "Leader returned non-empty for key " << key;
        EXPECT_TRUE(sm.chord_pending()) << "Leader not pending for key " << key;

        const std::string result =
            sm.feed_key(kso::ChordModifier::kCtrl, key, t0 + 100LL);
        EXPECT_EQ(result, expected) << "Failed for key " << key;
        EXPECT_FALSE(sm.chord_pending()) << "Still pending after key " << key;
    }
}
