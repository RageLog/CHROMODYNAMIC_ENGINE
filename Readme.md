# CHROMODYNAMIC Engine

[![CI](https://github.com/RageLog/CHROMODYNAMIC_ENGINE/actions/workflows/ci.yml/badge.svg?branch=dev)](https://github.com/RageLog/CHROMODYNAMIC_ENGINE/actions/workflows/ci.yml)
[![Docs](https://github.com/RageLog/CHROMODYNAMIC_ENGINE/actions/workflows/docs.yml/badge.svg?branch=master)](https://github.com/RageLog/CHROMODYNAMIC_ENGINE/actions/workflows/docs.yml)
[![Bench regression](https://github.com/RageLog/CHROMODYNAMIC_ENGINE/actions/workflows/bench-regression.yml/badge.svg)](https://github.com/RageLog/CHROMODYNAMIC_ENGINE/actions/workflows/bench-regression.yml)
[![Latest tag](https://img.shields.io/github/v/tag/RageLog/CHROMODYNAMIC_ENGINE?sort=semver&label=tag)](https://github.com/RageLog/CHROMODYNAMIC_ENGINE/tags)

CHROMODYNAMIC is a cross-platform, cross-API hybrid 2D+3D game engine and
general-purpose rendering / framework, written in modern C++23. Every
subsystem is shipped as a standalone library (`cd::<name>`), so the engine
can be consumed wholesale or one module at a time inside an unrelated
application.

> **Status:** active development. Phases 2–9 closed (foundation,
> runtime, rendering, asset, ECS, scripting, audio, networking, Forward+
> GPU compute). Phase 10 closed at **v0.25.0** — the honest baseline
> after a v1.0.0 tag was cut prematurely and rolled back when manual
> visual smoke surfaced 5 bugs ctest could not catch
> ([ADR-wave125](docs/ADR/ADR-20260523-wave125-v1.0-rollback.md)).
> All 5 fixed; engine-level hardening (Vulkan-NDC winding default,
> version-string sync) applied. Phase 11 opens with the 4-axis v1.0
> maturity gate (visual correctness on all windowed samples, cross-
> GPU-vendor validation, external downstream consumer, independent
> review).
> Library count: **55**. Sample count: **17**. Test binaries: **58**.
> Latest tag: **v0.25.0**.

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
  `cd::io`, `cd::log`, `cd::events`, `cd::diag`,
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
samples/             # 17 hello_* / bench_* samples covering every public surface
tools/               # Offline cookers (cd_cook_mesh, cd_cook_texture)
Dependencies/        # Optional vendored deps (googletest fallback)
scripts/             # Smoke harness, build helpers
```

## Sample applications

Built into `build/<preset>/bin/<Config>/hello_*`:

| Sample | Category | Demonstrates |
| ----------------------- | ---------- | -------------------------------------------------------------------- |
| `hello_engine`          | engine   | Full PBR/Vulkan render loop, Sponza, IBL, TAA, bloom, RT reflections |
| `hello_editor`          | editor   | EditHistory + TransformCommands + Material Editor + Animator panels wired into ImGui |
| `hello_behavior_designer` | editor | Behavior-tree visual designer (DEFERRED — ADR-20260613) |
<!-- hello_hot_reload removed phase1157: shader watch + material rebuild covered by hello_engine HelloShaderWatch.hpp -->
| `hello_asset_pipeline`  | asset    | Mesh / texture / OBJ / cooked (.cdmesh) / glTF / BC7 — all 6 importers in one binary |
| `hello_stress`          | foundation | Concurrency stress: work-stealing pool + ring-buffer flood under load |
| `hello_world`           | game     | Minimal game-loop archetype (entity spawn, update, teardown) |
| `hello_d3d12_pbr`       | render   | D3D12 PBR sphere grid (D3D12 backend has 5 kNotImpl sites — X4-E) |
| `hello_metal`           | rhi      | Metal minimal triangle (Apple Silicon / macOS) |
| `hello_opengl`          | rhi      | OpenGL triangle + resource upload (consolidates 2 former samples) |
| `hello_path_trace`      | rhi      | Vulkan ray-query path-tracer prototype |
| `hello_triangle`        | rhi      | Vulkan dynamic-rendering minimal triangle |
| `hello_ui`              | ui       | `cd::ui` + Dear ImGui layout demo |
| `bench_archetype`       | world    | ECS archetype storage throughput benchmark |
| `hello_anim`            | world    | `cd::anim` AnimationClip/Player keyframe drive (NEEDS-PORT → hello_engine) |

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
