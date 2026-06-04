# engine/ui/editor/panel_scene_navigator — cd::editor_panel_scene_navigator
#
# phase687 — Scene Navigator panel library.
# Provides cd::editor::panel::scene_navigator::SceneNavigator (DrawBatcher path).
#
# Search box at the top + live-filtered scrollable entity list below.
# Designer types a substring → list narrows in real time (case-insensitive).
# Esc / set_filter("") clears the filter and shows all entities again.
#
# Sibling to scene_tree (parent/child hierarchy); this panel owns the
# SEARCH / FILTER / BREADCRUMB concern.
#
# MOMENT: 500-entity scene, type 'enem', see 12 enemy spawners instantly.
#
# Targets:
#   cd_editor_panel_scene_navigator   (real target)
#   cd::editor_panel_scene_navigator  (alias)

cd_add_library(editor_panel_scene_navigator
  TYPE STATIC
  SOURCES
    src/SceneNavigator.cpp
  PUBLIC_DEPS
    cd::core
    cd::ecs           # Entity handle
    cd::ui_renderer   # DrawBatcher
    cd::ui_widgets    # Theme + Rect
)

if(CD_ENABLE_TESTING)
  add_subdirectory(tests)
endif()
