# CHROMODYNAMIC Engine

CHROMODYNAMIC is a cross-platform, cross-API hybrid 2D+3D game engine and
general-purpose rendering / framework, written in modern C++23. Every
subsystem is shipped as a standalone library (`cd::<name>`), so the engine
can be consumed wholesale or one module at a time inside an unrelated
application.

> **Status:** active development. Phase 2 (foundation + runtime) is
> closed; Phase 3 (rendering + asset + ECS bring-up) is in flight.
> Library count: **48**. Sample count: **23**. Test binaries: **50**.

## Highlights

- **Modern C++23** — `std::expected` for errors, `[[nodiscard]]` discipline,
  zero raw `new`/`delete`, `-Werror` across MSVC, clang-cl, MinGW GCC,
  and LLVM Clang.
- **Vulkan 1.3 dynamic rendering** via [volk](https://github.com/zeux/volk)
  and [VMA](https://gpuopen.com/vulkan-memory-allocator/). No global Vulkan
  prototypes, function pointers resolved per-device.
- **Library-oriented** — every `cd::<lib>` builds, tests, and links
  in isolation. Cross-library dependencies are an explicit DAG; circular
  references are a build error.
- **Cooked asset pipeline** — offline cookers (`cd_cook_mesh`,
  `cd_cook_texture`) emit engine-native binary formats (`.cdmesh`, `.cdtex`)
  with mip chains, BC7 compression, and `mmap`-friendly layout.
- **Asset import** — runtime readers for KTX2, glTF 2.0, OBJ, PNG/JPG/HDR.
- **ECS** — archetype storage + declared reads/writes + topologically
  sorted scheduler; 1000 entities × 3 systems × 120 frames in ~32 ms.
- **Profile** — header-only `cd::profile` with ring-buffer sink, stats
  aggregator, CSV sink, and Chrome Tracing sink
  (perfetto.dev / speedscope.app compatible).
- **Frustum culling** — Gribb-Hartmann plane extraction, per-mesh
  pre-computed local AABBs, 8-corner world-space transforms.
- **Dear ImGui** — docking branch + Vulkan dynamic-rendering backend,
  wired through `cd::imgui_backend`.
- **Tooling** — Doxygen HTML, gcov / OpenCppCoverage, ASan / UBSan / TSan
  / MSan, clang-tidy, clang-format, sample smoke harness.

## Quick start

The engine builds with [Ninja Multi-Config](https://ninja-build.org/) and
[CMake 3.28+](https://cmake.org/). All third-party dependencies are
either vendored or pulled via `FetchContent` — no `vcpkg` is required.

```bash
git clone https://github.com/CRFLT/CHROMODYNAMIC.git
cd CHROMODYNAMIC

cmake --preset ninja-base -DCD_DISABLE_VCPKG=ON
cmake --build --preset ninja-debug
ctest --preset ninja-debug --output-on-failure
```

You should see `100% tests passed, 0 tests failed out of 47`.

### Run every sample headlessly

```bash
cmake --build --preset ninja-debug --target smoke
```

This invokes [scripts/run_all_samples.ps1](scripts/run_all_samples.ps1)
(or `.sh` on POSIX). Every `hello_*` executable is launched with
`--headless 3`, watched with a 10-second deadline, and its exit code
captured. Expected output: `18/18 passed, 0 failed`.

### Generate API documentation

```bash
cmake --preset ninja-base -DCD_ENABLE_DOXYGEN=ON
cmake --build build/ninja-base --target cd_docs
# → build/ninja-base/docs/doxygen/html/index.html
```

### Coverage report

```bash
cmake --preset ci-gcc -DCD_ENABLE_COVERAGE=ON -DCD_DISABLE_VCPKG=ON
cmake --build --preset ci-gcc-debug
ctest --preset ci-gcc-debug
cmake --build --preset ci-gcc-debug --target coverage-report
# → build/ci-gcc/coverage.html + coverage.xml (Cobertura)
```

### Sanitizers

```bash
cmake --preset ninja-base-asan -DCD_DISABLE_VCPKG=ON
cmake --build --preset ninja-debug-asan
ctest --preset ninja-debug-asan        # ASan + UBSan
```

TSan and MSan have their own presets (`ninja-base-tsan`, `ninja-base-msan`).

## Supported toolchains

CI verifies the build matrix on every push:

| OS      | Compiler                  | Preset           | Status |
|---------|---------------------------|------------------|--------|
| Windows | MSVC 17 (VS 2022)         | `ci-vs2022`      | ✅ |
| Windows | MSVC 18 (VS 2026)         | `ci-vs2026`      | ✅ |
| Windows | MSVC + Ninja Multi-Config | `ci-msvc`        | ✅ |
| Windows | clang-cl                  | `ci-clangcl-win` | ✅ |
| Windows | LLVM Clang                | `ci-llvm-win`    | ✅ |
| Windows | MinGW GCC (UCRT64)        | `gcc-ucrt64-base`| ⚠️ partial (some MinGW-specific issues) |
| Linux   | GCC                       | `ci-gcc`         | ✅ |
| Linux   | LLVM Clang                | `ci-clang`       | ✅ |
| macOS   | AppleClang                | `ci-appleclang`  | ✅ |

The minimum compilers actively tested are MSVC 19.42, Clang 18, and
GCC 14. C++23 features used: `std::expected`, `consteval`, lambda
capture-init, deducing `this`, `if constexpr` with explicit template
arguments.

## Library catalogue (highlights)

See [docs/LIBRARIES.md](docs/LIBRARIES.md) for the full per-library table
(targets, namespaces, public headers, dependencies).

Foundation tier:
`cd::core`, `cd::mem`, `cd::concurrency`, `cd::time`, `cd::io`,
`cd::serialization`, `cd::log`, `cd::events`, `cd::diag`, `cd::profile`,
`cd::platform`, `cd::plugin`, `cd::config`, `cd::math`, `cd::vfs`.

Asset tier:
`cd::asset`, `cd::asset_image` (PNG/JPG/HDR), `cd::asset_obj`,
`cd::asset_gltf` (glTF 2.0), `cd::asset_cdmesh`, `cd::asset_cdtex`
(BC7 cook target), `cd::asset_ktx2` (Khronos KTX2 reader).

Render tier:
`cd::rhi`, `cd::rhi_vulkan`, `cd::shader`, `cd::material`, `cd::render`,
`cd::framegraph`, `cd::camera`, `cd::imgui_backend`.

World tier:
`cd::ecs`, `cd::scene`, `cd::physics`, `cd::anim`, `cd::audio`,
`cd::input`, `cd::net`.

UI tier:
`cd::ui`, `cd::editor_ui`, `cd::editor`.

Runtime tier:
`cd::runtime` — the composition layer that glues every subsystem into a
production application.

## Repository layout

```text
.claude/agents/      # 33-subagent suite (planning, build, test, audit, etc.)
docs/ADR/            # 17 Architecture Decision Records (ADR-NNNN-*.md)
docs/PLAN.md         # Phase / sprint timeline
docs/DESIGN.md       # Master design document
docs/Doxyfile.in     # Doxygen template (configured at CMake time)
cmake/CD*.cmake      # cd_add_library / cd_add_test / cd_add_sample helpers
cmake/CDDoxygen.cmake, CDCoverage.cmake, CDSmoke.cmake
CMakeModules/        # Vendored helpers (Sanitizers, Packaging, …)
engine/<lib>/        # One subdirectory per cd::<lib>
samples/             # 18 hello_* samples covering every public surface
tools/               # Offline cookers (cd_cook_mesh, cd_cook_texture)
Dependencies/        # Optional vendored deps (googletest fallback)
scripts/             # Smoke harness, build helpers
```

## Sample applications

Built into `build/<preset>/bin/<Config>/hello_*`:

| Sample | Demonstrates |
|---|---|
| `hello_core`          | `cd::core` primitives (Result, ErrorCode, span) |
| `hello_foundation`    | foundation layer wiring (log + diag + profile) |
| `hello_handle`        | `cd::mem` slot-map handle pattern |
| `hello_runtime`       | `cd::runtime` composition (subsystem startup/shutdown) |
| `hello_ecs`           | archetype storage + queries |
| `hello_scheduler`     | `cd::ecs::Scheduler` with declared reads/writes |
| `hello_triangle`      | Vulkan dynamic-rendering minimal triangle |
| `hello_mesh`          | indexed mesh draw |
| `hello_texture`       | sampler + descriptor set + textured quad |
| `hello_textured_cooked`| BC7 cook → load → GPU upload pipeline |
| `hello_cube`          | depth buffer + transform UBO |
| `hello_scene_graph`   | hierarchical transforms via `cd::scene` |
| `hello_obj`           | OBJ import + draw |
| `hello_cooked`        | `.cdmesh` cook target |
| `hello_gltf`          | glTF 2.0 import + per-instance frustum culling |
| `hello_framegraph`    | `cd::framegraph` pass declarations |
| `hello_hot_reload`    | shader hot-reload via file watcher |
| `hello_imgui`         | Dear ImGui demo + profile HUD |
| `hello_bench`         | `cd::bench` microbench on four hot-path snippets |
| `hello_json`          | `cd::asset_json` parse / mutate / serialize round-trip |
| `hello_scene_save`    | scene graph ↔ JSON round-trip (`cd::scene::Serializer`) |
| `hello_audio_synth`   | sine synth → WAV byte stream → `cd::asset_wav::load` roundtrip |
| `hello_asset_registry`| `cd::asset::AssetRegistry` + `WavAssetLoader` + `JsonAssetLoader` demo |

## Documentation

- [docs/DESIGN.md](docs/DESIGN.md) — master design document
- [docs/PLAN.md](docs/PLAN.md) — phase / sprint timeline
- [docs/ADR/](docs/ADR/) — 17 Architecture Decision Records (the
  authoritative "why")
- API reference — generated locally via `cd_docs` target; published to
  GitHub Pages by the `Docs` workflow on every master push.

## Contributing

Project rules are codified in [CLAUDE.md](CLAUDE.md) and are enforced
both by the CI and by the agent suite under [.claude/agents/](.claude/agents/).

The short version:

- C++23, `[[nodiscard]]` on every fallible return, `std::expected` for errors.
- Every claim needs evidence: build clean + tests pass + grep'able symbol.
- Surgical edits only — no full-file rewrites.
- Mimic the existing library pattern (`include/cd/<lib>/`, `src/`, `tests/`,
  `CMakeLists.txt`). Cross-library dependencies are explicit; cycles are
  build errors.

## License

To be decided per ADR. The current development copy is unpublished.
