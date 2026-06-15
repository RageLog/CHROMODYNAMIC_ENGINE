# hello_engine

The flagship CHROMODYNAMIC sample: a single binary that exercises the
full render stack (PBR + IBL + RT reflections + CSM + clouds + fog +
post-fx chain) plus a live R-Showcase panel for every library demo.

**Run**: `build/ninja-base/bin/Debug/hello_engine.exe`

**Golden capture** (headless, deterministic): `hello_engine.exe
--golden-fixture <N> --golden-out <path>.png --golden-frames <K>` renders
fixture N for K frames, dumps the final frame as a PNG, and exits. Used by
the Sponza golden-image CI lane (`cd_test_sample_sponza_golden`).

## Shader hot-reload (X5)

hello_engine reads its `prim` / `shadow` / `debug_line` shaders from
`samples/engine/hello_engine/shaders/*.glsl` at runtime (dev iteration)
and recompiles + swaps the pipeline live when a `.glsl` changes on disk.
Edit a shader, save, and the next frame shows the result — no rebuild, no
re-launch. This compresses the ~40 s edit→rebuild→relaunch loop to <1 s.

### How it works

- `cd_sample::HelloShaderWatch` (`HelloShaderWatch.hpp`) owns one
  `cd::shader::FileWatcher` (a portable polling watcher — no native OS
  event API, no background thread) plus a registry of
  `(Material*, recreate-closure)` entries.
- The main loop calls `shader_watch.poll_and_reload(device, compiler)`
  once per frame after Present. On a dirty edit the watcher fires the
  matching entry's closure, which re-runs `Material::create` with the
  same `MaterialDesc` and move-assigns the fresh `Material` over the live
  one.
- A failed compile (typo) is **non-fatal**: the closure logs the
  front-end diagnostic to stderr and leaves the live pipeline in place,
  so the session keeps rendering the last good shader.

### Dev envelope (MVP scope)

Hot-reload is **dev-only / single-pipeline**: it watches the handful of
hello_engine `.glsl` files and rebuilds one material per dirty file. The
following are tracked follow-ups, NOT in the X5 MVP (see
`docs/ADR/ADR-20260608-x5-shader-on-disk-hot-reload.md` addendum A.4):
include-closure-aware reload (blocked on SL-B cache-key work),
`HotReloadBus` throttle migration, release-mode `.spv` fast-path, and
all-pipeline generalization.

### GPU-lifetime guard (X5-2)

Before swapping a live pipeline, `poll_and_reload` calls
`device.wait_idle()` (once per dirty poll-cycle, before any swap). This
upholds the invariant that an old PSO is never destroyed while the GPU may
still reference it (Vulkan PSO destroy mid-flight = TDR). The guard lives
in the reload path, not in `Material::operator=`, so the boot-time spawn
path pays no needless drain. The cost is ~1 ms once per shader edit.

### Staleness warning + sha256 fixture (X5-3)

> **WARNING — relink-only-build staleness trap.** When the binary is built
> with `HELLO_ENGINE_USE_ON_DISK_SHADERS=ON` (default), CMake copies
> `shaders/` next to the exe so the on-disk path resolves from any cwd. A
> shader-ONLY edit (no `.cpp` touched) previously triggered a relink-only
> build that did NOT re-fire the copy, leaving the **exe-side copy stale**
> while the source was fresh — which silently produces a **FALSE
> byte-identical golden** (the golden is computed against the unchanged
> blob, masking the edit).

Two-part guard:

1. The POST_BUILD shader copy uses `copy_if_different` and lists the
   shader files as explicit `DEPENDS`, so any shader edit re-fires the
   copy even on a relink-only build (`CMakeLists.txt`).
2. A pre-golden CTest fixture (`cd_test_hello_engine_shader_staleness`,
   driven by `scripts/check_shader_staleness.cmake`) asserts
   `sha256(source) == sha256(exe-side copy)` for every shader. It is wired
   with `FIXTURES_SETUP HELLO_ENGINE_SHADERS_FRESH`; the Sponza golden
   test carries `FIXTURES_REQUIRED HELLO_ENGINE_SHADERS_FRESH`, so the
   staleness check runs BEFORE any golden compare and fails loudly instead
   of producing a false pass.

### Test command

```
ctest --preset ninja-debug -R "cd_test_material_recreate|cd_test_live_edit_smoke|cd_test_hello_engine_shader_staleness" --output-on-failure
```

- `cd_test_material_recreate` — the four hot-reload contracts (precedence,
  handle swap, deferred-release ordering, broken-edit non-fatal), Null RHI
  + stub compiler, no GPU.
- `cd_test_live_edit_smoke` — `FileWatcher` edit/modify/revert fires
  deterministically (explicit `last_write_time` bump, no `sleep_for`).
- `cd_test_hello_engine_shader_staleness` — the sha256 source==exe-side
  pre-golden guard.
