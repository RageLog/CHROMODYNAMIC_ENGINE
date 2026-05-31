# D-F7: toggle between on-disk GLSL paths (dev iteration, default ON) and
# embedded strings only (release-friendly, no file deps).
option(HELLO_ENGINE_USE_ON_DISK_SHADERS
       "When ON: read shaders from <repo>/samples/engine/hello_engine/shaders/*.glsl at runtime (dev iteration). When OFF: use embedded GLSL strings (release-friendly, no file deps)."
       ON)

cd_add_sample(hello_engine
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
    cd::editor
    cd::editor_panel
    cd::frame_timing
    cd::imgui_backend
    cd::light
    cd::async_submit
    cd::atmosphere
    cd::brdf_ltc
    cd::brdf_sheen_clearcoat
    cd::brdf_sss
    cd::ddgi
    cd::decal
    cd::framegraph
    cd::ibl
    cd::ibl_gpu
    cd::texture_synth
    cd::gpu_particles
    cd::light_shafts
    cd::nrc
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
    cd::shader
    cd::world_container
    cd::sample_framework
)

# D-F7: propagate the toggle to the compiled binary as a preprocessor define.
# $<BOOL:...> maps the CMake boolean to 1/0 so C++ #if works directly.
target_compile_definitions(cd_sample_hello_engine PRIVATE
  HELLO_ENGINE_USE_ON_DISK_SHADERS=$<BOOL:${HELLO_ENGINE_USE_ON_DISK_SHADERS}>)

# Copy shaders folder next to exe so on-disk hot-reload works from
# build/bin/Debug/ (or Release/) cwd without needing to run from repo root.
add_custom_command(TARGET cd_sample_hello_engine POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders
    $<TARGET_FILE_DIR:cd_sample_hello_engine>/shaders
  COMMENT "Stage hello_engine shaders for on-disk hot-reload")

# phase508-generic-gltf-path: stage the sample-local Generic glTF folder
# next to the exe so HelloGenericGltf.hpp's "assets/samples/Generic/"
# candidate path resolves from build/bin/{Debug,Release}/ without needing
# the user to run from repo root. The candidate list also includes the
# absolute repo-root path, so this is a convenience rather than a hard
# requirement.
add_custom_command(TARGET cd_sample_hello_engine POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory
    $<TARGET_FILE_DIR:cd_sample_hello_engine>/assets/samples/Generic
  COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_CURRENT_SOURCE_DIR}/assets/Generic
    $<TARGET_FILE_DIR:cd_sample_hello_engine>/assets/samples/Generic
  COMMENT "Stage hello_engine generic-gltf sample assets")

# Marathon Run 23 phase N2B: ghost-shadow / TLAS-exclusion regression
# test (pure-CPU, no RHI device needed).
if(CD_ENABLE_TESTING)
  add_subdirectory(tests)
endif()
