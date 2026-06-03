# samples/game/hello_cutscene_player — Phase 636 (M9 W2 closeout)
#
# Console-only proof that cd::game::cutscene_player is consumable as a
# standalone library. Builds a 3-phase cutscene (1000 ms each, 2 events/phase),
# drives it with 50 × 100 ms ticks, and prints every event fired per tick.
# Build gate: "compiles + exits 0".
#
# No RHI, no platform window, no Vulkan — headless by design.

cd_add_sample(hello_cutscene_player
  SOURCES main.cpp
  DEPS
    cd::game_cutscene_player
    cd::core
)
