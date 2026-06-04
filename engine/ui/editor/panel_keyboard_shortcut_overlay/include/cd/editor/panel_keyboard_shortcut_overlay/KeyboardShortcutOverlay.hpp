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
#include <string>
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

}  // namespace cd::editor::panel::keyboard_shortcut_overlay
