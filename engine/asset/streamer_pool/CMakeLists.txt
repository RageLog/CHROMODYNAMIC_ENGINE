# engine/asset/streamer_pool — cd::asset_streamer_pool (Phase 705)
#
# Unified coordinator for scene / texture / audio / shader asset streamers.
# Sprint-1: priority-weighted round-robin dispatch via a single tick() call.
# Sprint-2 plan: async thread-pool with inflight-job cap.
#
# Depends on:
#   cd::asset_scene_streamer   (Phase 586) — scene tick dispatch
#   cd::asset_texture_streamer (Phase 599) — texture tick dispatch (+ IDevice)
#   cd::asset_audio_streamer   (Phase 619) — audio tick dispatch
#   cd::asset_shader_cache     (Phase 609) — shader stats (entry_count)
#   cd::rhi                    — IDevice forward for texture ticking
cd_add_library(asset_streamer_pool
  TYPE STATIC
  SOURCES
    src/StreamerPool.cpp
  PUBLIC_DEPS
    cd::core
    cd::asset
    cd::asset_scene_streamer
    cd::asset_texture_streamer
    cd::asset_audio_streamer
    cd::asset_shader_cache
    cd::rhi
)

if(CD_ENABLE_TESTING)
  add_subdirectory(tests)
endif()
