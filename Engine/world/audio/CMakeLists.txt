cd_add_library(audio
  TYPE STATIC
  SOURCES
    src/NullAudioBackend.cpp
    src/FileSinkAudioBackend.cpp
    src/WasapiAudioBackend.cpp
    src/CoreAudioBackend.cpp
    src/AlsaBackend.cpp
    src/NativeBackend.cpp
  PUBLIC_DEPS cd::core cd::math
)

# WASAPI backend: Windows-only. The TU has a build gate (#if defined(_WIN32))
# so non-Windows builds compile it to an empty TU; on Windows we link the
# COM / mmdevice libraries.
if(WIN32)
  target_link_libraries(cd_audio PRIVATE ole32 uuid)
  # Vendored stb / glslang etc. are not relevant here; only Win32 base libs.
endif()

if(CD_ENABLE_TESTING)
  add_subdirectory(tests)
endif()
