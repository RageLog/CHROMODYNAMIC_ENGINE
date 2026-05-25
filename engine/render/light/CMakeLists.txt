# engine/render/light — cd::light (Phase 165)
# State-of-the-art light primitives: Frostbite + Filament model.
# Header-only; the tiny TU just exists to give CMake a target to attach
# headers + tests to.
cd_add_library(light
  TYPE INTERFACE
  PUBLIC_DEPS
    cd::core
    cd::math
    cd::asset
)

if(CD_ENABLE_TESTING)
  add_subdirectory(tests)
endif()
