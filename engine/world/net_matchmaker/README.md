# cd::net::matchmaker

In-memory **matchmaking data structures + skill / region matching**.
Pairs incoming players into session slots based on their skill rating
and region tag; the paired lobby then hands off to `cd::network::lobby`
for the room lifecycle.

## Sprints

| Sprint | Phase | Surface |
|--------|-------|---------|
| 1 | `phase563 / W5B` | In-memory `LobbyRegistry` + `SkillBasedFinder`. |
| 2 | queued           | Real session-service backend via `cd::net`. |

`cd::net` is **already linked** in CMake so a Sprint-2 landing
requires no consumer link-line changes; no `cd::net` header is
included by Sprint-1 today.

## Public surface

```cpp
namespace cd::net::matchmaker {

struct PlayerProfile
{
    uint32_t     id;
    float        skill_rating;       // ELO-style (e.g. 1500 baseline)
    std::string  region;             // ISO-3166-2 ("us-west-2", "eu-central-1")
};

struct Lobby
{
    uint32_t                 lobby_id;
    std::vector<uint32_t>    player_ids;
    cd::network::lobby::GameMode  game_mode;
};

class LobbyRegistry
{
public:
    void                          register_profile(PlayerProfile);
    uint32_t                      create_lobby(cd::network::lobby::GameMode);
    cd::expected<void, Error>     join(uint32_t lobby_id, uint32_t player_id);
    void                          leave(uint32_t lobby_id, uint32_t player_id);
    void                          close(uint32_t lobby_id);

    [[nodiscard]] const Lobby*    lobby(uint32_t lobby_id) const;
};

class SkillBasedFinder
{
public:
    explicit SkillBasedFinder(const LobbyRegistry&);

    // Returns the closest-matching lobby for `player`, or std::nullopt if
    // no lobby is within (skill_window, same_region).
    [[nodiscard]] std::optional<uint32_t>
                                  find(const PlayerProfile& player,
                                       float skill_window = 200.0F) const;
};

}
```

## Matching algorithm

`SkillBasedFinder::find(player, skill_window)` walks the registry's
open lobbies and returns the first one where:

1. `lobby.region` (median of joined players) **equals** `player.region`.
2. `|lobby.skill (mean) − player.skill_rating|` ≤ `skill_window`.

The default 200-point window matches the canonical ELO Bayesian
update step — wide enough to absorb new-player rating noise without
catastrophic mismatches.

For **cross-region** play, the caller widens the search by walking
adjacent regions explicitly (`{"us-west-2", "us-east-1"}` etc.). The
finder deliberately does not own the region adjacency table — that
lives in `cd::network::lobby::RegionTopology` (or, today, in the
game's matchmaking glue).

## Dependencies

* `cd::core` — `Defines.hpp`, `CD_CACHE_ALIGN`.
* `cd::net` — link-only (Sprint-2 wires headers).

## See also

* `cd::network::lobby` — consumes the paired `Lobby` to run the
  room.
