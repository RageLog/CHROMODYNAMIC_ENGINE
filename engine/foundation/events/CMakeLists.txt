# engine/foundation/events — cd::events
# Sprint S2.2 — ADR-017 P1

cd_add_library(events
  TYPE INTERFACE
  PUBLIC_DEPS cd::core
)

if(CD_ENABLE_TESTING)
  add_subdirectory(tests)
endif()
