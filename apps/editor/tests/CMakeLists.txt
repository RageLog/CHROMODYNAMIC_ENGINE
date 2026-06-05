# =============================================================================
# apps/editor/tests — editor binary unit tests
#
# phase775 / FINALE-8 W1B H2 — splash audio cue synthetic test.
# Verifies that SplashAudioCue::synthesize() produces non-zero PCM at the
# expected timing, and that trigger_if_needed() fires exactly once.
#
# This target is independent of the Vulkan gate (it only depends on
# cd::audio which is always available); gated on CD_ENABLE_TESTING via
# cd_add_test's internal guard.
# =============================================================================

cd_add_test(splash_audio_cue
  SOURCES test_splash_audio_cue.cpp
  DEPS    cd::audio cd::core
)

# The test's source includes ../SplashAudioCue.hpp from the parent directory.
# cd_add_test creates target cd_test_splash_audio_cue; add the parent dir to
# its include path so the relative #include resolves correctly.
if(TARGET cd_test_splash_audio_cue)
  target_include_directories(cd_test_splash_audio_cue
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..
  )
  set_target_properties(cd_test_splash_audio_cue PROPERTIES
    FOLDER "tests/splash_audio_cue"
  )
endif()
