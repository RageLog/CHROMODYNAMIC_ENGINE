// =============================================================================
// CHROMODYNAMIC — samples/net/hello_matchmaker
// Phase 589 — console proof that cd::net::matchmaker is consumable as a
//             standalone library from an external sample.
//
// What the demo does:
//   - Creates a LobbyRegistry.
//   - Registers 8 players with varying skill ratings across two regions (EU/NA).
//   - Configures a SkillBasedFinder (target size = 4, max_skill_delta = 75).
//   - Iterates over players in arrival order: for each player, try to find an
//     existing open lobby; if none found, create a new one and join it.
//   - After all players are placed, prints the resulting lobbies and their
//     rosters (player id + skill_rating).
//   - Verifies that every player landed in exactly one lobby.
//   - Exits 0 on success.
//
// Console-only. No windows, no GPU. No hello_engine dependency.
// =============================================================================
#include <cd/net/matchmaker/Matchmaker.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace
{

using cd::net::matchmaker::LobbyRegistry;
using cd::net::matchmaker::PlayerProfile;
using cd::net::matchmaker::SkillBasedFinder;

// Region tags — 4-byte opaque blobs.
constexpr std::array<std::uint8_t, 4> kRegionEU { 1, 0, 0, 0 };
constexpr std::array<std::uint8_t, 4> kRegionNA { 2, 0, 0, 0 };

[[nodiscard]] PlayerProfile make_player(std::uint64_t               id,
                                        float                       skill,
                                        std::array<std::uint8_t, 4> region) noexcept
{
    PlayerProfile p;
    p.id           = id;
    p.skill_rating = skill;
    p.region       = region;
    return p;
}

}  // namespace

int main()
{
    std::printf("=== hello_matchmaker — cd::net::matchmaker consumable sample ===\n\n");

    // -------------------------------------------------------------------------
    // 1. Player roster — 8 players, mixed skill + region.
    //    Two EU clusters (~1000, ~1500) and two NA clusters (~800, ~1200).
    // -------------------------------------------------------------------------
    const std::vector<PlayerProfile> players = {
        make_player(101, 1000.0F, kRegionEU),
        make_player(102, 1040.0F, kRegionEU),
        make_player(103,  980.0F, kRegionEU),
        make_player(104, 1020.0F, kRegionEU),
        make_player(201,  800.0F, kRegionNA),
        make_player(202,  820.0F, kRegionNA),
        make_player(203, 1200.0F, kRegionNA),
        make_player(204, 1240.0F, kRegionNA),
    };

    std::printf("Players registered: %zu\n", players.size());
    std::printf("  %-6s  %-12s  %s\n", "ID", "skill", "region");
    for (const PlayerProfile& p : players)
    {
        const char* region_name = (p.region == kRegionEU) ? "EU" : "NA";
        std::printf("  %-6llu  %-12.1f  %s\n",
                    static_cast<unsigned long long>(p.id),
                    static_cast<double>(p.skill_rating),
                    region_name);
    }
    std::printf("\n");

    // -------------------------------------------------------------------------
    // 2. Matchmaking setup.
    // -------------------------------------------------------------------------
    LobbyRegistry  registry;
    SkillBasedFinder finder;

    // Each lobby holds up to 4 players; skill window is ±75 rating points.
    finder.configure(4, 75.0F);

    // Register all profiles so the finder can evaluate constraints.
    for (const PlayerProfile& p : players)
        registry.register_player(p);

    // -------------------------------------------------------------------------
    // 3. Place players into lobbies.
    // -------------------------------------------------------------------------
    std::printf("Placing players...\n");

    for (const PlayerProfile& p : players)
    {
        auto match = finder.find_match(p, registry);

        std::uint64_t target_lobby {};
        if (match.has_value())
        {
            target_lobby = *match;
            std::printf("  Player %llu (skill %.1f) -> joined existing lobby %llu\n",
                        static_cast<unsigned long long>(p.id),
                        static_cast<double>(p.skill_rating),
                        static_cast<unsigned long long>(target_lobby));
        }
        else
        {
            target_lobby = registry.create_lobby("ranked");
            std::printf("  Player %llu (skill %.1f) -> created new lobby %llu\n",
                        static_cast<unsigned long long>(p.id),
                        static_cast<double>(p.skill_rating),
                        static_cast<unsigned long long>(target_lobby));
        }

        const bool joined = registry.join_lobby(target_lobby, p.id);
        if (!joined)
        {
            std::fprintf(stderr, "ERROR: join_lobby failed for player %llu\n",
                         static_cast<unsigned long long>(p.id));
            return 1;
        }
    }

    // -------------------------------------------------------------------------
    // 4. Print formed lobbies.
    // -------------------------------------------------------------------------
    std::printf("\nFormed lobbies:\n");

    const auto lobbies = registry.active_lobbies();
    std::uint32_t total_placed = 0;

    for (const auto& lobby : lobbies)
    {
        if (!lobby.is_open) continue;  // skip any closed lobbies

        std::printf("  Lobby %llu [%s]  (%zu players)\n",
                    static_cast<unsigned long long>(lobby.lobby_id),
                    lobby.game_mode.c_str(),
                    lobby.player_ids.size());

        for (const std::uint64_t pid : lobby.player_ids)
        {
            const PlayerProfile* prof = registry.profile_of(pid);
            if (prof != nullptr)
            {
                const char* rn = (prof->region == kRegionEU) ? "EU" : "NA";
                std::printf("    player %llu  skill %.1f  region %s\n",
                            static_cast<unsigned long long>(pid),
                            static_cast<double>(prof->skill_rating),
                            rn);
            }
            else
            {
                std::printf("    player %llu  (no profile)\n",
                            static_cast<unsigned long long>(pid));
            }
            ++total_placed;
        }
    }

    // -------------------------------------------------------------------------
    // 5. Verify: every player must appear exactly once.
    // -------------------------------------------------------------------------
    if (total_placed != static_cast<std::uint32_t>(players.size()))
    {
        std::fprintf(stderr,
                     "\nERROR: expected %zu placed players, found %u.\n",
                     players.size(),
                     total_placed);
        return 1;
    }

    std::printf("\n[hello_matchmaker] all %u players placed — OK\n", total_placed);
    return 0;
}
