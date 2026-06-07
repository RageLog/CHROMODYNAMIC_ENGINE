# samples/asset — asset import / cooked / json / save samples.
#
# phase902-sample-consolidation: per
# [[feedback-sample-consolidation-and-per-feature-demo]], headless
# "console proof" smokes have been folded into library gtests.
# Retired in phase 902:
#   * hello_json              → engine/asset/tests/json/test_asset_json.cpp (22 cases)
#   * hello_audio_streamer    → engine/asset/audio_streamer/tests/test_audio_streamer.cpp + _async.cpp
#   * hello_scene_streamer    → engine/asset/scene_streamer/tests/test_scene_streamer.cpp + _async.cpp
#   * hello_texture_streamer  → engine/asset/texture_streamer/tests/test_texture_streamer.cpp + _async.cpp
#   * hello_asset_validator   → engine/asset/validator/tests/test_validator.cpp (16 cases)
# Source directories deleted in the same commit.
#
# Retired in phase 905:
#   * hello_scene_save → engine/world/scene/tests/test_scene.cpp
#     (86 cases incl. SceneSerializer round-trip + parent re-link).
#     Phase 902 audit missed test_scene.cpp because it lives at
#     engine/world/scene/tests/ not engine/scene/tests/.
#
# Retained (no library tests yet OR substantive interactive demo):
#   * hello_asset_registry        — no library tests yet; queue for
#     a new engine/asset/tests/test_asset_registry.cpp
#   * hello_texture_compress_ratio — texture cooker tool (category c)
#
# Headless asset samples first.
add_subdirectory(hello_asset_registry)
add_subdirectory(hello_texture_compress_ratio)
# Vulkan-gated samples that need rendering to visualize the imported asset.
if(CD_ENABLE_VULKAN)
  add_subdirectory(hello_mesh)
  add_subdirectory(hello_texture)
  add_subdirectory(hello_gltf)
  add_subdirectory(hello_obj)
  add_subdirectory(hello_cooked)
  add_subdirectory(hello_textured_cooked)
endif()
