# cd::editor_cdproj

**Purpose**: .cdproj project-file read/write library. Handles serialization and deserialization of CHROMODYNAMIC project metadata (scenes, assets, settings, build configuration).

**Namespace**: `cd::editor::cdproj`.

**Headers**: `cd/editor/cdproj/ProjectFile.hpp` and related IO types.

**Primary types**:
- `cd::editor::cdproj::ProjectFile` -- root project document type with version, metadata, scene list.
- Supporting types for scene references, asset paths, build config.

**Standalone library**: Depends on `cd::core` only. No UI or rendering dependencies, making it usable by both CLI tools (batch asset conversion, project validation) and the editor GUI.

**Usage example**:
```cpp
#include <cd/editor/cdproj/ProjectFile.hpp>

cd::editor::cdproj::ProjectFile proj;
proj.load("MyProject.cdproj");
for (const auto& scene : proj.scenes()) {
  // Process scene references
}
```

**Test command**: `ctest --preset ninja-debug -R cd_test_editor_cdproj --output-on-failure`.

**Notes**:
- Phase 547: Extracted as standalone library per modular design.
- Pure data serialization; no domain logic.

**TODO**: expand coverage (currently <3 test cases).
