# cd::editor::panel::material_editor

Material parameter editor panel. Surfaces base-color / metallic /
roughness for the bound material, draws a live preview rectangle in
the current material's colour tint, and accepts a single
`MaterialId` via `set_material_id`. Renders into
`cd::ui::renderer::DrawBatcher`.

## API

```cpp
namespace cd::editor::panel::material_editor {

using MaterialId = std::uint32_t;

struct MaterialView                   // POD snapshot read by the panel
{
    cd::math::Vec3f   base_color;
    float             metallic;
    float             roughness;
    float             normal_strength;
};

class MaterialEditor
{
public:
    void                          register_material(MaterialId, std::string display_name);
    void                          set_material_id(MaterialId);
    void                          set_view(MaterialId, MaterialView);

    void                          draw(cd::ui::renderer::DrawBatcher&,
                                       const cd::ui::widgets::Theme&,
                                       const cd::ui::widgets::Rect&) const;
};

}
```

## Layout

```
  ┌────────────────────────────────────────────┐
  │ Material: PBR M0R0  (chrome mirror)        │
  ├────────────────────────────────────────────┤
  │ Base Color    ████████  (tint preview)     │
  │ Metallic     [|||||||●--]   1.00           │
  │ Roughness    [●----------]  0.04           │
  │ Normal Str.  [|||●-------]  0.30           │
  └────────────────────────────────────────────┘
```

## Read-only contract

The panel is a **view-only** UI surface — `set_view()` updates the
snapshot, `draw()` emits the visualisation. The panel does NOT mutate
the material itself; the editor wires slider drag events to the
material backend through whatever path it normally uses
(`cd::asset::material_authoring` + hot-reload, for example).

## Dependencies

* `cd::core` — `Defines.hpp`.
* `cd::ui_renderer` — `DrawBatcher` (PUBLIC).
* `cd::ui_widgets` — `Theme` + `Rect`.

Does **not** link to `cd::material` — material values arrive through
the generic `MaterialView` POD so the panel stays decoupled from the
render-tier material library.
