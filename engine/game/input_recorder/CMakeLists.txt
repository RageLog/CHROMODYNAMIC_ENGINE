# engine/game/input_recorder — cd::game_input_recorder (Phase 651, M10 W4B)
#
# HL2-style input record/replay: capture player input events with deterministic
# timestamps to a binary .cdinput file; replay them frame-accurately via
# Replayer::next_event(). Enables:
#   * Deterministic regression testing (commit .cdinput as a golden fixture).
#   * Demo capture: record a 30-second movement sequence, replay 100x to
#     reproduce a flaky physics or animation interaction.
#   * CI smoke: run the same input sequence on every PR to guard against
#     simulation drift.
#
# Dependencies (CLAUDE.md §7): cd::core only. No math, no ECS, no render.
# The library is a pure state machine + binary serialiser; the engine routes
# replayed events to the appropriate input backend from the outside.

cd_add_library(game_input_recorder
  TYPE STATIC
  SOURCES
    src/InputRecorder.cpp
  PUBLIC_DEPS
    cd::core
)

if(CD_ENABLE_TESTING)
  add_subdirectory(tests)
endif()
