# engine/runtime — cd::runtime
# Sprint S2.10 — composition root for foundation services.

cd_add_library(runtime
  TYPE STATIC
  SOURCES
    src/EngineContext.cpp
  PUBLIC_DEPS
    cd::core
    cd::asset
    cd::concurrency
    cd::diag
    cd::log
    cd::vfs
)

if(CD_ENABLE_TESTING)
  add_subdirectory(tests)
endif()
