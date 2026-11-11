# =============================================================================
# apps/ -- end-user application binaries (cd_editor, cd_player, ...).
# =============================================================================

add_subdirectory(common)

if(CD_ENABLE_VULKAN)
  add_subdirectory(editor)
endif()
add_subdirectory(player)

# Phase 771 — iOS demo application (Sprint-1 scaffold).
if(IOS AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/ios_demo/CMakeLists.txt")
  add_subdirectory(ios_demo)
endif()

# Phase 773 (FINALE-7 W2C Sprint-1) — Web demo (Emscripten only).
if(EMSCRIPTEN OR CD_ENABLE_APPS_WEB)
  add_subdirectory(web_demo)
endif()

# Phase 772 (FINALE-7 W2B F3) — Android demo (ANativeWindow + touch bridge).
if(ANDROID)
  add_subdirectory(android_demo)
endif()
