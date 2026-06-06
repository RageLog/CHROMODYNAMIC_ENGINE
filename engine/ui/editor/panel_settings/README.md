# cd::editor::panel::settings

Settings panel with a **5-category tab strip** (Graphics / Input /
Audio / Editor / Advanced) + a flat key/value entry registry. Users
edit `vsync=on` → `vsync=off`; the engine reads the change the next
frame.

**Moment**: a user opens Settings, switches to the Graphics tab,
toggles vsync off — the engine respects it the next frame, no
restart, no terminal flag.

## API

```cpp
namespace cd::editor::panel::settings {

enum class Category : uint8_t { kGraphics, kInput, kAudio, kEditor, kAdvanced };
enum class EntryKind : uint8_t { kBool, kInt, kFloat, kString };

struct Entry
{
    std::string  key;
    EntryKind    kind;
    std::string  value_str;          // serialised text form for any kind
    std::string  label;              // human-readable display
    Category     category;
};

class SettingsPanel
{
public:
    void                          register_entry(Entry);
    cd::expected<void, Error>     set_entry(std::string_view key,
                                            std::string_view value_str);
    [[nodiscard]] const Entry*    entry(std::string_view key) const;

    void                          set_active_category(Category);
    [[nodiscard]] Category        active_category() const noexcept;

    void                          draw(cd::ui::renderer::DrawBatcher&,
                                       const cd::ui::widgets::Theme&,
                                       const cd::ui::widgets::Rect&) const;
};

}
```

## Layout

```
  ┌─────────────────────────────────────────────────────────────────┐
  │  Graphics │ Input │ Audio │ Editor │ Advanced                   │
  ├─────────────────────────────────────────────────────────────────┤
  │  vsync         [on  / off ]                                      │
  │  msaa          [ 1 / 2 / 4 / 8 ]                                 │
  │  ssao          [on  / off ]                                      │
  │  …                                                              │
  └─────────────────────────────────────────────────────────────────┘
```

The active tab is highlighted with `theme.accent`. Per-entry control
selection (toggle / slider / dropdown / text box) is driven by
`Entry::kind`:

| Kind     | UI control                          |
|----------|-------------------------------------|
| `kBool`  | Toggle pill (`on` / `off`).         |
| `kInt`   | Discrete dropdown (caller supplies allowed values) or slider. |
| `kFloat` | Slider.                             |
| `kString`| Text box.                           |

## Value storage

`value_str` is the **serialised text form** for every kind so the
panel is type-agnostic at the layout layer. Consumers parse it back
on the engine side (`std::from_chars` for ints / floats, "on"/"off"
for bools). This keeps the panel free of per-engine-subsystem typed
unions and means a new entry can land **without** the panel needing
to learn a new variant tag.

## Engine integration

`set_entry("vsync", "off")` is the canonical mutation path; engine
subsystems poll `entry("vsync")` once per frame and respond to
changes. Push-based notification (Observer pattern) is queued for
sprint-2; today's pull-based read is sufficient for the typical
"polled at frame start" pattern.

## Dependencies

* `cd::core` — `Defines.hpp`, `expected`.
* `cd::ui_renderer` — `DrawBatcher` (PUBLIC).
* `cd::ui_widgets` — `Theme` + `Rect`.
