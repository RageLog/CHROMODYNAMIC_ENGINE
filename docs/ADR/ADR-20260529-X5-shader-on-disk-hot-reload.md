# ADR-20260529-X5-shader-on-disk-hot-reload

Date: 2026-05-29 (Marathon Run 17, Mega-Marathon Milestone M1).

## Context

Up through Run 16, all GLSL shader source for `hello_engine` (and most other samples) lived as embedded `inline constexpr const char*` strings inside C++ headers (`samples/engine/hello_engine/PrimShader.hpp` + `PrimShader_kPrimFS.inl` + `PrimShader_kShadow.inl`, ~990 lines of GLSL). The `cd::material::Material::create` API accepts inline GLSL via `MaterialDesc::vertex_glsl` / `fragment_glsl` and routes through the existing `cd::shader::ICompiler` (glslang) abstraction to SPIR-V.

The infrastructure to escape this pattern was already partially in place:

- `cd::shader::FileWatcher` (polling watcher, portable across Win / Linux / macOS) exists at `engine/render/shader/include/cd/shader/FileWatcher.hpp`.
- `cd::shader::ICompiler` accepts arbitrary `source` strings — it does not care whether the GLSL came from an embedded literal or a file.
- The `Material` class already separates compilation (which runs once at `create`) from binding (`apply` / `bind`).

What was missing:

- The shader source files themselves on disk (`*.glsl`).
- A loader to turn a file path into a `std::string` suitable for `MaterialDesc::vertex_glsl`.
- An idempotent `Material::recreate_from_files` path that re-compiles + builds a fresh pipeline, swaps the old one out, and destroys the old RHI handles in deferred-frame order (i.e. once the GPU is no longer using them).
- A wiring layer in `hello_engine`: per-frame `FileWatcher::poll()`, on dirty re-trigger the affected `Material::recreate_*`.

## Decision

Three coordinated changes:

### 1. On-disk shader files

- New folder `engine/render/material/shaders/` for engine-public shader libraries that ship alongside the corresponding `Material*Material.hpp` (sky, composite, bloom, etc.).
- New folder `samples/engine/hello_engine/shaders/` for sample-specific GLSL (prim, shadow).
- Files use the existing single-stage `.glsl` convention (separate `.vert.glsl` + `.frag.glsl` per stage). No preprocessor / `#include` pipeline yet — that is a follow-up ADR if/when the shader corpus grows past trivial.

### 2. Material public API extension

```cpp
struct MaterialDesc {
    // existing fields ...

    // New: file-source variant. When both are set, the loader reads the
    // files into owning strings before delegating to the existing
    // vertex_glsl / fragment_glsl compile path. Mutually exclusive with
    // inline GLSL fields (validated at create time).
    std::string_view vertex_glsl_path {};
    std::string_view fragment_glsl_path {};
};

class Material {
    // existing ...

    /// Re-compile + re-create the pipeline using the same MaterialDesc.
    /// Old RHI handles are released only AFTER device->wait_idle so the
    /// GPU is guaranteed not to be using them. Returns the new
    /// Material; the caller assigns it over the old one to retire the
    /// previous resources.
    [[nodiscard]] static cd::core::Result<Material>
    recreate(cd::rhi::IDevice& device,
             cd::shader::ICompiler* compiler,
             const MaterialDesc& desc);
};
```

`MaterialDesc::vertex_glsl_path` is a path RELATIVE to a user-supplied search root (defaulting to the binary's working dir). The path is resolved inside the loader; the `string_view` is read at `create` time only — the underlying file content is read into a `std::string` owned by the loader's scratch buffer for the duration of compilation. There is no long-term file handle.

### 3. hello_engine wiring (sample-side)

`HelloMaterials.hpp::spawn_materials` is updated to use `vertex_glsl_path` / `fragment_glsl_path` for the prim + shadow materials. An optional embedded-string fallback path remains for shipped-binary scenarios where no `shaders/` folder is present at runtime (controlled by `HELLO_ENGINE_USE_ON_DISK_SHADERS=ON|OFF` CMake option; defaults ON in debug, OFF in release).

A new `HelloShaderWatch.hpp` owns the `cd::shader::FileWatcher`, the path-to-Material* registry, and the per-frame `poll → recreate` loop. The frame loop calls `hello_shader_watch.poll_and_reload(device, compiler, materials)` once per frame after present. Recreate failures are non-fatal (the old pipeline keeps rendering).

## Consequences

- Editing `engine/render/material/shaders/composite.frag.glsl` while `hello_engine` is running causes a pipeline rebuild within ~1 frame (poll runs at 60 Hz). Validated by editing the tonemap operator and seeing the viewport update without restart.
- Shader source is now grep-able / lint-able / IDE-highlightable as text files. Roadmap to `#include` / preprocessing / spirv-cross reflection is unblocked.
- The embedded-string `inline constexpr const char* kPrimVS` symbols remain (in `PrimShader.hpp`) as the release-mode fallback; the actual byte-for-byte content is generated from the on-disk file at build time via a CMake `configure_file` to keep the two in lockstep. This avoids the "fork drifts" failure mode.
- Hot-reload is opt-in per material (caller registers paths with the watcher). No global "watch all shaders" feature, which keeps the dependency surface tight.

## Rejected alternatives

- **`ReadDirectoryChangesW` + `inotify` + `FSEvents` native event watcher.** Rejected — the existing polling watcher is portable, race-free, and sub-millisecond at our scale. Native APIs add per-OS background threads and rename-edge-case complexity. Revisit when the shader corpus exceeds ~100 files and the stat cost becomes measurable.
- **Hot-swap by patching the pipeline state object in place.** Rejected — Vulkan pipelines are immutable; the canonical pattern is "create new, retire old after wait_idle". The recreate path already follows this.
- **Single combined `.glsl` file with `#stage vertex` / `#stage fragment` blocks.** Rejected for the V1 — adds parser surface, conflicts with editor language-server vertex-vs-fragment tooling. Revisit when ergonomic gains outweigh added surface.
- **Use `cd::vfs::IFileSource` instead of raw `std::filesystem::path` for shader reads.** Rejected for V1 — vfs is designed for pak/zip mounting at runtime, but shader hot-reload requires write notifications which `IFileSource` does not expose. Future ADR can unify when packed-asset shader shipping becomes a requirement.

## Reference

- `engine/render/shader/include/cd/shader/FileWatcher.hpp` (existing, used as-is).
- `engine/render/material/include/cd/material/Material.hpp` (extended in this Run).
- `engine/render/material/src/Material.cpp` (loader + recreate impl).
- `samples/engine/hello_engine/HelloMaterials.hpp` (call-site change).
- `samples/engine/hello_engine/HelloShaderWatch.hpp` (new, per-frame poll harness).

## Demir Kural status

No academic citation in this ADR. The hot-reload pattern is industry-standard engineering practice (covered in Unreal Engine 4 / Unity / Filament shader pipeline docs); a SOTA engineering note appears in `research/notes/shader_hotreload_sota.md` (Run 17 deliverable).
