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
    # phase1173-hello-world-fold: gameplay-stack libs for the folded
    # samples/game/hello_world "Gameplay Stack Demo" R-Showcase probe.
    cd::game_query
    cd::game_trigger
    cd::game_camera
    cd::game_particles_event
    cd::editor
    cd::editor_panel
    # phase1174 — behavior_designer fold (ADR-20260614 Kanıt 6 / §5.2).
    # cd::ui_* overlay stack so hello_engine can render the library
    # BehaviorDesigner panel via a cd::ui::renderer_rhi::Submitter that
    # co-exists with ImGui. cd::game_ai_bt + cd::ui_renderer + cd::ui_widgets
    # arrive transitively as PUBLIC_DEPS of cd::editor_panel_behavior_designer
    # (verified in engine/ui/editor/panel_behavior_designer/CMakeLists.txt).
    cd::editor_panel_behavior_designer
    cd::ui_widgets
    # phase1177 — hello_ui fold (ADR-20260614 §3.4, Job C). The folded
    # hello_ui "UI Widgets Demo" panel uses the cd::ui_layout Flex solver
    # for its Button/Slider/Label column tree (direct #include of Flex.hpp).
    cd::ui_layout
    cd::ui_renderer_rhi
    cd::ui_font
    cd::ui_theme
    cd::frame_timing
    cd::imgui_backend
    cd::light
    cd::async_submit  # TODO(phase1143): unused link, analyst 2026-06-13
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
    cd::sample_framework
)

# D-F7: propagate the toggle to the compiled binary as a preprocessor define.
# $<BOOL:...> maps the CMake boolean to 1/0 so C++ #if works directly.
target_compile_definitions(cd_sample_hello_engine PRIVATE
  HELLO_ENGINE_USE_ON_DISK_SHADERS=$<BOOL:${HELLO_ENGINE_USE_ON_DISK_SHADERS}>)

# Copy shaders folder next to exe so on-disk hot-reload works from
# build/bin/Debug/ (or Release/) cwd without needing to run from repo root.
#
# X5-3(a) (ADR-20260608 addendum A.3) — EXE-side staleness guard. The prior
# `copy_directory` had two problems: (1) it did not list the shader files as
# DEPENDS, so a shader-ONLY edit (no .cpp touched) triggered a relink-only
# build that NEVER re-fired this POST_BUILD step, leaving the exe-side copy
# STALE while the source was fresh; (2) a stale exe-side copy produces a FALSE
# byte-identical golden (the golden is computed against the unchanged blob,
# masking the edit). Fix: enumerate the shader sources, use copy_if_different
# (per-file, no needless rewrites), and add the files as explicit DEPENDS so
# CMake re-runs the copy whenever ANY shader source changes — even when no
# translation unit recompiled. Belt-and-braces: the pre-golden sha256 fixture
# in tests/ (cd_test_hello_engine_shader_staleness) still asserts source ==
# exe-side before any golden compare runs. The default runtime path stays
# identical (exe-side shaders/), so the chrome golden remains byte-identical.
file(GLOB HELLO_ENGINE_SHADER_SOURCES CONFIGURE_DEPENDS
  ${CMAKE_CURRENT_SOURCE_DIR}/shaders/*.glsl)
add_custom_command(TARGET cd_sample_hello_engine POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory
    $<TARGET_FILE_DIR:cd_sample_hello_engine>/shaders
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    ${HELLO_ENGINE_SHADER_SOURCES}
    $<TARGET_FILE_DIR:cd_sample_hello_engine>/shaders
  DEPENDS ${HELLO_ENGINE_SHADER_SOURCES}
  COMMENT "Stage hello_engine shaders for on-disk hot-reload (copy_if_different)")

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
