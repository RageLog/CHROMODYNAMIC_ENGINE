# engine/foundation/mem — cd::mem allocator suite
# Sprint S2.1.b — ADR-005 §D + ADR-017 P0

cd_add_library(mem
  SOURCES
    src/PageAllocator.cpp
  PUBLIC_DEPS cd::core
)

if(CD_ENABLE_TESTING)
  add_subdirectory(tests)
endif()
