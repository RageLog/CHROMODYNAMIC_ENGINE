# engine/foundation/config — cd::config
# Sprint S2.8 — ADR-017 P4 (CVar persistence on top of cd::io)

cd_add_library(config
  TYPE INTERFACE
  PUBLIC_DEPS cd::core cd::io
)

if(CD_ENABLE_TESTING)
  add_subdirectory(tests)
endif()
