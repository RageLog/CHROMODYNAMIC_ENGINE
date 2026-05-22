# tools/cook_mesh — offline asset cooker
#
# Plain executable (NOT a cd_add_sample because samples are interactive
# demos; this is a CLI build-pipeline tool). Output name `cd_cook_mesh`
# so the binary is findable on PATH-style searches and namespaced.

add_executable(cd_cook_mesh main.cpp)
target_link_libraries(cd_cook_mesh PRIVATE
  cd::asset_obj
  cd::asset_gltf
  cd::asset_cdmesh
  cd::core
  cd::math
)
target_compile_features(cd_cook_mesh PUBLIC cxx_std_23)
cd_apply_warnings(cd_cook_mesh)
cd_apply_sanitizers(cd_cook_mesh)
cd_apply_coverage(cd_cook_mesh)
set_target_properties(cd_cook_mesh PROPERTIES
  FOLDER "tools"
  OUTPUT_NAME "cd_cook_mesh"
)
