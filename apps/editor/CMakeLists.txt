# =============================================================================
# apps/editor -- CHROMODYNAMIC Studio (cd_editor_lib + cd_app_editor)
#
# Valve Source 2 toolframework2-modeled modular C++23 editor architecture
# (ADR-20261112):
#   * cd_editor_lib -- compiled core, render, scene, physics, pipeline,
#                      workspace_2d, and Hammer 5 / Godot 4 clean UI docks
#   * cd_app_editor -- thin 23-line entry point (apps/editor/main.cpp)
# =============================================================================

option(STUDIO_USE_ON_DISK_SHADERS
       "When ON: read shaders from apps/editor/shaders/*.glsl at runtime (dev iteration)."
       ON)

option(CD_USE_MATERIAL_UI_ROUTE_A
       "Editor prefers Route A (cd::material UI variant) at runtime, with safety fallback to Route B (inline GLSL). Default ON (ship path)."
       ON)

cd_add_library(editor_lib
  EXCLUDE_FROM_INSTALL
  TYPE STATIC
  SOURCES
    core/StudioConfig.cpp
    core/StudioState.cpp
    core/StudioApp.cpp
    core/StudioMcpServer.cpp
    render/WorldRenderPasses.cpp
    scene/SceneOperations.cpp
    physics/PhysicsPieSolver.cpp
    pipeline/AssetIngest.cpp
    workspace_2d/Workspace2D.cpp
    ui/UnifiedTopBar.cpp
    ui/ProjectHubModal.cpp
    ui/SceneTreeDock.cpp
    ui/InspectorDock.cpp
    ui/ProjectAndToolsDock.cpp
    ui/ViewportHudAndGizmo.cpp
  PUBLIC_DEPS
    cd::asset
    cd::asset_gltf
    cd::asset_image
    cd::asset_json
    cd::asset_obj
    cd::asset_pak
    cd::schema
    cd::render2d
    cd::world2d
    cd::audio
    cd::camera
    cd::concurrency
    cd::core
    cd::ecs
    cd::game_query
    cd::game_trigger
    cd::game_camera
    cd::game_particles_event
    cd::editor
    cd::editor_panel
    cd::editor_panel_behavior_designer
    cd::ui_widgets
    cd::ui_layout
    cd::ui_renderer_rhi
    cd::ui_font
    cd::ui_theme
    cd::frame_timing
    cd::light
    cd::async_submit
    cd::atmosphere
    cd::brdf_ltc
    cd::brdf_sheen_clearcoat
    cd::brdf_sss
    cd::ddgi
    cd::debug_draw
    cd::debug_line
    cd::decal
    cd::framegraph
    cd::ibl
    cd::ibl_gpu
    cd::texture_synth
    cd::gpu_particles
    cd::light_shafts
    cd::mesh_shader
    cd::nrc
    cd::virtual_geometry
    cd::virtual_textures
    cd::physics
    cd::restir_di
    cd::restir_gi
    cd::script
    cd::material
    cd::math
    cd::net
    cd::platform
    cd::post_bloom
    cd::post_camera
    cd::post_composite
    cd::post_dof
    cd::post_exposure
    cd::post_gtao
    cd::post_motion_blur
    cd::post_smaa
    cd::post_ssr
    cd::post_taa
    cd::velocity
    cd::render
    cd::scene_renderer
    cd::render_scene_ingest
    cd::volumetric_clouds
    cd::volumetric_fog
    cd::rhi
    cd::rhi_vulkan
    cd::scene
    cd::gluon
    cd::shader
    cd::world_container
    cd::app_framework
    cd::app_common
)

target_include_directories(cd_editor_lib PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
)

target_compile_definitions(cd_editor_lib PUBLIC
  STUDIO_USE_ON_DISK_SHADERS=$<BOOL:${STUDIO_USE_ON_DISK_SHADERS}>
)

if(STUDIO_USE_ON_DISK_SHADERS)
  target_compile_definitions(cd_editor_lib PUBLIC
    STUDIO_SHADER_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/shaders"
  )
endif()

if(CD_USE_MATERIAL_UI_ROUTE_A)
  target_compile_definitions(cd_editor_lib PUBLIC CD_USE_MATERIAL_UI_ROUTE_A=1)
endif()

cd_add_app(editor
  SOURCES main.cpp
  DEPS
    cd::editor_lib
)

file(GLOB STUDIO_SHADER_SOURCES CONFIGURE_DEPENDS
  ${CMAKE_CURRENT_SOURCE_DIR}/shaders/*.glsl)
add_custom_command(TARGET cd_app_editor POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory
    $<TARGET_FILE_DIR:cd_app_editor>/shaders
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    ${STUDIO_SHADER_SOURCES}
    $<TARGET_FILE_DIR:cd_app_editor>/shaders
  COMMENT "Stage studio shaders for on-disk hot-reload (copy_if_different)")

add_custom_command(TARGET cd_app_editor POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory
    $<TARGET_FILE_DIR:cd_app_editor>/assets/samples/Generic
  COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_CURRENT_SOURCE_DIR}/assets/Generic
    $<TARGET_FILE_DIR:cd_app_editor>/assets/samples/Generic
  COMMENT "Stage studio generic-gltf assets")

if(CD_ENABLE_TESTING)
  add_subdirectory(tests)
  add_test(
    NAME cd_editor_headless_pie_smoke
    COMMAND cd_app_editor --headless 5 --pie-play --showcase 01_pbr_materials_sss_hair
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  )
endif()
