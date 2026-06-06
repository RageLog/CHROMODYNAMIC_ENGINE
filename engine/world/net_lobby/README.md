# cd::network::lobby

In-memory **room-state management** for multiplayer sessions. Once
players are matched into a slot (by `cd::net::matchmaker`), the
lobby owns the room: capacity, passcode, ready-up state, team
assignments, and the start-game gate.

| Library                  | Concern                                     |
|--------------------------|---------------------------------------------|
| `cd::net::matchmaker`    | PAIRING players into a session slot.        |
| `cd::network::lobby`     | MANAGING the room after pairing.            |
| `cd::net::session_replay`| RECORDING the packet stream once playing.   |

## Sprints

| Sprint | Phase | Surface |
|--------|-------|---------|
| 1 | `phase713 / W5A` | In-memory `Lobby` registry + room-state machine. |
| 2 | queued           | WebSocket / reliable-UDP broadcast wired through `cd::net`. |

`cd::net` is **already linked** in the CMake target so a Sprint-2
landing requires no consumer link-line changes; Sprint-1 simply does
not `#include` any `cd::net` header yet.

## Public surface

```cpp
namespace cd::network::lobby {

enum class GameMode : uint8_t { kCoop, kDeathmatch, kCapture, kRanked };

struct LobbyConfig
{
    std::string  name;
    GameMode     mode;
    uint32_t     capacity;
    std::string  passcode;          // empty = public
};

struct PlayerState
{
    uint32_t     id;
    std::string  display_name;
    bool         ready;
    uint8_t      team;
};

struct RoomState
{
    LobbyConfig                config;
    std::vector<PlayerState>   players;
    bool                       started;
    uint64_t                   created_at_ms;
};

class Lobby
{
public:
    cd::expected<uint32_t, Error>  create_room(LobbyConfig);
    cd::expected<void, Error>      join(uint32_t room_id, PlayerState);
    void                           leave(uint32_t room_id, uint32_t player_id);
    void                           set_ready(uint32_t room_id, uint32_t player_id, bool ready);
    cd::expected<void, Error>      start(uint32_t room_id);    // all-ready gate

    [[nodiscard]] const RoomState* room(uint32_t room_id) const;
};

}
```

## Start gate

`start()` succeeds only when:

* The room's `player_count >= 2`.
* Every `PlayerState::ready` is `true`.
* The room has not already started.

The gate is **deliberately strict** — solo "play with bots" sessions
use a different path (`cd::game::ai_director` + an empty lobby) and
should not depend on bypassing the start gate.

## Dependencies

* `cd::core` — `Defines.hpp`.
* `cd::net` — link-only (Sprint-2 wires headers).

## See also

* `cd::net::matchmaker` — produces the `LobbyConfig` + initial
  player list this library consumes.
