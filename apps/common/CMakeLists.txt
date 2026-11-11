# =============================================================================
# apps/common — cd::app_common shared application infrastructure library
#
# Single canonical owner of:
#   * FileTextIo     — UTF-8 BOM-safe text reading & atomic writing
#   * CameraMath3D   — 3D camera basis, NDC unproject, VK screen project, YXZ quat/euler
#   * FrameCapture   — Swapchain BGRA->RGBA readback + DrawBatcher PNG capture
#   * PackageExporter— Standalone .cdpak (CDPK) + cd_player bundle packager
# =============================================================================

cd_add_library(app_common
  TYPE STATIC
  SOURCES
    src/FileTextIo.cpp
    src/CameraMath3D.cpp
    src/FrameCapture.cpp
    src/PackageExporter.cpp
  PUBLIC_DEPS
    cd::core
    cd::math
    cd::asset_image
    cd::asset_pak
    cd::imgdiff
    cd::rhi
    cd::ui_renderer
)

target_include_directories(cd_app_common PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
)

if(CD_ENABLE_TESTING)
  cd_add_test(app_common
    SOURCES tests/test_app_common.cpp
    DEPS
      cd::app_common
      cd::asset_image
      cd::asset_pak
      cd::ui_renderer
      cd::core
  )
endif()
