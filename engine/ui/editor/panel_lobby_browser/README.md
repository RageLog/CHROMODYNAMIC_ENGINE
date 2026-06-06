# cd::editor::panel::lobby_browser

Visualises `cd::network::lobby::Lobby`'s active room list. One row per
`RoomState` — room name + game mode + player-count bar + lock icon
(if the room has a passcode).

**Moment**: a multiplayer dev sees rooms appear / disappear in
real-time inside the editor — no external network monitor needed.

## API

```cpp
namespace cd::editor::panel::lobby_browser {

using RoomId = uint32_t;

class LobbyBrowser
{
public:
    void                          set_lobby(const cd::network::lobby::Lobby*);
    void                          select(RoomId);
    [[nodiscard]] std::optional<RoomId>
                                  selected_room_id() const noexcept;

    void                          draw(cd::ui::renderer::DrawBatcher&,
                                       const cd::ui::widgets::Theme&,
                                       const cd::ui::widgets::Rect&) const;
};

}
```

## Layout

```
  ┌─────────────────────────────────────────────────────────────┐
  │ Active Rooms (4)                                            │
  ├─────────────────────────────────────────────────────────────┤
  │ 🔒 Friday Casuals   • coop      [████████░░░░]  6 / 8       │
  │    Open Skirmish    • dm        [██████░░░░░░]  3 / 6       │ ◄ selected
  │ 🔒 Comp Ladder      • ranked    [████████████]  4 / 4       │
  │    Free For All     • dm        [██████████░░] 10 / 12      │
  └─────────────────────────────────────────────────────────────┘
```

Per-row elements:

* **Lock icon** when `config.passcode` is non-empty.
* **Room name** in `theme.text`.
* **Game mode** as a small accent-coloured pill.
* **Player-count bar** normalised to capacity; full ⇒ `accent_warning`.
* **`current / capacity`** numeric on the right.

## Lifecycle

`set_lobby(lobby_ptr)` accepts a pointer to a live `Lobby` instance.
The browser re-walks `lobby->rooms()` on every `draw()` — a
deliberate "no internal cache" choice that keeps the browser correct
under hot-reload and fast room churn. With the typical < 50 rooms
case the per-draw walk is sub-millisecond.

`select(room_id)` records the selection for the editor's gizmo /
property inspector to scope itself. Selection persists across
`draw()` calls; rooms that disappear from the lobby's open set clear
the selection automatically.

## Dependencies

* `cd::core` — `Defines.hpp`.
* `cd::network_lobby` — `Lobby`, `RoomState`, `LobbyConfig`,
  `GameMode` (PUBLIC).
* `cd::ui_renderer` — `DrawBatcher` (PUBLIC).
* `cd::ui_widgets` — `Theme` + `Rect`.
