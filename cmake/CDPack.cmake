# =============================================================================
# CDPack.cmake — FAZ 10 Wave 10.1: CMake add_cdpack() offline .cdpak helper
#
# Usage:
#   add_cdpack(my_showcase_pak
#     INPUT_DIR "${CMAKE_SOURCE_DIR}/showcases/01_pbr_materials_sss_hair"
#     OUTPUT    "${CMAKE_BINARY_DIR}/pak/01_pbr_materials_sss_hair.cdpak"
#   )
# =============================================================================
include_guard(GLOBAL)

function(add_cdpack TARGET_NAME)
  set(_options "")
  set(_one_value_args INPUT_DIR OUTPUT)
  set(_multi_value_args "")
  cmake_parse_arguments(CDPACK "${_options}" "${_one_value_args}" "${_multi_value_args}" ${ARGN})

  if(NOT CDPACK_INPUT_DIR OR NOT CDPACK_OUTPUT)
    message(FATAL_ERROR "add_cdpack(${TARGET_NAME}) requires INPUT_DIR and OUTPUT")
  endif()

  if(TARGET cd_assetc)
    add_custom_command(
      OUTPUT "${CDPACK_OUTPUT}"
      COMMAND $<TARGET_FILE:cd_assetc> --pack "${CDPACK_INPUT_DIR}" "${CDPACK_OUTPUT}"
      DEPENDS cd_assetc
      COMMENT "[cd_assetc] Cooking .cdpak archive ${CDPACK_OUTPUT}"
      VERBATIM
    )
    add_custom_target(${TARGET_NAME} DEPENDS "${CDPACK_OUTPUT}")
    set_target_properties(${TARGET_NAME} PROPERTIES FOLDER "pak")
  endif()
endfunction()
