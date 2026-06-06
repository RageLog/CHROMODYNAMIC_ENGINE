# cd::editor::panel::input_recorder

Editor surface for `cd::game::input_recorder`. Three button strips
(Record / Stop / Replay) plus a progress bar showing record-event
count or replay position.

| Element        | State                                                 |
|----------------|-------------------------------------------------------|
| Record button  | Red when actively recording, grey otherwise.          |
| Stop button    | Orange when any operation in flight, dim grey otherwise. |
| Replay button  | Green when replayer is in flight, grey otherwise.     |
| Progress strip | Red fill = recording event count, green fill = replay. |

## API

```cpp
namespace cd::editor::panel::input_recorder {

class InputRecorderPanel
{
public:
    void                          set_recorder(cd::game::input_recorder::Recorder*);
    void                          set_replayer(cd::game::input_recorder::Replayer*);

    // UI input → state transitions
    void                          on_record_click();
    void                          on_stop_click();
    void                          on_replay_click();

    void                          draw(cd::ui::renderer::DrawBatcher&,
                                       const cd::ui::widgets::Theme&,
                                       const cd::ui::widgets::Rect&) const;
};

}
```

## State transitions

```
                  ┌─ on_record_click ─►  RECORDING ─┐
                  │                                  │
   IDLE ──────────┤                                  │
                  │                                  │
                  └─ on_replay_click ─►  REPLAYING ─┤
                                                    │
   RECORDING / REPLAYING  ── on_stop_click ───►    IDLE
```

The panel **does not own** the recorder / replayer. Pointer
ownership stays with the caller; this surface only drives state
transitions through whichever pointer is currently bound and
queries its `state()` for the button colour state.

## Progress strip

Recording: width-normalised to `recorder.event_count() / max_events`
where `max_events` is read from a per-panel config (default 100k).
Crosses 100% by design — the recorder allows unbounded growth and the
strip simply pins at the right edge.

Replaying: width-normalised to `replayer.position() / replayer.total()`.

## Dependencies

* `cd::core` — `Defines.hpp`.
* `cd::ui_renderer` — `DrawBatcher` (PUBLIC).
* `cd::ui_widgets` — `Theme` + `Rect`.
* `cd::game::input_recorder` — `Recorder` + `Replayer` pointer
  vocabulary (PUBLIC).
