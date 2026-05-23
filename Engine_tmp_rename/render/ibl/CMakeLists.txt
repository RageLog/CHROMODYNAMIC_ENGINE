# engine/render/ibl — cd::ibl
#
# Image-based lighting CPU-side bakes. Header-only; cd::core + cd::math only.
# GPU compute-shader equivalents are Phase 6.

cd_add_library(ibl
  TYPE INTERFACE
  PUBLIC_DEPS
    cd::core
    cd::math
)

if(CD_ENABLE_TESTING)
  add_subdirectory(tests)
endif()
