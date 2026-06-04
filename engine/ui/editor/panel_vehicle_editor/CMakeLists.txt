# engine/ui/editor/panel_vehicle_editor — cd::editor_panel_vehicle_editor
#
# phase676 — Vehicle Editor panel library.
# Provides cd::editor::panel::vehicle_editor::VehicleEditor (DrawBatcher path).
# 3 sections (engine / wheels / chassis) + live state readout:
#   * Engine: max torque + 6 gear-ratio strips + idle/max RPM.
#   * Wheels: 4 rows (FL/FR/RL/RR) showing radius / mass / suspension stiffness.
#   * Chassis: mass strip + dimensions trio (length / width / height).
#   * Live state: speed_kph / rpm / gear / throttle / brake / steer bars.
#
# Targets:
#   cd_editor_panel_vehicle_editor   (real target)
#   cd::editor_panel_vehicle_editor  (alias)

cd_add_library(editor_panel_vehicle_editor
  TYPE STATIC
  SOURCES
    src/VehicleEditor.cpp
  PUBLIC_DEPS
    cd::core
    cd::physics_vehicle      # VehicleConfig + VehicleState
    cd::ui_renderer          # DrawBatcher
    cd::ui_widgets           # Theme + Rect
)

if(CD_ENABLE_TESTING)
  add_subdirectory(tests)
endif()
