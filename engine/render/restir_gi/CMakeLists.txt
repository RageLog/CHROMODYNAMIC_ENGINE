# engine/render/restir_gi — cd::restir_gi (Day 7/L)
# Ouyang 2021 ReSTIR GI — bounce-sample reservoir + spatial/temporal
# reuse for path tracing.
cd_add_library(restir_gi
  TYPE INTERFACE
  PUBLIC_DEPS
    cd::core
    cd::math
)

if(CD_ENABLE_TESTING)
  add_subdirectory(tests)
endif()
