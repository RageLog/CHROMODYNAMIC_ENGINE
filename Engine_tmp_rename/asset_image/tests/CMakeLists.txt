cd_add_test(asset_image
  SOURCES test_asset_image.cpp
  DEPS    cd::asset_image cd::asset cd::core
)

cd_add_test(bc7
  SOURCES test_bc7.cpp
  DEPS    cd::asset_image cd::core
)
