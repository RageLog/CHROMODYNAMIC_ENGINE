# engine/ui/animation -- ADR-20260530 Phase 3.2 (property animation + easing
# curves + timeline).
#
# cd::ui_animation -- pure-C++ tween / easing engine. No RHI / no font / no
# input dependencies. Header API + .cpp impl. Penner easing reference set
# plus a multi-channel Timeline scheduler.

cd_add_library(ui_animation
  TYPE STATIC
  SOURCES
    src/Animation.cpp
  PUBLIC_DEPS
    cd::core
    cd::math
)

if(CD_ENABLE_TESTING)
  add_subdirectory(tests)
endif()
