# engine/foundation/log — cd::log
# Sprint S2.2 — ADR-013 + ADR-017 P1

cd_add_library(log
  TYPE INTERFACE
  PUBLIC_DEPS cd::core
)

if(CD_ENABLE_TESTING)
  add_subdirectory(tests)
endif()
