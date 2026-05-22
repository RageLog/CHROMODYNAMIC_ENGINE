# engine/foundation/plugin — cd::plugin
# Sprint S2.4 — ADR-017 P2

cd_add_library(plugin
  SOURCES
    src/Loader.cpp
    src/HotReload.cpp
    src/FileWatcher.cpp
  PUBLIC_DEPS cd::core
)

# macOS native watcher (FSEvents) needs the CoreServices framework.
# Linked PUBLIC so downstream targets transitively pick it up.
if(APPLE)
  find_library(CD_CORESERVICES_FRAMEWORK CoreServices)
  if(CD_CORESERVICES_FRAMEWORK)
    target_link_libraries(cd_plugin PUBLIC ${CD_CORESERVICES_FRAMEWORK})
  endif()
endif()

if(CD_ENABLE_TESTING)
  add_subdirectory(tests)
endif()
