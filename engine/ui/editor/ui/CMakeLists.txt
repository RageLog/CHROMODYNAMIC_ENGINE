# engine/ui/editor_ui — cd::editor_ui (Phase 5 / Sprint S5.2)
cd_add_library(editor_ui
  TYPE STATIC
  SOURCES src/EditorWidgets.cpp
  PUBLIC_DEPS cd::ui cd::scene cd::ecs cd::math cd::core
)
if(CD_ENABLE_TESTING)
  add_subdirectory(tests)
endif()
