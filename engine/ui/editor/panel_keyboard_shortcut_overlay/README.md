# cd::editor::panel::keyboard_shortcut_overlay

Full-screen semi-transparent overlay that lists every registered
editor shortcut grouped by category. **Source 2 SDK convention**:
toggle visibility with `?`.

**Moment**: a new editor user presses `?`, sees a clean shortcut
cheatsheet overlaid — no manual learning curve, every key revealed
at a glance.

## API

```cpp
namespace cd::editor::panel::keyboard_shortcut_overlay {

struct Shortcut
{
    std::string  keys;                // e.g. "Ctrl+S"
    std::string  action_description;  // e.g. "Save Layout"
    std::string  category;            // e.g. "File"
};

class KeyboardShortcutOverlay
{
public:
    void                          register_shortcut(const Shortcut&);
    [[nodiscard]] std::size_t     shortcut_count() const noexcept;

    void                          set_visible(bool);
    [[nodiscard]] bool            is_visible() const noexcept;

    // Renders into the supplied DrawBatcher. No-op when !is_visible().
    void                          draw(cd::ui::renderer::DrawBatcher&,
                                       const cd::ui::widgets::Theme&,
                                       const cd::ui::widgets::Rect&) const;
};

}
```

Duplicates are accepted on `register_shortcut` — caller is responsible
for de-duping the registry feed (an editor that hot-reloads its
shortcut JSON multiple times in one session benefits from this).

## Layout

When visible, `draw()` ignores the `bounds` rectangle and paints a
full-screen overlay in this order:

1. Semi-transparent dim backdrop covering the entire window.
2. Centred panel background.
3. Accent header bar — *"Keyboard Shortcuts"*.
4. **Columns** — one per unique category, ordered left-to-right by
   first-insertion order. Within each column:
   1. Category header quad (accent_warning colour).
   2. Per-shortcut row:
      * Accent-coloured key-pill quad (left of row).
      * Dimmed label quad (right of key-pill).

## Chord state (Phase 776 / FINALE-8 W1C close-out)

The header also exposes a minimal `ChordStateMachine` so apps that
need 2-key chord shortcuts (`Ctrl+K Ctrl+S` VS-Code-style) can drive
it without a full input library:

```cpp
ChordStateMachine sm;
std::string fired = sm.feed_key(ChordModifier::kCtrl, 'K', now_ms);
// fired is empty until the SECOND key arrives within 500 ms,
// then returns "Ctrl+K Ctrl+S".
```

`feed_key` ignores non-Ctrl keys and any leader key other than `K`
(the only leader the editor currently registers).

`is_chord(spec)` is a free helper: returns true iff the spec string
contains a space (`"Ctrl+K Ctrl+S"` ⇒ true; `"Ctrl+S"` ⇒ false).

## Thread-safety

Read-side (`is_visible`, `shortcut_count`, `draw`) is safe across
threads. Write-side (`register_shortcut`, `set_visible`) is
single-threaded; call from the editor's main thread.

## Dependencies

* `cd::core` — `Defines.hpp`.
* `cd::ui_renderer` — `DrawBatcher` (PUBLIC because `draw()` takes
  one by ref).
* `cd::ui_widgets` — `Theme` + `Rect` vocabulary.
