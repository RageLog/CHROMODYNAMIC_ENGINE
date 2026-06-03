# samples/asset — asset import / cooked / json / save samples.
# Headless asset samples first.
add_subdirectory(hello_asset_registry)
add_subdirectory(hello_asset_validator)
add_subdirectory(hello_json)
add_subdirectory(hello_scene_save)
add_subdirectory(hello_scene_streamer)
add_subdirectory(hello_texture_streamer)
add_subdirectory(hello_audio_streamer)
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
