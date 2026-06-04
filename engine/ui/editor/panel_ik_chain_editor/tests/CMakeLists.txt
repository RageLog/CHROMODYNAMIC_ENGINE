cd_add_test(editor_panel_ik_chain_editor
  SOURCES test_ik_chain_editor.cpp
  DEPS
    cd::editor_panel_ik_chain_editor
    cd::anim_ik
    cd::ui_renderer
    cd::ui_widgets
)
