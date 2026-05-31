cd_add_library(audio_dsp_fx
  TYPE STATIC
  SOURCES
    src/DspFx.cpp
  PUBLIC_DEPS cd::core cd::audio
)

if(CD_ENABLE_TESTING)
  add_subdirectory(tests)
endif()
