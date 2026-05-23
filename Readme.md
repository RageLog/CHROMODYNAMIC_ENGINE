# CHROMODYNAMIC Engine

CHROMODYNAMIC is a cross-platform, cross-API hybrid 2D+3D game engine and
general-purpose rendering / framework, written in modern C++23. Every
subsystem is shipped as a standalone library (`cd::<name>`), so the engine
can be consumed wholesale or one module at a time inside an unrelated
application.

> **Status:** active development. Phases 2–9 closed (foundation,
> runtime, rendering, asset, ECS, scripting, audio, networking, Forward+
> GPU compute). Phase 10 ("v1.0 path") in flight: Sprint 1 (Quality)
> closed at v0.21.0; Sprint 2 (Docs) in progress.
> Library count: **55**. Sample count: **40**. Test binaries: **58**.
> Latest tag: **v0.21.0**.

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

You should see `100% tests passed, 0 tests failed out of 58`.

### Run every sample headlessly

```bash
cmake --build --preset ninja-debug --target smoke
```

This invokes [scripts/run_all_samples.ps1](scripts/run_all_samples.ps1)
(or `.sh` on POSIX). Every `hello_*` executable is launched with
`--headless 3`, watched with a 10-second deadline, and its exit code
captured.

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

## Architecture

For the high-level stack diagram, the dependency DAG, and the cross-cutting
conventions (error policy, ownership, build flags, test discipline, and the
checklist for adding a new subsystem) read [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md).

See [docs/LIBRARIES.md](docs/LIBRARIES.md) for the full per-library catalogue
(targets, namespaces, public headers, dependencies).

Tier summary:

- **Foundation** — `cd::core`, `cd::mem`, `cd::concurrency`, `cd::time`,
  `cd::io`, `cd::serialization`, `cd::log`, `cd::events`, `cd::diag`,
  `cd::profile`, `cd::platform`, `cd::plugin`, `cd::config`, `cd::math`,
  `cd::vfs`, `cd::bench`.
- **Asset** — `cd::asset`, `cd::asset_image`, `cd::asset_obj`,
  `cd::asset_gltf`, `cd::asset_cdmesh`, `cd::asset_cdtex`, `cd::asset_ktx2`,
  `cd::asset_wav`, `cd::asset_json`, `cd::asset_pak`.
- **Render** — `cd::rhi`, `cd::rhi_vulkan`, `cd::rhi_d3d12`,
  `cd::rhi_metal`, `cd::rhi_native`, `cd::shader`, `cd::material`,
  `cd::render`, `cd::framegraph`, `cd::camera`, `cd::cluster`,
  `cd::cluster_gpu`, `cd::cluster_pbr`, `cd::volumetric`, `cd::ibl`,
  `cd::async_submit`, `cd::imgui_backend`.
- **World** — `cd::ecs`, `cd::scene`, `cd::physics`, `cd::anim`,
  `cd::audio`, `cd::input`, `cd::net`.
- **UI** — `cd::ui`, `cd::editor_ui`, `cd::editor`.
- **Side** — `cd::script` (Lua 5.4), `cd::imgdiff` (FLIP/SSIM/PSNR).
- **Runtime** — `cd::runtime` — composition layer.

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

- [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) — engine stack, DAG, and
  cross-cutting conventions
- [docs/LIBRARIES.md](docs/LIBRARIES.md) — per-library catalogue
- [docs/DESIGN.md](docs/DESIGN.md) — long-form design rationale
- [docs/PLAN.md](docs/PLAN.md) — phase / sprint timeline
- [docs/ADR/](docs/ADR/) — Architecture Decision Records (the
  authoritative "why" — 71 entries through Phase 10 Sprint 1)
- [CHANGELOG.md](CHANGELOG.md) — per-release notes (Conventional Commits)
- [docs/Modules.dox](docs/Modules.dox) — Doxygen module hierarchy
- API reference — generated locally via the `cd_docs` target; published
  to GitHub Pages by the `Docs` workflow on every master push.

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
