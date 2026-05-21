# engine/ui/ui — cd::ui (Phase 5 / Sprint S5.1)
#
# Retained-mode widget tree. Renderer-agnostic — widgets emit DrawCommands
# that frontends (editor / app) consume. Depends on cd::core only.
cd_add_library(ui
  TYPE STATIC
  SOURCES src/Widget.cpp
  PUBLIC_DEPS cd::core
)
if(CD_ENABLE_TESTING)
  add_subdirectory(tests)
endif()
