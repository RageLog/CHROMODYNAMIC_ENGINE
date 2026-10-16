# =============================================================================
# apps/editor -- CHROMODYNAMIC 3D Engine Studio (editor.exe)
#
# Full Vulkan 1.3 3D PBR + Sponza Atrium + Skinned Animation + Hardware Z-Buffer
# + Multi-Light & CSM Shadows + Post-FX + Domestic Studio Workbench UI.
# =============================================================================

option(STUDIO_USE_ON_DISK_SHADERS
       "When ON: read shaders from apps/editor/shaders/*.glsl at runtime (dev iteration)."
       ON)

cd_add_app(editor
  SOURCES main.cpp
  DEPS
    cd::asset
    cd::asset_gltf
    cd::asset_image
    cd::asset_json
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

target_compile_definitions(cd_app_editor PRIVATE
  STUDIO_USE_ON_DISK_SHADERS=$<BOOL:${STUDIO_USE_ON_DISK_SHADERS}>)

if(STUDIO_USE_ON_DISK_SHADERS)
  target_compile_definitions(cd_app_editor PRIVATE
    STUDIO_SHADER_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/shaders")
endif()

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

option(CD_USE_MATERIAL_UI_ROUTE_A
       "Editor prefers Route A (cd::material UI variant) at runtime, with safety fallback to Route B (inline GLSL). Default ON (ship path)."
       ON)
if(CD_USE_MATERIAL_UI_ROUTE_A)
  target_compile_definitions(cd_app_editor PRIVATE CD_USE_MATERIAL_UI_ROUTE_A=1)
endif()
