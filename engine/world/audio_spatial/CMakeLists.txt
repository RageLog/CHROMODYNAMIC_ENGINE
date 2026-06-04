# engine/world/audio_spatial — Phase 693
# cd::audio::spatial — 3D positional audio sidecar to cd::audio.
#
# Sprint-1: ITD/ILD panning + 1/r distance attenuation + Doppler pitch shift.
# Sprint-2 queue: HRTF convolution via openal-soft or steam-audio (vcpkg).
#
# Sidecar relationship:
#   cd::audio       — 2D mixer / playback backend (WASAPI / CoreAudio / ALSA)
#   cd::audio::spatial — WHERE sounds come from (positional math only, no I/O)
#
# No platform-conditional link: pure math, no OS audio API calls.
cd_add_library(audio_spatial
  TYPE STATIC
  SOURCES
    src/AudioSpatial.cpp
  PUBLIC_DEPS cd::core
)

if(CD_ENABLE_TESTING)
  add_subdirectory(tests)
endif()
