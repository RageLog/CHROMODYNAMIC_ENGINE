# engine/foundation/bench — cd::bench
#
# Header-only microbenchmark utility. Depends only on the C++ stdlib;
# we link cd::core for the INTERFACE-only target so consumers can pull
# `<cd/bench/Benchmark.hpp>` via the alias.

cd_add_library(bench
  TYPE INTERFACE
  PUBLIC_DEPS
    cd::core
)

if(CD_ENABLE_TESTING)
  add_subdirectory(tests)
endif()
