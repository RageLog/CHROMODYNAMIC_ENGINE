# samples/game/hello_game — Phase 479 (G1.5 closeout of
# ADR-20260530-gameplay-library-family).
#
# Ties together the four G1 libraries shipped in phases 459 / 462 / 470 / 471:
#   * cd::gameplay_time              (TimeKeeper fixed-tick loop)
#   * cd::gameplay_input_binding     (ActionMap "Jump" -> Space + Gamepad A)
#   * cd::game_save                  (atomic slot save + restore of entity
#                                     transforms)
#   * cd::game_settings              (volume + language persisted across runs)
#
# Console-only. No platform window, no RHI, no Vulkan — the sample is
# deliberately portable so the cd::game::* family can be exercised on
# every host that links cd::core. The output is human-readable text on
# stdout (build / CI smoke gate = "compiles + exit 0").

cd_add_sample(hello_game
  SOURCES main.cpp
  DEPS
    cd::core
    cd::gameplay_time
    cd::gameplay_input_binding
    cd::game_save
    cd::game_settings
)
