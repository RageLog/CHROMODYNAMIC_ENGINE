# engine/foundation/time — cd::time
# Sprint S2.1.d — ADR-005 §F + ADR-017 P0/P2

cd_add_library(time
  SOURCES
    src/HiResClock.cpp
  PUBLIC_DEPS cd::core
)

if(CD_ENABLE_TESTING)
  add_subdirectory(tests)
endif()
