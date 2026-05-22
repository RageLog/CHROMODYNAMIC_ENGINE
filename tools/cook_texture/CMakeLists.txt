# tools/cook_texture — offline PNG/JPG → BC7 .cdtex cooker

add_executable(cd_cook_texture main.cpp)
target_link_libraries(cd_cook_texture PRIVATE cd::asset_image cd::core)
target_compile_features(cd_cook_texture PUBLIC cxx_std_23)
cd_apply_warnings(cd_cook_texture)
cd_apply_sanitizers(cd_cook_texture)
cd_apply_coverage(cd_cook_texture)
set_target_properties(cd_cook_texture PROPERTIES
  FOLDER "tools"
  OUTPUT_NAME "cd_cook_texture"
)
