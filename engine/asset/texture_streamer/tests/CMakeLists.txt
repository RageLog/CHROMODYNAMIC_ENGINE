cd_add_test(asset_texture_streamer
  SOURCES test_texture_streamer.cpp
  DEPS    cd::asset_texture_streamer
          cd::rhi
)

cd_add_test(asset_texture_streamer_async
  SOURCES test_texture_streamer_async.cpp
  DEPS    cd::asset_texture_streamer
          cd::rhi
)
