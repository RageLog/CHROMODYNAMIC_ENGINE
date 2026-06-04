cd_add_test(asset_streamer_pool
  SOURCES test_streamer_pool.cpp
  DEPS    cd::asset_streamer_pool
          cd::asset_scene_streamer
          cd::asset_texture_streamer
          cd::asset_audio_streamer
          cd::asset_shader_cache
          cd::rhi
)
