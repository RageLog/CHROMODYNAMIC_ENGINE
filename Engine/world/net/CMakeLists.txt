cd_add_library(net
  TYPE STATIC
  SOURCES src/LoopbackConnection.cpp
  PUBLIC_DEPS cd::core
)
if(CD_ENABLE_TESTING)
  add_subdirectory(tests)
endif()
