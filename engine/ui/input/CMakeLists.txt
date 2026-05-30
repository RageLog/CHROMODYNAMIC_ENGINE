# engine/ui/input — Input plumbing (ADR-20260530, Phase 2.1)
#
# cd::ui_input — pure-C++ hit-test + focus chain + tab navigation + modal
# capture for the retained-mode UI. No RHI / no font / no platform window
# dep; the frontend bridge translates raw input into MouseEvent / KeyEvent
# and feeds them in. Public types live in cd/ui/input/Input.hpp.

cd_add_library(ui_input
  TYPE STATIC
  SOURCES
    src/Input.cpp
  PUBLIC_DEPS
    cd::core
)

if(CD_ENABLE_TESTING)
  add_subdirectory(tests)
endif()
