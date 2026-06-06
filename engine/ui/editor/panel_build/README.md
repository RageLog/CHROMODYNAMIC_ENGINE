# cd::editor::panel::build

Build panel. Top half: a colour-coded **Status badge**
(`idle` / `compiling` / `success` / `failed`). Bottom half: a
scrollable **BuildEvent log** with timestamp + message, colour-coded
by severity. Clicking an event with a `source_file` emits
`selected_event_index` — Sprint-2 will hook this to "open file at
line" in the host code editor.

**Moment**: a dev presses Build, sees compile progress live in the
panel — green check when done, red errors with file + line on
failure. No external terminal needed.

## API

```cpp
namespace cd::editor::panel::build {

enum class Status   : uint8_t { kIdle, kCompiling, kSuccess, kFailed };
enum class Severity : uint8_t { kInfo, kWarning, kError };

struct BuildEvent
{
    uint64_t      t_ns;
    Severity      severity;
    std::string   message;
    std::string   source_file;    // optional; empty = no file link
    uint32_t      line;           // 0 = no line info
};

class BuildPanel
{
public:
    void                          set_status(Status);
    void                          push_event(BuildEvent);
    void                          clear_events();

    [[nodiscard]] Status          status() const noexcept;
    [[nodiscard]] std::optional<std::size_t>
                                  selected_event_index() const noexcept;

    void                          draw(cd::ui::renderer::DrawBatcher&,
                                       const cd::ui::widgets::Theme&,
                                       const cd::ui::widgets::Rect&) const;
};

}
```

## Layout

```
  ┌─────────────────────────────────────────────────────────────┐
  │  ▌▌▌ COMPILING                                              │ ◄ status badge
  ├─────────────────────────────────────────────────────────────┤
  │ 12:01:03  ℹ  [link] cd_test_runtime                          │
  │ 12:01:04  ℹ  [link] cd_test_restir_math                      │
  │ 12:01:04  ⚠  unused variable 'tmp' (Foo.cpp:42)              │ ◄ source_file
  │ 12:01:05  ✗  expected ')' (Bar.cpp:88)                       │ ◄ click
  │ 12:01:06  ℹ  [build] 187/254 …                               │
  └─────────────────────────────────────────────────────────────┘
```

Status badge colour ↔ enum:

| Status        | Colour                |
|---------------|-----------------------|
| `kIdle`       | `theme.text_dim`      |
| `kCompiling`  | `theme.accent` (cyan) |
| `kSuccess`    | `accent_success`      |
| `kFailed`     | `accent_error`        |

Severity colour ↔ row:

| Severity      | Colour              |
|---------------|---------------------|
| `kInfo`       | `theme.text`        |
| `kWarning`    | `accent_warning`    |
| `kError`      | `accent_error`      |

## Selection

`selected_event_index()` is set when the user clicks on a row whose
`source_file` is non-empty. Sprint-2 wires this to the host editor's
"open file at line" command; today's path is `std::optional` for the
caller to inspect.

## Event throughput

The panel keeps an internal ring of 512 events (oldest dropped when
full). A typical CHROMODYNAMIC clean build emits ~400 lines of
status; the ring covers a full build cycle without truncation. A
future "show all" follow-on uses a larger backing store + virtualised
list — queued under `L-build-panel-history`.

## Dependencies

* `cd::core` — `Defines.hpp`.
* `cd::ui_renderer` — `DrawBatcher` (PUBLIC).
* `cd::ui_widgets` — `Theme` + `Rect`.
