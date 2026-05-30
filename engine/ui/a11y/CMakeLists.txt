# engine/ui/a11y -- Phase 478 cd::ui::a11y accessibility baseline
#
# cd::ui_a11y -- parallel accessibility tree mirroring cd::ui::Widget,
# WCAG 2.1 contrast checker, focus indicator rect, role taxonomy, and
# tab-navigation contract. ADR-20260530 Phase 4.4 deliverable.
#
# Dependencies: cd::core (Defines), cd::ui (Widget / Rect / WidgetId).
# Deliberately NOT cd::ui_theme -- the contrast formula is duplicated
# here so audit-only / headless tooling can pull a11y without dragging
# the design-token system in.

cd_add_library(ui_a11y
  TYPE STATIC
  SOURCES
    src/A11y.cpp
  PUBLIC_DEPS
    cd::core
    cd::ui
)

if(CD_ENABLE_TESTING)
  add_subdirectory(tests)
endif()
