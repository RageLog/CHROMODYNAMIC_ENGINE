# cd::editor::panel::scene_navigator

Search-box + **live-filtered scrollable entity list**. A designer
types a substring → the list narrows in real time (case-insensitive).
`Esc` or `set_filter("")` clears the filter and shows every entity
again.

**Moment**: 500-entity scene, type `enem`, see 12 enemy spawners
instantly.

Sibling library to `cd::editor::panel::scene_tree`, which owns the
**hierarchy** (parent/child) concern. This panel owns the
**search / filter / breadcrumb** concern. Both can be docked side-by-
side; the tree gives spatial navigation, the navigator gives
keyboard-driven retrieval.

## API

```cpp
namespace cd::editor::panel::scene_navigator {

class SceneNavigator
{
public:
    void                                       register_entity(cd::ecs::EntityHandle,
                                                               std::string display_name);
    void                                       set_filter(std::string substring);

    void                                       select(cd::ecs::EntityHandle);
    [[nodiscard]] std::optional<cd::ecs::EntityHandle>
                                               selected() const noexcept;

    void                                       draw(cd::ui::renderer::DrawBatcher&,
                                                    const cd::ui::widgets::Theme&,
                                                    const cd::ui::widgets::Rect&) const;
};

}
```

## Filter algorithm

`set_filter("enem")` triggers an `std::ranges::views::filter` over
the registered (`EntityHandle`, `display_name`) pairs:

```cpp
auto matches = entities | std::views::filter([&](auto& e) {
    return cd::core::ascii_icontains(e.display_name, filter);
});
```

`ascii_icontains` is a `tolower`-on-ASCII substring check —
zero allocations, branch-predictable. The filter recomputes on every
`set_filter` call; with 10 000 entities and a 4-character filter the
walk is sub-millisecond.

Cyrillic / CJK display names: the panel does a byte-wise substring
match (no full Unicode normalisation), which works for the common
case (entity names in ASCII or single-codepoint glyphs) but won't
match canonically-equivalent compositions. The full ICU integration
is queued under `L-i18n-search` and is not in scope for the panel.

## Breadcrumb

When an entity has a parent chain, the panel emits a breadcrumb above
the list:

```
  Scene › Buildings › TempleEast › PressurePlate
```

Click-through on a breadcrumb segment scopes the filter to that
sub-tree (`set_scope(handle)`). `Esc` clears the scope.

## Dependencies

* `cd::core` — `Defines.hpp`, `ascii_icontains`.
* `cd::ecs` — `EntityHandle` (PUBLIC; register API takes it).
* `cd::ui_renderer` — `DrawBatcher` (PUBLIC).
* `cd::ui_widgets` — `Theme` + `Rect`.
