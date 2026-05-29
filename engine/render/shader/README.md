# cd::shader

**Purpose**: GLSL/HLSL to SPIR-V compilation with optional cache + hot-reload file watch. Provides the `ICompiler` interface every material pipeline uses to translate source shader strings into the SPIR-V the RHI backend consumes.

**Namespace**: `cd::shader`.

**Headers**: `cd/shader/{Compiler,CachedCompiler,FileWatcher,ShaderStage}.hpp`.

**Primary types**:
- `cd::shader::ICompiler` -- abstract compiler interface. `compile(source, stage, entry, defines) -> Result<SpirvBlob>`.
- `cd::shader::make_glslang_compiler()` -- factory for the production glslang-backed compiler.
- `cd::shader::CachedCompiler` -- wraps an ICompiler with content-hash keyed disk cache; second compile of same source is sub-millisecond.
- `cd::shader::FileWatcher` -- cross-platform file-mtime watcher driving hot-reload at the editor surface.
- `cd::shader::ShaderStage` -- enum class (kVertex / kFragment / kCompute / kRayGen / kClosestHit / ...).

**Usage**:
```cpp
#include <cd/shader/Compiler.hpp>

auto compiler = cd::shader::make_glslang_compiler();
if (compiler == nullptr) return 1;

auto blob_r = compiler->compile(kVertexSrc,
                                cd::shader::ShaderStage::kVertex,
                                "main", {});
if (!blob_r.has_value()) return 2;
```

**Test command**: `ctest --preset ninja-debug -R cd_test_shader --output-on-failure`.

**Notes**:
- glslang is vendored under `Dependencies/`; ICompiler is the seam that lets us swap to DXC / shaderc / runtime SPV-Tools later without touching call sites.
- CachedCompiler is opt-in; production engine binds it at boot. Sample apps usually use the raw glslang compiler for simplicity.
- FileWatcher is a future hook for shader-on-disk hot-reload (Phase 2 / X5 per CHROMODYNAMIC roadmap).
