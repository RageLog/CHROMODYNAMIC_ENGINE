# samples/editor — editor + hot-reload + runtime stack (Vulkan-gated).
#
# phase903-sample-consolidation: hello_runtime integration smoke
# folded into engine/runtime/tests/test_engine_context.cpp (9 cases
# cover CVars + VFS + Config + threadpool + math glue surface).
# Source directory deleted in the same commit.
#
# phase907 audit (consolidation feedback category review):
# All 5 retained editor samples are INTERACTIVE Vulkan+ImGui visual
# demos (330-1046 lines each). They are category (b) per
# [[feedback-sample-consolidation-and-per-feature-demo]] — should
# eventually fold into hello_engine as panel toggles. The fold is
# deferred until hello_engine grows an Editor Panels collapsing
# header (small risk: each panel currently expects its own swap-
# chain bring-up). Keeping them as standalone exes meanwhile.
add_subdirectory(hello_editor)
add_subdirectory(hello_hot_reload)
add_subdirectory(hello_material_editor)  # phase567: ImGui-free cd::ui_* material editor panel sample
add_subdirectory(hello_animator)         # phase591: ImGui-free cd::editor::panel::animator sample
add_subdirectory(hello_behavior_designer)  # phase592: demos cd::editor::panel::behavior_designer
