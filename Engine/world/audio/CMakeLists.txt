cd_add_library(audio
  TYPE STATIC
  SOURCES src/NullAudioBackend.cpp
  PUBLIC_DEPS cd::core
)
if(CD_ENABLE_TESTING)
  add_subdirectory(tests)
endif()
