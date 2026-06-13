# T3.2 / phase529-hello-editor-sample
# Scene-tree (HierarchyView) + inspector + console + viewport demo.
# Depends on cd::asset_gltf and cd::render_scene_ingest for the phase 508
# generic glTF load path (HelloGenericGltf.hpp shared from hello_engine).
cd_add_sample(hello_editor
  SOURCES main.cpp
  DEPS
    cd::debug_draw
    cd::debug_line
    cd::core
    cd::math
    cd::ecs
    cd::light
    cd::world_container
    cd::scene
    cd::editor
    cd::editor_panel_inspector
    cd::editor_panel_console
    cd::editor_panel_material_editor
    cd::editor_panel_animator
    cd::asset_json
    cd::asset_gltf
    cd::imgui_backend
    cd::platform
    cd::render
    cd::render_scene_ingest
    cd::material
    cd::shader
    cd::rhi
    cd::rhi_vulkan
    cd::sample_common
)

# Expose the hello_engine header directory so main.cpp can include
# HelloGenericGltf.hpp via the relative path
# "../../engine/hello_engine/HelloGenericGltf.hpp". This avoids copying
# the header and keeps the phase 508 load path as a single canonical file.
target_include_directories(cd_sample_hello_editor PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../../engine/hello_engine
)

# Stage the Generic glTF drop-in directory next to the exe so
# HelloGenericGltf's candidate-path resolver finds it at runtime.
add_custom_command(TARGET cd_sample_hello_editor POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory
    $<TARGET_FILE_DIR:cd_sample_hello_editor>/assets/samples/Generic
  COMMENT "Create hello_editor generic-gltf drop-in directory"
)

add_subdirectory(tests)
