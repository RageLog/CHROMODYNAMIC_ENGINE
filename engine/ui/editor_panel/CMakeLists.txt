# engine/ui/editor_panel -- cd::editor_panel
#
# Composite-pass post-fx preset library.  Headless data + binding
# (CompositePresets.hpp + CompositeFxBinding.hpp) for any consumer;
# optional ImGui-aware button widget header
# (CompositePresetButtonsImGui.hpp) for samples / editor binaries
# that already link cd::imgui_backend.
#
# Header-only INTERFACE target -- no .cpp surface.  ImGui dependency
# is on the consumer side: TUs that include the ImGui buttons header
# must transitively link cd::imgui_backend.
#
# Marathon Run 23 phase N5A+N5B.
cd_add_library(editor_panel
  TYPE INTERFACE
  PUBLIC_DEPS cd::core
)

if(CD_ENABLE_TESTING)
  add_subdirectory(tests)
endif()
