# engine/foundation/profile — cd::profile
#
# Lightweight CPU scope-timer + sink abstraction. Depends only on cd::core.

cd_add_library(profile
  TYPE STATIC
  SOURCES
    src/Scope.cpp
  PUBLIC_DEPS
    cd::core
)

if(CD_ENABLE_TESTING)
  add_subdirectory(tests)
endif()
