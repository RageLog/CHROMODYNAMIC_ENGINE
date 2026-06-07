# samples/editor — editor + hot-reload + runtime stack (Vulkan-gated).
#
# phase903-sample-consolidation: hello_runtime integration smoke
# folded into engine/runtime/tests/test_engine_context.cpp (9 cases
# cover CVars + VFS + Config + threadpool + math glue surface).
# Source directory deleted in the same commit.
add_subdirectory(hello_editor)
add_subdirectory(hello_hot_reload)
add_subdirectory(hello_material_editor)  # phase567: ImGui-free cd::ui_* material editor panel sample
add_subdirectory(hello_animator)         # phase591: ImGui-free cd::editor::panel::animator sample
add_subdirectory(hello_behavior_designer)  # phase592: demos cd::editor::panel::behavior_designer
