# engine/foundation/diag — cd::diag
# Sprint S2.1.e — ADR-013 §C + ADR-017 P0

cd_add_library(diag
  SOURCES
    src/CrashReporter.cpp
    src/Assert.cpp
  PUBLIC_DEPS cd::core cd::platform
)

if(CD_ENABLE_TESTING)
  add_subdirectory(tests)
endif()
