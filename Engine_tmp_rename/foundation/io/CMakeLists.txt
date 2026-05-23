# engine/foundation/io — cd::io
# Sprint S2.6 — ADR-017 P3 (serialization primitives)

cd_add_library(io
  TYPE INTERFACE
  PUBLIC_DEPS cd::core
)

if(CD_ENABLE_TESTING)
  add_subdirectory(tests)
endif()
