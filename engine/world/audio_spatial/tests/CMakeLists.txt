cd_add_test(audio_spatial
  SOURCES test_audio_spatial.cpp
  DEPS    cd::audio_spatial cd::core
)

cd_add_test(hrtf_mixer
  SOURCES test_hrtf_mixer.cpp
  DEPS    cd::audio_spatial cd::core
)
