# cd::editor::panel::ik_chain_editor

**2-D side-view projection** of a `cd::animation::ik::IkChain`.
Coloured joint circles + bone-line connections + target X marker +
convergence indicator + click-to-select per joint.

**Moment**: an animator drops a 5-joint leg chain + a ground-target,
sees the CCD solve converge to plant the foot on contact — visual
debugging without launching the game.

## Visual key

| Element              | Colour                          |
|----------------------|---------------------------------|
| Root joint           | blue                            |
| Middle joints        | white                           |
| End-effector joint   | `accent_success` (green)        |
| Bone lines           | `theme.text_dim`                |
| Target X marker      | `accent_warning` (orange)       |
| Selected joint ring  | `theme.accent` outline          |
| Convergence ✓        | `accent_success` green check    |
| Non-convergence ✗    | `accent_error` red cross        |

## API

```cpp
namespace cd::editor::panel::ik_chain_editor {

using JointIndex = uint32_t;

class IkChainEditor
{
public:
    void                          set_chain(std::span<const cd::animation::ik::Joint>);
    void                          set_target(cd::math::Vec3f world);
    void                          set_result(cd::animation::ik::IkResult);

    void                          simulate_click(cd::math::Vec2f screen_xy);
    [[nodiscard]] std::optional<JointIndex>
                                  selected_joint() const noexcept;

    void                          draw(cd::ui::renderer::DrawBatcher&,
                                       const cd::ui::widgets::Theme&,
                                       const cd::ui::widgets::Rect&) const;
};

}
```

## Projection

Side-view projection drops the Z axis:

```
  screen_xy = world_xy * scale + offset;
```

The auto-fit on first `draw()` chooses `scale` so the chain's AABB
+ target fit inside the panel bounds with a 10 px margin. Pan / zoom
is queued for Sprint-2.

## Convergence

The `IkResult::converged` flag drives the corner indicator. The
overlay shows additionally the `residual_metres` so the animator
can see how close the solver got:

```
  ✓  converged   residual 0.003 m
```

vs

```
  ✗  not converged   residual 0.412 m
```

For the non-convergence case, the chain still draws — the animator
needs to see WHERE the solver gave up to debug joint limits, chain
length, etc.

## Dependencies

* `cd::core` — `Defines.hpp`.
* `cd::animation_ik` — `Joint`, `IkResult`, `IkChain` (PUBLIC,
  header use).
* `cd::ui_renderer` — `DrawBatcher` (PUBLIC).
* `cd::ui_widgets` — `Theme` + `Rect`.
