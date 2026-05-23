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
endif()

# CoreAudio backend: macOS-only. The TU has a build gate
# (#if defined(__APPLE__)); on Apple we link the AudioUnit /
# AudioToolbox / CoreAudio / CoreFoundation frameworks. Wave 79.
if(APPLE)
  find_library(CD_AUDIOUNIT_FRAMEWORK AudioUnit)
  find_library(CD_AUDIOTOOLBOX_FRAMEWORK AudioToolbox)
  find_library(CD_COREAUDIO_FRAMEWORK CoreAudio)
  find_library(CD_COREFOUNDATION_FRAMEWORK CoreFoundation)
  if(CD_AUDIOUNIT_FRAMEWORK AND CD_AUDIOTOOLBOX_FRAMEWORK
     AND CD_COREAUDIO_FRAMEWORK AND CD_COREFOUNDATION_FRAMEWORK)
    target_link_libraries(cd_audio PRIVATE
      ${CD_AUDIOUNIT_FRAMEWORK}
      ${CD_AUDIOTOOLBOX_FRAMEWORK}
      ${CD_COREAUDIO_FRAMEWORK}
      ${CD_COREFOUNDATION_FRAMEWORK})
  endif()
endif()

if(CD_ENABLE_TESTING)
  add_subdirectory(tests)
endif()
