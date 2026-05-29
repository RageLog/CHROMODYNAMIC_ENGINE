# cd::editor_ui

## Purpose
Editor-specific widget library building on cd::ui: scene/asset browsers, property inspectors, ECS entity editors, and command palette integration. Binds game systems (ECS, scene graph, rendering) to UI widgets for in-engine editing.

## Namespace
`cd::<ui>::editor_ui::`

## Public headers
- `include/cd/editor_ui/EditorWidgets.hpp` — Specialized inspector widgets (Transform, Light, Material)
- `include/cd/editor_ui/AssetPalette.hpp` — Filterable asset browser with thumbnail preview
- `include/cd/editor_ui/EscChain.hpp` — Escape key dismiss chain (exit search, deselect, etc.)

## Primary types
- `EditorUi::TransformWidget` — Gizmo + numeric fields for Entity translation/rotation/scale
- `EditorUi::AssetBrowser` — File tree + search + drag-drop to scene
- `EditorUi::PropertyInspector` — Reflection-driven component editor

## Usage example
```cpp
#include <cd/editor_ui/EditorWidgets.hpp>

// Create inspector for selected entity.
auto inspector = cd::editor_ui::PropertyInspector::for_entity(
  selected_entity, ecs_registry
);

// Bind to UI layer.
editor_panel->add_child(inspector);

// Changes reflect back to ECS on commit.
inspector->on_property_changed([&](const auto& change) {
  ecs_registry.patch(change.entity_id, change.component);
});
```

## Build/Test
```bash
cmake --build --preset ninja-debug --target cd_editor_ui
ctest --preset ninja-debug -R editor_ui
```

## Dependencies
- `cd::ui` — base widget framework
- `cd::scene` — scene graph traversal
- `cd::ecs` — entity component system
- `cd::math` — vector/matrix for gizmo
- `cd::core` — engine types

## References
- Unity Editor Inspector pattern
- Unreal Slate UI framework (widget composition model)

## Notes
- STATIC library (stateful editor widgets).
- Integrates with cd::ecs reflection for auto-property editing.
- Supports multi-selection and batch editing.
- Undo/redo handled by editor application, not the widgets.
