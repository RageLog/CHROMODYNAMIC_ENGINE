cd_add_test(asset_gltf
  SOURCES test_asset_gltf.cpp
  DEPS    cd::asset_gltf cd::asset cd::core cd::math
)

cd_add_test(scene_loader
  SOURCES test_scene_loader.cpp
  DEPS    cd::asset_gltf cd::asset cd::core cd::math
)
