# cd::editor::panel::material_preview

Material preview panel. Sprint-1 ships a **2-D swatch + parameter
bars** that read the bound material's base colour, metallic /
roughness, and alpha cutoff. Sprint-2 promotes the swatch to a
**256×256 RT-rendered PBR sphere** when the editor binds a
preview texture.

## Sprints

| Sprint | Phase | Surface |
|--------|-------|---------|
| 1 | `phase678` | 2-D swatch + metallic / roughness / alpha bars + texture path pills. |
| 2 | `phase738` | Optional 256×256 PBR RT path via `set_preview_texture()`. |

## Sprint-1 layout

```
  ┌──────────────────────────────────────────┐
  │ Material Preview                          │
  ├──────────────────────────────────────────┤
  │                                          │
  │      ████   (base-color swatch)          │
  │                                          │
  │  Metallic   [|||||||||●--]               │
  │  Roughness  [●----------]                │
  │  Alpha-cut  [|●---------]   (kMask only) │
  │                                          │
  │  📁 albedo:  textures/sandstone.png      │
  │  📁 normal:  textures/sandstone_n.png    │
  │  📁 mr:      textures/sandstone_mr.png   │
  └──────────────────────────────────────────┘
```

## Sprint-2 RT path

```cpp
namespace cd::editor::panel::material_preview {

class MaterialPreview
{
public:
    void                          set_material(/* MaterialView */);
    void                          set_preview_texture(cd::rhi::TextureHandle);

    // Sprint-2 invalidation flow
    [[nodiscard]] bool            is_dirty() const noexcept;
    void                          clear_dirty();

    void                          draw(cd::ui::renderer::DrawBatcher&,
                                       const cd::ui::widgets::Theme&,
                                       const cd::ui::widgets::Rect&) const;
};

}
```

When `set_preview_texture(handle)` is called, `draw()` emits a
`DrawBatcher::textured_quad` over the swatch area instead of a
solid-colour quad. The texture is **owned by the caller** (the
editor's RT job that rendered the PBR sphere off-screen); the panel
holds a handle copy only.

## Dirty cache

`is_dirty()` returns true when the bound `MaterialView` fingerprint
changes (any field different from the last `clear_dirty()` call).
The editor's RT job checks this on every frame and re-renders the
256×256 sphere only when dirty — saves ~1 ms / frame when the user
isn't actively dragging sliders.

```cpp
per_frame([&] {
    if (panel.is_dirty())
    {
        render_pbr_sphere_to(preview_rt);     // editor's RT job
        panel.set_preview_texture(preview_rt.handle);
        panel.clear_dirty();
    }
    panel.draw(batcher, theme, panel_bounds);
});
```

## Dependencies

* `cd::core` — `Defines.hpp`.
* `cd::ui_renderer` — `DrawBatcher` (PUBLIC).
* `cd::ui_widgets` — `Theme` + `Rect`.
* `cd::rhi` — `TextureHandle` for Sprint-2 (PUBLIC, header use).
