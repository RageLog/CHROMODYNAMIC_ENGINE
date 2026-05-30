# samples/game/hello_narrative - Phase 505 (G4.4 - closes Phase G4).
#
# End-to-end demo of the Wave-2 gameplay narrative stack:
#   * cd::game::dialogue - branching VM (phase 499)
#   * cd::game::quest    - QuestLog + objective tracker (phase 500)
#   * cd::game::l10n     - StringTable + CLDR plurals + RTL (phase 501)
#
# The sample is headless (console only). The locales/ folder is staged
# next to the built executable via a POST_BUILD copy so the binary can
# locate its .kv tables regardless of cwd.
#
# Dependencies (CLAUDE.md S7): core + the three Wave-2 narrative libraries.
cd_add_sample(hello_narrative
  SOURCES main.cpp
  DEPS    cd::core
          cd::game_dialogue
          cd::game_quest
          cd::game_l10n
)

# Stage the locales/ folder next to the built executable so the sample
# can locate its .kv tables from build/bin/Debug/ (or Release/) without
# needing to run from the repo root.
add_custom_command(TARGET cd_sample_hello_narrative POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_CURRENT_SOURCE_DIR}/locales
    $<TARGET_FILE_DIR:cd_sample_hello_narrative>/locales
  COMMENT "Stage hello_narrative locale tables next to executable")
