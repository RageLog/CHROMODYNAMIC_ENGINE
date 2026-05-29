# cd::editor

**Purpose**: In-engine editor framework. Provides a scene hierarchy view, property drawers, undo/redo history, command palette, gizmos (translate/rotate/scale), selection outlining, bookmarking, and preferences persistence. Designed to work alongside the live game runtime (editor can pause/resume game).

**Namespace**: `cd::editor`.

**Public Headers**:
- `cd/editor/Editor.hpp` — main editor aggregator; owns all sub-panels.
- `cd/editor/HierarchyView.hpp` — scene tree (entity parent-child explorer).
- `cd/editor/PropertyDrawer.hpp` — generic property inspector (reflect types to UI).
- `cd/editor/AxisGizmo.hpp` — 3D translate/rotate/scale manipulator.
- `cd/editor/SelectionSet.hpp` — current selection (entities + components).
- `cd/editor/SelectionOutline.hpp` — GPU-rendered outline effect (selected entities glow).
- `cd/editor/EditHistory.hpp` — undo/redo stack; command-pattern recording.
- `cd/editor/TransformCommands.hpp` — undoable move/rotate/scale commands.
- `cd/editor/CommandPalette.hpp` — searchable action menu (Ctrl+P).
- `cd/editor/MenuBar.hpp` — top-level menu (File, Edit, View, etc.).
- `cd/editor/Bookmark.hpp` — saved camera position snapshots (return to named views).
- `cd/editor/PreferencesStore.hpp` — editor settings (layout, theme, tool preferences).

**Build**:
```bash
cmake --build --preset ninja-debug --target cd_editor
ctest --preset ninja-debug -R editor --output-on-failure
```

**Dependencies**: cd::editor_ui, cd::ui, cd::scene, cd::ecs, cd::input, cd::camera, cd::math, cd::core.

**Integration Points**:
- **ECS**: directly manipulates entity components via cd::ecs reflection.
- **Scene**: hierarchy view navigates the scene graph.
- **Renderer**: selection outline rendered as additional pass in composite.
- **Input**: gizmo interaction via mouse/keyboard bindings.

**Notes**:
- Editor UI lives in cd::editor_ui (ImGui panels, property widget library).
- Game can run while editor is open (pause game to edit, resume to test).
- All editor operations are undoable (EditHistory records commands).
- Gizmo interaction threadsafe via job-queue command recording.
