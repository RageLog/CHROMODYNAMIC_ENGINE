# cd::editor::panel::auto_save_indicator

Status-bar widget surfacing a **5-state auto-save indicator**:

| Status        | Meaning                          | Colour                  |
|---------------|----------------------------------|-------------------------|
| `kIdle`       | No unsaved changes.              | `theme.text_dim`        |
| `kPendingDirty`| Unsaved changes; auto-save pending. | `accent_warning` amber |
| `kSaving`     | Auto-save in flight.             | `theme.accent` cyan     |
| `kJustSaved`  | Saved within the last few seconds.| `accent_success` green  |
| `kError`      | Save failed.                     | `accent_error` red      |

**Moment**: a user edits the scene → an *Unsaved changes* badge
(amber) appears immediately; auto-save fires → *Saving…* → *Saved
just now* (green). Trust signal that work isn't lost.

## API

```cpp
namespace cd::editor::panel::auto_save_indicator {

enum class Status : uint8_t { kIdle, kPendingDirty, kSaving, kJustSaved, kError };

class AutoSaveIndicator
{
public:
    void                          set_status(Status);
    void                          set_label(std::string);          // optional override
    [[nodiscard]] Status          status() const noexcept;

    void                          draw(cd::ui::renderer::DrawBatcher&,
                                       const cd::ui::widgets::Theme&,
                                       const cd::ui::widgets::Rect&) const;
};

}
```

## Layout

```
                                                          ┌──────────────────┐
  status-bar content                                       │ ● Saved just now │
                                                          └──────────────────┘
                                                            ▲ colour dot
```

The widget fits in a ~50 px-tall strip. The status dot picks its
colour from the `Status` enum; the label text falls back to a sensible
default per state when `set_label("")` is the most recent call.

## Default labels

| Status         | Default label         |
|----------------|-----------------------|
| `kIdle`        | "No changes"          |
| `kPendingDirty`| "Unsaved changes"     |
| `kSaving`      | "Saving…"             |
| `kJustSaved`   | "Saved just now"      |
| `kError`       | "Save failed"         |

`set_label` lets the caller override (for example to surface the file
name on save errors). When the caller's label is empty the default
re-applies.

## Lifecycle

The editor owns the auto-save scheduler (`cd::game::save` + a debounce
timer). On dirty, it calls `set_status(kPendingDirty)`. On save start
`set_status(kSaving)`. On success `set_status(kJustSaved)` plus a
caller-side timer that resets to `kIdle` after 3 s. On failure
`set_status(kError)` + `set_label(reason)`.

## Dependencies

* `cd::core` — `Defines.hpp`.
* `cd::ui_renderer` — `DrawBatcher` + `Color` (PUBLIC).
* `cd::ui_widgets` — `Theme` + `Rect`.
