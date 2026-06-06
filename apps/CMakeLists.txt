# =============================================================================
# apps/ -- end-user application binaries (Phase 524 / T0.2).
#
# Distinguished from samples/ in that these are shipping editor tools /
# launchers, not pedagogical "hello_*" demos. Today the only resident is
# the cd::editor binary; future shipping apps (e.g. cd_runtime_player,
# cd_asset_cooker_ui) drop in alongside as they land.
#
# Same Vulkan gating as samples/editor since the editor binary depends on
# cd::rhi_vulkan + cd::render. CD_ENABLE_VULKAN is defined by
# engine/rhi/vulkan/CMakeLists.txt and propagated as a CACHE BOOL.

if(CD_ENABLE_VULKAN)
  add_subdirectory(editor)
endif()

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
