cd_add_test(asset_scene_streamer
  SOURCES test_scene_streamer.cpp
  DEPS    cd::asset_scene_streamer
)

cd_add_test(asset_scene_streamer_async
  SOURCES test_scene_streamer_async.cpp
  DEPS    cd::asset_scene_streamer
)
