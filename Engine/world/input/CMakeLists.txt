cd_add_library(input
  TYPE STATIC
  SOURCES src/Input.cpp
  PUBLIC_DEPS cd::core
)
if(CD_ENABLE_TESTING)
  add_subdirectory(tests)
endif()
