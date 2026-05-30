# engine/ui/theme -- Phase 474 cd::ui::theme V2 design token system
#
# cd::ui_theme -- token-based theme tree (Material 3 shape). Replaces the
# legacy 2-palette stub at engine/ui/include/cd/ui/Theme.hpp by being a
# new library at engine/ui/theme/ -- the legacy header stays in place so
# pre-V2 call sites keep compiling.
#
# Dependencies: cd::core only (deliberately NOT cd::ui -- headless asset
# / tooling consumers should be able to read tokens without dragging the
# widget tree in).

cd_add_library(ui_theme
  TYPE STATIC
  SOURCES
    src/Theme.cpp
  PUBLIC_DEPS
    cd::core
)

if(CD_ENABLE_TESTING)
  add_subdirectory(tests)
endif()
