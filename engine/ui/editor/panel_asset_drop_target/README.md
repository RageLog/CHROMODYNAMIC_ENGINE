# cd::editor::panel::asset_drop_target

Drop-zone widget. Renders a dashed-border rectangle labelled "Drop
assets here", filters incoming paths by an extension allow-list, and
surfaces accepted paths to the caller through `consume_dropped_path()`.

A headless `simulate_drop()` lets unit tests exercise the filter +
consumption flow without a real platform drag-and-drop integration.

## API

```cpp
namespace cd::editor::panel::asset_drop_target {

class AssetDropTarget
{
public:
    void                          set_label(std::string);
    void                          set_accepted_extensions(std::vector<std::string>); // ".png", ".gltf"

    // Platform integration: editor's OS-level drop callback funnels here.
    void                          on_drop(std::filesystem::path);

    // Headless harness used by tests + simulate_drop interactive demos.
    void                          simulate_drop(std::filesystem::path);

    // Caller drains the accepted queue once per frame.
    [[nodiscard]] std::optional<std::filesystem::path>
                                  consume_dropped_path();

    void                          draw(cd::ui::renderer::DrawBatcher&,
                                       const cd::ui::widgets::Theme&,
                                       const cd::ui::widgets::Rect&) const;
};

}
```

## Filter contract

`on_drop(path)` / `simulate_drop(path)`:

1. Lowercase the path's extension.
2. Linear scan `accepted_extensions` — first match accepts.
3. Accepted paths push to an internal FIFO; `consume_dropped_path()`
   drains.

Paths whose extension does not match are **silently dropped**. The
editor reports rejected drops through the regular status-bar log, not
this panel — the panel's contract is "I am a queue endpoint for
accepted paths".

## Layout

```
  ┌╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴┐
  ╷                                       ╷
  ╷             ↓                         ╷
  ╷      Drop assets here                 ╷
  ╷    (.png .jpg .gltf .glb .ktx2)       ╷
  ╷                                       ╷
  ╵                                       ╵
  └╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴┘
```

Dashed border is rendered as repeated short quads with theme accent
colour. While a drag is hovering the panel, the editor calls a
future `set_hover_active(true)` API to switch the border to
`accent_success` — currently a planned Sprint-2 addition.

## Dependencies

* `cd::core` — `Defines.hpp`.
* `cd::ui_renderer` — `DrawBatcher` (PUBLIC).
* `cd::ui_widgets` — `Theme` + `Rect`.
