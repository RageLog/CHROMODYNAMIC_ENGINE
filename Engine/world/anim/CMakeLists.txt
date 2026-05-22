cd_add_library(anim
  TYPE STATIC
  SOURCES src/Animation.cpp
  PUBLIC_DEPS cd::math cd::core
)
if(CD_ENABLE_TESTING)
  add_subdirectory(tests)
endif()
