# =============================================================================
# apps/ -- end-user application binaries (cd_editor, cd_player, ...).
# =============================================================================

if(NOT TARGET cd::app_common)
  add_library(cd_app_common INTERFACE)
  target_include_directories(cd_app_common INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/common
  )
  target_link_libraries(cd_app_common INTERFACE
    cd::asset_image
    cd::imgdiff
    cd::rhi
  )
  add_library(cd::app_common ALIAS cd_app_common)
endif()

if(CD_ENABLE_VULKAN)
  add_subdirectory(editor)
endif()
add_subdirectory(player)

# Phase 771 — iOS demo application (Sprint-1 scaffold).
# ios_demo directory only exists when committed; guard at parent level so
# Win11/Linux configs don't fail when ios_demo/ is absent.
if(IOS AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/ios_demo/CMakeLists.txt")
  add_subdirectory(ios_demo)
endif()

# Phase 773 (FINALE-7 W2C Sprint-1) — Web demo (Emscripten only).
# The subdirectory CMakeLists.txt contains an `if(NOT EMSCRIPTEN) return()`
# guard so this add_subdirectory is safe for all non-web presets.
if(EMSCRIPTEN OR CD_ENABLE_APPS_WEB)
  add_subdirectory(web_demo)
endif()

# Phase 772 (FINALE-7 W2B F3) — Android demo (ANativeWindow + touch bridge).
# android_demo/CMakeLists.txt contains an `if(NOT ANDROID) message(FATAL_ERROR)`
# guard so this block is gated here at the apps/ level to keep Win11 builds
# completely untouched.
if(ANDROID)
  add_subdirectory(android_demo)
endif()
