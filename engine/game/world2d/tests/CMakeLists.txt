cd_add_test(world2d
  SOURCES test_world2d.cpp
  DEPS    cd::world2d cd::render2d cd::schema cd::physics cd::script cd::asset_json cd::asset_pak cd::audio
)

cd_add_test(world2d_stress
  SOURCES test_world2d_stress.cpp
  DEPS    cd::world2d cd::render2d cd::schema cd::physics cd::script cd::asset_json cd::asset_pak cd::audio
)

