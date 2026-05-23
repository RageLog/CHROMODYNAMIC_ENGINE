# engine/foundation/vfs — cd::vfs
# Sprint S2.9 — ADR-017 P4 (virtual filesystem with overlay layers)

cd_add_library(vfs
  TYPE STATIC
  SOURCES
    src/FilesystemSource.cpp
  PUBLIC_DEPS cd::core cd::io
)

if(CD_ENABLE_TESTING)
  add_subdirectory(tests)
endif()
