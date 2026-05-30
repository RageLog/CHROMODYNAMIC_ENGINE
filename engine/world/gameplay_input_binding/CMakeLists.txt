# engine/world/gameplay_input_binding — cd::gameplay_input_binding (Phase 462)
#
# Action-mapping (input remapping) layer one tier above raw cd::input polling.
# Gameplay code queries actions ("jump", "move_forward"); bindings (device +
# scancode/button/axis) are configured out-of-band so swapping device or
# rebinding mid-game is a data change, not a code change.
#
# Dependencies (CLAUDE.md §7): cd::core only at the public header level. The
# library is intentionally device-agnostic — callers translate any backend
# (cd::input::InputState, SDL, GLFW, replay tape) into the small
# RawInputSnapshot POD at the call site.

cd_add_library(gameplay_input_binding
  TYPE STATIC
  SOURCES
    src/InputBinding.cpp
  PUBLIC_DEPS
    cd::core
)

if(CD_ENABLE_TESTING)
  add_subdirectory(tests)
endif()
