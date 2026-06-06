# cd::editor::panel::vehicle_editor

Vehicle editor panel for `cd::physics::vehicle`. Three configuration
sections plus a live state readout.

## Sections

| Section  | Contents                                                  |
|----------|-----------------------------------------------------------|
| Engine   | max torque + 6 gear-ratio strips + idle / max RPM.        |
| Wheels   | 4 rows (FL / FR / RL / RR): radius / mass / suspension stiffness. |
| Chassis  | mass strip + dimensions trio (length / width / height).   |
| State    | speed_kph / rpm / gear / throttle / brake / steer bars.   |

## API

```cpp
namespace cd::editor::panel::vehicle_editor {

class VehicleEditor
{
public:
    void                          set_config(cd::physics::vehicle::VehicleConfig);
    void                          set_state(cd::physics::vehicle::VehicleState);

    void                          draw(cd::ui::renderer::DrawBatcher&,
                                       const cd::ui::widgets::Theme&,
                                       const cd::ui::widgets::Rect&) const;
};

}
```

## Layout

```
  ┌──────────────────────────────────────────────────────────────┐
  │ Engine                                                       │
  │   max torque   [████████████████]  450 Nm                    │
  │   gear  1:3.83 | 2:2.36 | 3:1.69 | 4:1.31 | 5:1.00 | 6:0.85  │
  │   rpm   idle 800       max 7200                              │
  ├──────────────────────────────────────────────────────────────┤
  │ Wheels                  radius   mass   suspension           │
  │   FL                    0.32 m   18 kg  [████████░░]         │
  │   FR                    0.32 m   18 kg  [████████░░]         │
  │   RL                    0.34 m   20 kg  [██████████]         │
  │   RR                    0.34 m   20 kg  [██████████]         │
  ├──────────────────────────────────────────────────────────────┤
  │ Chassis                                                      │
  │   mass  [████████████░░░░░] 1450 kg                          │
  │   L 4.32 m   W 1.80 m   H 1.42 m                             │
  ├──────────────────────────────────────────────────────────────┤
  │ State                                                        │
  │   speed   [██████░░░░░░] 78 km/h                             │
  │   rpm     [████████░░░░] 4200                                │
  │   gear    3                                                  │
  │   throttle [████████████]  brake [░░░░░░]  steer [░░░░░░░]   │
  └──────────────────────────────────────────────────────────────┘
```

## View-only contract

`VehicleEditor` is a **view-only** surface — `set_config()` and
`set_state()` push snapshots; `draw()` paints them. The panel does
not mutate the `Vehicle`. Slider drag handlers route through the
editor's command pipeline; this panel only displays the result.

## Dependencies

* `cd::core` — `Defines.hpp`.
* `cd::physics_vehicle` — `VehicleConfig` + `VehicleState` (PUBLIC).
* `cd::ui_renderer` — `DrawBatcher` (PUBLIC).
* `cd::ui_widgets` — `Theme` + `Rect`.
