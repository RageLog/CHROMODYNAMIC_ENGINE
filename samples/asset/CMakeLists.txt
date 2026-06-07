# samples/asset — asset import / cooked / json / save samples.
#
# phase902-sample-consolidation: per
# [[feedback-sample-consolidation-and-per-feature-demo]], headless
# "console proof" smokes have been folded into library gtests.
# Retired in phase 902:
#   * hello_json              -> engine/asset/tests/json/test_asset_json.cpp (22)
#   * hello_audio_streamer    -> engine/asset/audio_streamer/tests/test_audio_streamer.cpp + _async.cpp
#   * hello_scene_streamer    -> engine/asset/scene_streamer/tests/test_scene_streamer.cpp + _async.cpp
#   * hello_texture_streamer  -> engine/asset/texture_streamer/tests/test_texture_streamer.cpp + _async.cpp
#   * hello_asset_validator   -> engine/asset/validator/tests/test_validator.cpp (16)
#
# Retired in phase 905:
#   * hello_scene_save -> engine/world/scene/tests/test_scene.cpp (86 incl.
#     SceneSerializer round-trip + parent re-link).
#
# Retired in phase 916:
#   * hello_asset_registry        -> engine/asset/tests/test_asset.cpp (59 incl.
#     AssetRegistryTest.{LoadTextSucceeds, RepeatLoadIsCached, UnknownTagRejected,
#     VfsMissingProducesError, ReleaseAndClear, InstallByPasses} +
#     tag_from_extension table-driven cases for json / image / obj / gltf /
#     cdmesh / cdtex / wav / ktx2 / hdr / png / jpg / bmp). Phase 905 audit
#     incorrectly reported zero coverage -- the existing tests live under
#     class AssetRegistryTest : ::testing::Test and use TEST_F so a flat
#     'TEST(' grep missed them.
#   * hello_texture_compress_ratio -> engine/asset/texture_compress/tests/
#     test_texture_compress.cpp (19 incl. T2_Bc1RatioIsEightToOne +
#     T8_AnalyzeRatioIsEightToOne + RmseZeroForSolidColor + mip-chain
#     byte-count + BC7 encode-or-nullopt + non-multiple-dim rejection).
#
# Retained: Vulkan-gated visual viewers for imported assets. These are
# category (b) demos -- canonical "here is what cd::asset_<format>
# produces on screen" entry points. Queued for fold into hello_engine
# once it grows an Asset Viewer panel.
if(CD_ENABLE_VULKAN)
  add_subdirectory(hello_mesh)
  add_subdirectory(hello_texture)
  add_subdirectory(hello_gltf)
  add_subdirectory(hello_obj)
  add_subdirectory(hello_cooked)
  add_subdirectory(hello_textured_cooked)
endif()
