# cd::editor::panel::cutscene_player

Editor surface for `cd::game::cutscene_player`. **Per-phase block
layout + event-marker dots + global playhead scrubber**, plus Play /
Pause / Stop / Skip / Save / Load buttons.

Sprint-2 adds pan / zoom: a scrollable horizontal timeline with mouse-
wheel zoom + middle-or-shift-drag pan + a bottom scrollbar showing
the viewport within the zoomed timeline.

## Sprints

| Sprint | Phase | Surface |
|--------|-------|---------|
| 1 | `phase617`     | Per-phase blocks + event dots + scrubber + transport buttons. |
| 2a| `phase703`     | Save / Load JSON round-trip wired through `cutscene_player::save_to_json` / `load_from_json`. |
| 2b| `phase741`     | Pan / zoom (`tick_input`, `zoom_factor`, `pan_offset_x`, scrollbar). |

## API

```cpp
namespace cd::editor::panel::cutscene_player {

class CutscenePlayerPanel
{
public:
    void                          set_cutscene(cd::game::cutscene_player::Cutscene);
    void                          set_player(cd::game::cutscene_player::CutscenePlayer*);

    // Sprint-2b pan + zoom input pump (called per frame from the
    // host's mouse / wheel pipeline).
    void                          tick_input(cd::math::Vec2f pointer,
                                             bool            middle_down,
                                             float           wheel_delta,
                                             bool            shift_held,
                                             cd::ui::widgets::Rect bounds);

    // Sprint-1 transport
    void                          on_play();
    void                          on_pause();
    void                          on_stop();
    void                          on_skip();

    // Sprint-2a round-trip
    cd::expected<void, Error>     save_to_json(const std::filesystem::path&) const;
    cd::expected<void, Error>     load_from_json(const std::filesystem::path&);

    void                          draw(cd::ui::renderer::DrawBatcher&,
                                       const cd::ui::widgets::Theme&,
                                       const cd::ui::widgets::Rect&) const;
};

}
```

## Layout

```
  ┌────────────────────────────────────────────────────────────┐
  │  ▶ ❚❚ ■ ▶▶  | 💾 📂                                         │   ← transport
  ├────────────────────────────────────────────────────────────┤
  │ phase 0        │ phase 1                  │ phase 2         │
  │ [────●────────│────────●─────────●──────────│●─────────────] │   ← phase blocks
  │                          ▲                                  │
  │                          playhead                           │
  ├────────────────────────────────────────────────────────────┤
  │ ◀──────────────zoom + pan viewport────────────────────────▶ │   ← scrollbar
  └────────────────────────────────────────────────────────────┘
```

The per-phase block colour is sampled deterministically from
`phase.id` so adjacent phases stay distinguishable. Event-marker dots
are drawn at the offset = phase block left + `offset_seconds * px_per_second`.

## Pan / zoom

`tick_input` semantics (`bounds` is the timeline strip rect):

* `wheel_delta` non-zero → multiply `zoom_factor` by `1.1^wheel_delta`,
  clamp `zoom_factor ∈ [0.25, 16.0]`.
* `middle_down` or `shift_held + left-down` → accumulate
  `pan_offset_x += pointer.x − last_pointer.x`.
* Scrollbar drag drives `pan_offset_x` directly.

## Dependencies

* `cd::core` — `Defines.hpp`.
* `cd::game_cutscene_player` — `Cutscene`, `CutscenePlayer` (PUBLIC).
* `cd::ui_renderer` — `DrawBatcher` (PUBLIC).
* `cd::ui_widgets` — `Theme` + `Rect`.
