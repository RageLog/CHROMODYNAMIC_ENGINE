cd_add_test(asset_audio_streamer
  SOURCES test_audio_streamer.cpp
  DEPS    cd::asset_audio_streamer
)

cd_add_test(asset_audio_streamer_async
  SOURCES test_audio_streamer_async.cpp
  DEPS    cd::asset_audio_streamer
)
