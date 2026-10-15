# tools/assetc — FAZ 10 Wave 10.1: Offline .cdpak Asset Bundle Cooker & Verifier

add_executable(cd_assetc main.cpp)
target_link_libraries(cd_assetc PRIVATE
  cd::asset_pak
  cd::asset_cdmesh
  cd::asset_cdtex
  cd::asset_json
  cd::core
)
target_compile_features(cd_assetc PUBLIC cxx_std_23)
cd_apply_warnings(cd_assetc)
cd_apply_sanitizers(cd_assetc)
cd_apply_coverage(cd_assetc)
set_target_properties(cd_assetc PROPERTIES
  FOLDER "tools"
  OUTPUT_NAME "cd_assetc"
)

if(CD_ENABLE_TESTING)
  add_test(
    NAME cd_assetc_smoke
    COMMAND cd_assetc --smoke-self-test
  )
  add_test(
    NAME cd_assetc_showcases_pack_verify
    COMMAND cd_assetc --input ${CMAKE_SOURCE_DIR}/showcases --output ${CMAKE_CURRENT_BINARY_DIR}/showcases_bundle.cdpak --verify
  )
endif()
