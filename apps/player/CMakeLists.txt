# apps/player — cd_player standalone runtime executable (FAZ 6 Wave 6.2)

add_executable(cd_player main.cpp)
target_link_libraries(cd_player PRIVATE
  cd::runtime
  cd::gameplay_framework
  cd::elements_domain
  cd::element_core
  cd::scene_renderer
  cd::framegraph
  cd::ui_renderer
  cd::ui_renderer_rhi
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
endif()
