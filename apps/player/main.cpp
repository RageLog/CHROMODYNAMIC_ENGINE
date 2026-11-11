// =============================================================================
// CHROMODYNAMIC — apps/player/main.cpp (cd_player)
//
// Thin standalone runtime entry point (< 30 LOC, ADR-20261112).
// Delegates CLI parsing + bundle auto-discovery to cd::player::parse_player_cli
// and execution to cd::player::PlayerApp.
// =============================================================================
#include "PlayerApp.hpp"
#include "PlayerConfig.hpp"

int main(int argc, char** argv)
{
    cd::player::PlayerApp app(cd::player::parse_player_cli(argc, argv));
    return app.run();
}
