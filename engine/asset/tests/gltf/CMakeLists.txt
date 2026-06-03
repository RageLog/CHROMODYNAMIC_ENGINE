cd_add_test(asset_gltf
  SOURCES test_asset_gltf.cpp
  DEPS    cd::asset_gltf cd::asset cd::core cd::math
)

cd_add_test(scene_loader
  SOURCES test_scene_loader.cpp
  DEPS    cd::asset_gltf cd::asset cd::core cd::math
)

# phase640 / T1.10 — glTF alphaMode round-trip (loader + cd::material::MaterialInstance).
cd_add_test(gltf_alpha_mode
  SOURCES test_gltf_alpha_mode.cpp
  DEPS    cd::asset_gltf cd::asset cd::core cd::math cd::material cd::rhi
)

# phase658 / T1.14 — infer_alpha_mode histogram heuristic (4 cases: pure opaque,
# mostly-opaque cutout edges, mixed mid-alpha, all-zero degenerate).
cd_add_test(gltf_alpha_mode_infer
  SOURCES test_gltf_alpha_mode_infer.cpp
  DEPS    cd::asset_gltf cd::asset cd::core cd::math
)
