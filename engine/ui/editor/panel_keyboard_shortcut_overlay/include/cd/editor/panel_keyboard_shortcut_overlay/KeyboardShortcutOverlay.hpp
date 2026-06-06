// =============================================================================
// CHROMODYNAMIC — cd/editor/panel_keyboard_shortcut_overlay/KeyboardShortcutOverlay.hpp
//
// phase688 — cd::editor::panel::keyboard_shortcut_overlay
//            (panel_keyboard_shortcut_overlay library)
//
// Full-screen shortcut cheatsheet overlay for the editor.
//
// When visible, draw() ignores `bounds` and paints a full-screen semi-
// transparent backdrop with registered shortcuts grouped by category.
// Categories are laid out horizontally; within each column shortcuts are
// listed vertically as coloured key-pill + label rows.
//
// Source 2 / Hammer convention: toggle visibility with the '?' key.
//
// Shortcut struct
// ───────────────
//   keys               — printable key combination, e.g. "Ctrl+S"
//   action_description — human-readable action, e.g. "Save Layout"
//   category           — grouping header,          e.g. "File"
//
// Class API
// ─────────
//   register_shortcut(const Shortcut&) — add a shortcut (duplicates accepted)
//   set_visible(bool)                  — show / hide the overlay
//   is_visible() const                 — query current visibility
//   shortcut_count() const             — total registered shortcuts
//   draw(DrawBatcher&, Theme&, Rect&)  — emit draw commands; no-op when hidden
//
// Lifetime contract
// ─────────────────
//   KeyboardShortcutOverlay is default-constructible and owns its shortcut
//   storage. Thread-safe for read, single-thread for write.
//
// MOMENT: A new editor user presses '?', sees a clean shortcut cheatsheet
// overlaid — no manual learning curve, every key revealed at a glance.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <cd/ui/renderer/DrawBatcher.hpp>
#include <cd/ui/widgets/Widgets.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace cd::editor::panel::keyboard_shortcut_overlay
{

// ---------------------------------------------------------------------------
// Shortcut — one registered key binding entry.
// ---------------------------------------------------------------------------
struct Shortcut
{
    std::string keys;               ///< e.g. "Ctrl+S"
    std::string action_description; ///< e.g. "Save Layout"
    std::string category;           ///< e.g. "File"
};

// ---------------------------------------------------------------------------
// KeyboardShortcutOverlay
// ---------------------------------------------------------------------------
class KeyboardShortcutOverlay
{
public:
    /// Default-constructible. Starts hidden; no shortcuts registered.
    KeyboardShortcutOverlay() noexcept = default;

    // ---- Registration API ---------------------------------------------------

    /// Add a shortcut. Duplicates are accepted (caller responsibility).
    void register_shortcut(const Shortcut& shortcut);

    /// Returns the total number of registered shortcuts.
    [[nodiscard]] std::size_t shortcut_count() const noexcept;

    // ---- Visibility API -----------------------------------------------------

    /// Show or hide the overlay. Source 2 / Hammer convention: bound to '?'.
    void set_visible(bool visible) noexcept;

    /// Returns true when the overlay will draw on the next draw() call.
    [[nodiscard]] bool is_visible() const noexcept;

    // ---- DrawBatcher path (DockSpace / apps/editor) -------------------------

    /// Emit draw commands into `batcher`.
    ///
    /// When visible:
    ///   1. Full-screen semi-transparent dim backdrop (ignores `bounds`).
    ///   2. Centred panel background.
    ///   3. Accent header bar ("Keyboard Shortcuts").
    ///   4. Columns — one per unique category, left→right.
    ///      Each column:
    ///        a. Category header quad (accent_warning colour).
    ///        b. One row per shortcut in that category:
    ///             * accent-coloured key-pill quad (left of row).
    ///             * dimmed label quad  (right of key-pill).
    ///
    /// No-op when !is_visible().
    ///
    /// Thread-safety: call from the render thread only.
    void draw(cd::ui::renderer::DrawBatcher&  batcher,
              const cd::ui::widgets::Theme&   theme,
              const cd::ui::widgets::Rect&    bounds) const;

private:
    std::vector<Shortcut> shortcuts_; ///< All registered shortcuts (insertion order).
    bool                  visible_ { false };
};

// =============================================================================
// Phase 776 (FINALE-8 W1C — close-out stub) — Chord state machine
// Minimal so apps/editor compiles; full Sprint-2 deferred.
// =============================================================================

enum class ChordModifier : std::uint8_t
{
    kNone  = 0U,
    kCtrl  = 1U,
    kShift = 2U,
    kAlt   = 4U,
};

class ChordStateMachine
{
public:
    ChordStateMachine() noexcept = default;

    // Convenience overload: take ChordModifier enum directly (preferred at call sites).
    [[nodiscard]] std::string feed_key(ChordModifier mod, char key_char, std::int64_t now_ms) noexcept
    {
        return feed_key(static_cast<std::uint8_t>(mod), key_char, now_ms);
    }

    [[nodiscard]] bool chord_pending() const noexcept { return waiting_second_; }

    [[nodiscard]] std::string feed_key(std::uint8_t mods, char key_char, std::int64_t now_ms) noexcept
    {
        const bool ctrl = (mods & static_cast<std::uint8_t>(ChordModifier::kCtrl)) != 0U;
        if (!ctrl || key_char == '\0') { reset(); return {}; }
        if (waiting_second_)
        {
            // Second key arrived: either complete the chord (if within 500ms window)
            // or cancel the chord and stay idle (return empty).
            if (now_ms - first_ms_ > 500) { reset(); return {}; }
            std::string chord; chord.reserve(20);
            chord += "Ctrl+"; chord += first_key_;
            chord += " Ctrl+"; chord += key_char;
            reset();
            return chord;
        }
        // Only the leader key 'K' starts a chord; other Ctrl+key combinations
        // pass through transparently (not chord-shorthand-eligible).
        if (key_char != 'K') { return {}; }
        first_key_ = key_char; first_ms_ = now_ms; waiting_second_ = true;
        return {};
    }

    void tick(std::int64_t now_ms) noexcept
    {
        if (waiting_second_ && (now_ms - first_ms_ > 500)) { reset(); }
    }

    void reset() noexcept { waiting_second_ = false; first_key_ = '\0'; first_ms_ = 0; }

private:
    bool         waiting_second_ { false };
    char         first_key_      { '\0' };
    std::int64_t first_ms_       { 0 };
};

// Free helper — true if the shortcut spec is a 2-key chord
// (e.g. "Ctrl+K Ctrl+S"); false for single-key shortcuts (e.g. "Ctrl+S").
[[nodiscard]] inline bool is_chord(std::string_view spec) noexcept
{
    return spec.find(' ') != std::string_view::npos;
}

}  // namespace cd::editor::panel::keyboard_shortcut_overlay
