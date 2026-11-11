# engine/foundation/schema — cd::schema
# FAZ 33 (ADR-20261111) — Self-describing C++23 property & AI command schema layer

cd_add_library(schema
  TYPE STATIC
  SOURCES
    src/Schema.cpp
  PUBLIC_DEPS
    cd::core
    cd::math
)

if(CD_ENABLE_TESTING)
  add_subdirectory(tests)
endif()
