# engine/asset_wav — cd::asset_wav
#
# Minimal RIFF/WAVE PCM loader. No external deps; cd::core only.

cd_add_library(asset_wav
  TYPE STATIC
  SOURCES
    src/Wav.cpp
  PUBLIC_DEPS
    cd::core
)

if(CD_ENABLE_TESTING)
  add_subdirectory(tests)
endif()
