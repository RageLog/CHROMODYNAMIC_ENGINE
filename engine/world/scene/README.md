# cd::scene

**Purpose**: scene-graph layer above cd::ecs. Adds parent/child transform hierarchy (LocalTransform + WorldTransform), an entity-name lookup, and the per-frame world-transform propagation pass that resolves the hierarchy into renderable world-space transforms.

**Namespace**: `cd::scene`.

**Headers**: `cd/scene/Scene.hpp` (+ subsystem helpers as the library expands).

**Primary types**:
- `cd::scene::Scene` -- thin wrapper around cd::ecs::World. Tracks parent/child edges + names + a dirty-flag list for incremental hierarchy updates.
- `cd::scene::LocalTransform` -- { position, rotation (quat), scale } component.
- `cd::scene::WorldTransform` -- baked Mat4f component, recomputed from LocalTransform + parent chain each frame.
- `cd::scene::SceneCameraController` -- example controller used by hello_engine sample for the editor-style camera (orbit/pan/zoom).

**Usage**:
```cpp
#include <cd/scene/Scene.hpp>

cd::scene::Scene scene { world };
auto root = scene.create_entity("Root");
auto child = scene.create_entity("Child", root);
scene.local(child)->value.position = { 1, 0, 0 };
scene.update_transforms();
const auto child_world = scene.world(child)->value;
```

**Test command**: `ctest --preset ninja-debug -R cd_test_scene --output-on-failure`.

**Notes**:
- Hierarchy lives next to the ECS World rather than inside it; this keeps cd::ecs ignorant of scene-graph semantics so other consumers (UI tree, command palette outline) can layer their own hierarchies.
- W8-AR ECS PBR sphere grid + 16-sphere demo entities walk through Scene for transform composition.
- hello_engine ties Scene + EditHistory + SelectionState into its World/Project/Level/Layer container (gap #18 / cd::world_container).
