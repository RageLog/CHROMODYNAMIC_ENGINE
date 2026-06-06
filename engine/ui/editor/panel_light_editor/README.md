# cd::editor::panel::light_editor

Light editor panel. Two sections:

1. **Point-light list** — one row per registered light with position,
   radius, intensity, and a colour swatch.
2. **Cluster grid stats strip** — `x / y / z` cell counts and the
   `near_z / far_z` froxel range used by `cd::lighting_clusters`.

Click-to-select highlighting lets a designer pick a light row; the
selected `LightId` is queryable so the gizmo / property editor can
follow along.

## API

```cpp
namespace cd::editor::panel::light_editor {

using LightId = std::uint32_t;

class LightEditor
{
public:
    void                          register_light(LightId,
                                                 cd::lighting_clusters::PointLight,
                                                 cd::math::Vec3f colour,
                                                 float intensity);
    void                          set_cluster_grid(cd::lighting_clusters::ClusterGrid);

    void                          select(LightId);
    [[nodiscard]] std::optional<LightId>  selected() const noexcept;

    void                          draw(cd::ui::renderer::DrawBatcher&,
                                       const cd::ui::widgets::Theme&,
                                       const cd::ui::widgets::Rect&) const;
};

}
```

## Layout

```
  ┌───────────────────────────────────────────────────┐
  │ Lights (12 active)                                │
  │ ─────────────────────────────────────────────     │
  │ ● spot_01     (0.0, 2.0,  0.0)  r=8  i=1500 lux  │ ◄ selected
  │ ● point_02    (3.2, 1.5, -1.7)  r=4  i=600  lux  │
  │ ● point_03    (...)                              │
  │ ─────────────────────────────────────────────     │
  │ Cluster grid: 16 × 9 × 24  near=0.05  far=80     │
  └───────────────────────────────────────────────────┘
```

The colour swatch is drawn as a tiny coloured quad at the start of
each row, using `theme.text` as the border colour for contrast.

## Dependencies

* `cd::core` — `Defines.hpp`.
* `cd::lighting_clusters` — `PointLight` + `ClusterGrid` type
  vocabulary (PUBLIC).
* `cd::ui_renderer` — `DrawBatcher` (PUBLIC).
* `cd::ui_widgets` — `Theme` + `Rect`.

`cd::lighting_clusters` is a PUBLIC dep because the registration
API takes its types directly. Consumers that want the panel
implicitly link to clusters; this is intentional — the two
libraries are co-versioned and the panel cannot show useful state
without the cluster vocabulary.
