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
