# engine/render/restir_di — cd::restir_di (Day 6/K)
# Bitterli 2020 ReSTIR direct illumination — per-pixel WRS reservoir
# + temporal/spatial reuse primitives. CPU reference + GLSL helpers.
cd_add_library(restir_di
  TYPE INTERFACE
  PUBLIC_DEPS
    cd::core
    cd::math
)

if(CD_ENABLE_TESTING)
  add_subdirectory(tests)
endif()
