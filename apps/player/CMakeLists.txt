# =============================================================================
# apps/player — cd_player standalone runtime executable (ADR-20261112)
# =============================================================================

add_executable(cd_player
  main.cpp
  PlayerConfig.cpp
  PlayerApp.cpp
  ShowcaseRenderer3D.cpp
)

target_link_libraries(cd_player PRIVATE
  cd::app_common
  cd::runtime
  cd::gameplay_framework
  cd::elements_domain
  cd::element_core
  cd::world2d
  cd::render2d
  cd::asset
  cd::asset_pak
  cd::shader
  cd::scene_renderer
  cd::framegraph
  cd::ui_renderer
  cd::ui_renderer_rhi
  cd::audio
  cd::render
  cd::rhi_vulkan
  cd::rhi
  cd::platform
  cd::core
)

if(CD_ENABLE_TESTING)
  add_test(
    NAME cd_player_all_showcases_smoke
    COMMAND cd_player --all-showcases --smoke-frames 3
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  )
  add_test(
    NAME cd_player_genre_platformer_smoke
    COMMAND cd_player --genre platformer --null --frames 5
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  )
  add_test(
    NAME cd_player_genre_rpg_smoke
    COMMAND cd_player --genre rpg --null --frames 5
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  )
  add_test(
    NAME cd_player_genre_arena_smoke
    COMMAND cd_player --genre arena --null --frames 5
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  )

  add_test(
    NAME cd_player_help_exit_smoke
    COMMAND cd_player --help
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  )

  add_test(
    NAME cd_player_invalid_flag_smoke
    COMMAND cd_player --headless --invalid-flag
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  )
  set_tests_properties(cd_player_invalid_flag_smoke PROPERTIES WILL_FAIL TRUE)

  add_test(
    NAME cd_player_invalid_genre_smoke
    COMMAND cd_player --headless --genre invalid_preset
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  )
  set_tests_properties(cd_player_invalid_genre_smoke PROPERTIES WILL_FAIL TRUE)

  cd_add_test(player_config
    SOURCES tests/test_player_config.cpp PlayerConfig.cpp
    DEPS cd::core GTest::gtest GTest::gtest_main
  )
endif()
