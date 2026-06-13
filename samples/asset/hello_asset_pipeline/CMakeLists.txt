# phase1150: 6 asset samples folded into hello_asset_pipeline
# Deps = union of all 6 former CMakeLists:
#   hello_mesh:           cd::platform cd::rhi cd::rhi_vulkan cd::shader cd::material cd::render cd::core cd::sample_common
#   hello_texture:        + (same)
#   hello_obj:            + cd::asset_obj cd::camera cd::math
#   hello_cooked:         + cd::asset_cdmesh
#   hello_gltf:           + cd::asset_gltf cd::camera cd::math
#   hello_textured_cooked:+ cd::asset_image cd::asset_cdtex
cd_add_sample(hello_asset_pipeline
  SOURCES main.cpp
  DEPS    cd::platform cd::rhi cd::rhi_vulkan cd::shader cd::material cd::render
          cd::asset_obj cd::asset_gltf cd::asset_cdmesh cd::asset_cdtex cd::asset_image
          cd::camera cd::math cd::core cd::sample_common
)
