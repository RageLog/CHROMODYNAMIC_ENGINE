# engine/foundation/plugin — cd::plugin
# Sprint S2.4 — ADR-017 P2

cd_add_library(plugin
  SOURCES
    src/Loader.cpp
    src/HotReload.cpp
  PUBLIC_DEPS cd::core
)

if(CD_ENABLE_TESTING)
  add_subdirectory(tests)
endif()
