# engine/ui/editor/panel_ik_chain_editor — cd::editor_panel_ik_chain_editor
#
# phase710 — IK Chain Editor panel library.
# Provides cd::editor::panel::ik_chain_editor::IkChainEditor (DrawBatcher path).
# 2D side-view projection of a cd::animation::ik::IkChain:
#   * Coloured joint circles (root=blue, mid=white, end=success-green).
#   * Bone-line connections in chain order.
#   * Target X marker (accent_warning orange).
#   * Convergence indicator (green check / red cross) based on IkResult.
#   * Selected joint ring (accent outline) via simulate_click hit-test.
#
# MOMENT: animator drops a 5-joint leg chain + ground-target, sees the IK
# solve converge to plant the foot — visual debugging without launching game.
#
# Targets:
#   cd_editor_panel_ik_chain_editor   (real target)
#   cd::editor_panel_ik_chain_editor  (alias)

cd_add_library(editor_panel_ik_chain_editor
  TYPE STATIC
  SOURCES
    src/IkChainEditor.cpp
  PUBLIC_DEPS
    cd::core
    cd::anim_ik          # IkChain / IkResult / Joint
    cd::ui_renderer      # DrawBatcher
    cd::ui_widgets       # Theme + Rect
)

if(CD_ENABLE_TESTING)
  add_subdirectory(tests)
endif()
