# samples/ — runnable demos per phase milestone
# Sprint S2.0: hello_core (cd::core link + Result roundtrip)
# Sprint S2.10: hello_triangle (RHI Vulkan MVP)
# Sprint S2.20: hello_asset (gltf import → cdmesh)
# Sprint S2.24: hello_scene (end-to-end Phase 2 demo)

# samples/common/ is a header-only helper folder (SampleRuntime.hpp etc.).
# Inject its include path via an INTERFACE library so every cd_sample_*
# target picks it up without each CMakeLists having to repeat the path.
add_library(cd_sample_common INTERFACE)
target_include_directories(cd_sample_common INTERFACE
  ${CMAKE_CURRENT_SOURCE_DIR}/common
)
# GoldenCapture.hpp pulls in cd::asset_image (PNG write+read), cd::imgdiff
# (compare/highlight), and the RHI command-buffer layer (image-to-buffer
# copy + barriers). Samples consuming the golden helper inherit all of
# these transitively via cd::sample_common.
target_link_libraries(cd_sample_common INTERFACE
  cd::asset_image
  cd::imgdiff
  cd::rhi
)
add_library(cd::sample_common ALIAS cd_sample_common)

add_subdirectory(hello_core)
add_subdirectory(hello_handle)
add_subdirectory(hello_foundation)
add_subdirectory(hello_runtime)
add_subdirectory(hello_ecs)
add_subdirectory(hello_scheduler)
add_subdirectory(hello_bench)
add_subdirectory(hello_json)
add_subdirectory(hello_scene_save)
add_subdirectory(hello_audio_synth)
add_subdirectory(hello_asset_registry)
add_subdirectory(hello_audio_play)
add_subdirectory(hello_audio_wasapi)
add_subdirectory(hello_udp)
add_subdirectory(hello_watch)
add_subdirectory(hello_events)
add_subdirectory(hello_flip)
add_subdirectory(hello_script)
add_subdirectory(hello_positional_audio)
add_subdirectory(hello_clustered_lights)
add_subdirectory(hello_compute_cluster)

# hello_d3d12_boot exercises the Phase 12.B D3D12 backend boot path.
# Windows-only because cd::rhi_d3d12 only links on Windows; on other
# platforms the engine ships the same library but its factory returns
# kBackendInitFailed. Gate the sample so non-Windows builds skip it.
if(WIN32)
  add_subdirectory(hello_d3d12_boot)
  add_subdirectory(hello_d3d12_clear)
  add_subdirectory(hello_d3d12_triangle)
endif()

# hello_replication is cross-platform (loopback connection, no real socket).
add_subdirectory(hello_replication)

# hello_editor wires cd::editor::EditHistory + TransformCommands into
# an ImGui-driven inspector. Gated on CD_ENABLE_VULKAN because it
# rides the cd::imgui_backend Vulkan path (the cd::ui-based
# cd::editor stack remains available for headless test coverage).
if(CD_ENABLE_VULKAN)
  add_subdirectory(hello_editor)
endif()

# hello_triangle requires the Vulkan backend; gate so the rest of the
# engine still builds when CD_ENABLE_VULKAN=OFF.
if(CD_ENABLE_VULKAN)
  add_subdirectory(hello_triangle)
  add_subdirectory(hello_mesh)
  add_subdirectory(hello_cube)
  add_subdirectory(hello_texture)
  add_subdirectory(hello_gltf)
  add_subdirectory(hello_obj)
  add_subdirectory(hello_cooked)
  add_subdirectory(hello_scene_graph)
  add_subdirectory(hello_framegraph)
  add_subdirectory(hello_hot_reload)
  add_subdirectory(hello_textured_cooked)
  add_subdirectory(hello_imgui)
  add_subdirectory(hello_inspector)
  add_subdirectory(hello_pbr)
  add_subdirectory(hello_anim)
  add_subdirectory(hello_skybox)
  add_subdirectory(hello_render_thread)
  add_subdirectory(hello_gpu_cluster)
  # Phase 12.D capability-detection sample.
  add_subdirectory(hello_rhi_features)
endif()
