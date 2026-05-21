# engine/foundation/math — cd::math
# Sprint S2.7 — ADR-017 P4 (scalar/vector/matrix primitives, scalar-only v1)

cd_add_library(math
  TYPE INTERFACE
  PUBLIC_DEPS cd::core
)

if(CD_ENABLE_TESTING)
  add_subdirectory(tests)
endif()
