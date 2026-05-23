# engine/foundation/concurrency — cd::concurrency primitives
# Sprint S2.1.c — ADR-005 §G + ADR-015 + ADR-017 P0/P1

cd_add_library(concurrency
  TYPE INTERFACE
  PUBLIC_DEPS cd::core
)

if(CD_ENABLE_TESTING)
  add_subdirectory(tests)
endif()
