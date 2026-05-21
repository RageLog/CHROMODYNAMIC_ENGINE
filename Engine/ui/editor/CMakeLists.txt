# engine/ui/editor — cd::editor (Phase 5 / Sprint S5.3)
cd_add_library(editor
  TYPE STATIC
  SOURCES src/Editor.cpp
  PUBLIC_DEPS
    cd::editor_ui
    cd::ui
    cd::scene
    cd::ecs
    cd::input
    cd::math
    cd::core
)
if(CD_ENABLE_TESTING)
  add_subdirectory(tests)
endif()
