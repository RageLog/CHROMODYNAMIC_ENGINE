# cd::world_container

## Purpose
Foundation types for multi-project, multi-level hierarchy: World (persistent scene root), Project (edit-time collection), Level (streamable unit), and Layer (organizational grouping). Designed to integrate with editor (v1.6) and streaming system (v1.7).

## Namespace
`cd::<world>::world_container::`

## Public headers
- `include/cd/world_container/World.hpp` — Root world entity and update context
- `include/cd/world_container/Project.hpp` — Edit-time project descriptor
- `include/cd/world_container/Level.hpp` — Streamable level unit with bounds and visibility
- `include/cd/world_container/Layer.hpp` — Organizational layer for filtering and bulk operations

## Primary types
- `World::Container` — Root holds project + current level + layer masks
- `Project::Descriptor` — Metadata (name, path, author, version)
- `Level::Streamable` — Bounds, asset references, load state
- `Layer::Mask` — Bit field for per-layer visibility/collision toggling

## Usage example
```cpp
#include <cd/world_container/World.hpp>

// Create world hierarchy.
auto world = cd::world_container::World::create("main_game");

auto project = world->create_project("project_city");
auto level = project->create_level("level_downtown", world_bounds);

// Layer-based visibility.
auto gameplay_layer = level->create_layer("gameplay_entities");
auto decor_layer = level->create_layer("decorative_foliage");

// Editor can toggle layers without ECS query.
level->set_layer_visible(decor_layer, /*visible*/ false);

// Streaming system loads/unloads levels by bounds.
world->update(camera_frustum, delta_time);
```

## Build/Test
```bash
cmake --build --preset ninja-debug --target cd_world_container
ctest --preset ninja-debug -R world_container
```

## Dependencies
- `cd::core` — engine types
- `cd::math` — vector/matrix math (bounds)

## References
- Unreal Engine World/Level/Layer model (reference design pattern)
- Architecture: `docs/ADR/ADR-*-world-*.md`

## Notes
- Header-only INTERFACE library.
- Designed for integration with ECS (cd::ecs entities belong to layers).
- Streaming system (v1.7) queries level bounds for persistence prediction.
- Editor integration (v1.6) uses layers for viewport filtering and bulk property edits.
