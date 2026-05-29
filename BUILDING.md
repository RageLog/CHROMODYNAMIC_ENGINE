# Building CHROMODYNAMIC

This document describes how to build CHROMODYNAMIC Engine reproducibly from a clean
checkout, the dependency-resolution policy, and the supported toolchain matrix.

For project goals see `README.md`; for architecture see `docs/DESIGN.md` and the
`docs/ADR/` series.

## TL;DR (Windows MSVC, the fast path)

```powershell
git clone --recursive https://github.com/CRFLT/CHROMODYNAMIC.git
cd CHROMODYNAMIC

# 1. Install vcpkg deps (gtest + fmt) - manifest mode auto-runs at configure-time
#    if VCPKG_ROOT env var or Dependencies/vcpkg/ submodule is present. To skip
#    vcpkg entirely (use vendored fallbacks), set CD_DISABLE_VCPKG=ON.

# 2. Configure + build via Ninja Multi-Config preset
cmake --preset ninja-base
cmake --build --preset ninja-debug

# 3. Run the tests
ctest --preset ninja-debug --output-on-failure

# 4. Launch the smoke sample
.\build\ninja-base\bin\Debug\hello_engine.exe
```

## Required toolchain

| Component | Minimum | Recommended |
|---|---|---|
| CMake | 3.28 | 3.31+ |
| Ninja | 1.11 | 1.12+ |
| C++ compiler | one of: MSVC 19.40 (VS 2022 17.11), clang-cl 19, clang 19, GCC 14, AppleClang 16 | latest stable |
| C++ standard | 23 | 23 |
| Git | 2.40 | 2.44+ |
| Python | 3.10 (some helper scripts) | 3.12 |

The 4+ compiler matrix is checked nightly via the `ci-*` presets in
`CMakePresets.json`. See `docs/CI_SELF_HOSTED.md` for the runner setup.

## Dependency Resolution Tiers

CHROMODYNAMIC uses a deliberate two-tier dependency strategy. Understanding why
matters before you add new third-party code.

### Tier A - vcpkg manifest (`vcpkg.json`)

Only deps that satisfy ALL of the following live in `vcpkg.json`:

1. The engine actually calls `find_package(<name> CONFIG)` (or `MODULE`) for them
   from at least one `CMakeLists.txt`.
2. They are stable across the SDK / driver matrix the engine supports - we are
   willing to inherit whatever version the pinned vcpkg baseline ships.
3. They do not need EXACT version pinning per-subsystem.

As of phase 282 the vcpkg manifest lists only:

- `gtest` >= 1.14.0 - resolved by top-level `find_package(GTest CONFIG QUIET)`
  in `CMakeLists.txt`. Falls back to vendored `Dependencies/googletest/`, then
  to `FetchContent` (v1.15.2) if vcpkg is disabled.
- `fmt` >= 10.0.0 - resolved by `find_package(fmt CONFIG QUIET)`. Optional in
  Phase 1; cd::log integration lands in Sprint S2.2.

The vcpkg baseline is pinned at:

```
56bb2411609227288b70117ead2c47585ba07713
```

This corresponds to vcpkg release `2026.04.27`. Bump together with a clean
configure + build verification (see `Reproducible clean-build verification`
below).

### Tier B - FetchContent in consumer CMakeLists.txt

The graphics / asset / engine stack is intentionally FetchContent-resolved
per-subsystem so each consumer pins its EXACT SDK / commit version. Examples:

| Dep | Pinned in | Reason |
|---|---|---|
| `Vulkan-Headers` | `engine/render/rhi_vulkan/CMakeLists.txt` (tag `vulkan-sdk-1.3.290.0`) | Lockstep with `volk` + driver SDK; cannot drift with vcpkg baseline. |
| `volk` | same | Must match Vulkan-Headers exactly. |
| `VMA` | same (tag `v3.1.0`) | Released independently of vcpkg cadence. |
| `glslang` | `engine/render/shader/CMakeLists.txt` | Same reason as Vulkan-Headers. |
| Other (planned: ktx, fastgltf, meshoptimizer, basis-universal, miniaudio, jolt) | per-subsystem on landing | See ADR-016 Vendor Matrix & Replace-Ready Policy. |

Promoting a Tier B dep into Tier A requires:

1. A `find_package(<name> CONFIG)` call in the consuming `CMakeLists.txt`.
2. Removal of the `FetchContent_Declare` block.
3. A clean configure + build + test pass on `ci-msvc`, `ci-clangcl-win`, and
   one Linux preset.
4. A note in the `vcpkg.json` `$x-policy-note` block citing the wiring change.

### Tier C - Vendored submodules / drop-in (`Dependencies/`)

For deps where neither vcpkg nor FetchContent gives the reproducibility we
need (typically: closed-source SDKs, patched forks, or things vcpkg has
historically broken), drop the source into `Dependencies/<name>/` and add
an `add_subdirectory` in `Dependencies/CMakeLists.txt`. Currently only
`Dependencies/googletest/` is vendored as the second-tier fallback for
`gtest`.

## Reproducible clean-build verification

Use this exact sequence to verify a clean bootstrap from scratch (the script
that gates a vcpkg-baseline bump):

```bash
# Wipe build / install caches
rm -rf build/ install/

# (Optional) wipe vcpkg buildtrees / downloads for a true cold cache
# vcpkg remove --outdated --recurse  # only if you really need it

# Configure - vcpkg.json deps install transparently during this step
cmake --preset ninja-base 2>&1 | tee /tmp/cd-configure.log

# Verify the configure summary shows both vcpkg deps resolved
grep "GTest: vcpkg/find_package" /tmp/cd-configure.log
grep "fmt found" /tmp/cd-configure.log     # optional, may warn if vcpkg fmt not present

# Build Debug
cmake --build --preset ninja-debug 2>&1 | tail -50

# Run the test suite
ctest --preset ninja-debug --output-on-failure
```

A green run of the above is what justifies a `phase###-X2(build):` commit.

## CD_DISABLE_VCPKG escape hatch

Set `-DCD_DISABLE_VCPKG=ON` (or `env CD_DISABLE_VCPKG=ON`) to bypass vcpkg
entirely. The build then resolves `gtest` via vendored `Dependencies/googletest/`
and `fmt` via FetchContent (if present) or skips it. Useful for offline /
air-gapped CI and for first-time contributors on systems without vcpkg.

## Sanitizer presets

| Preset | Sanitizer | Compiler restriction |
|---|---|---|
| `ninja-base-asan` | AddressSanitizer + UBSan | any |
| `ninja-base-tsan` | ThreadSanitizer | clang / gcc — **Linux only** |
| `ninja-base-msan` | MemorySanitizer | clang only — Linux only |

### ASan / UBSan (cross-platform)

```bash
cmake --preset ninja-base-asan
cmake --build --preset ninja-debug-asan
ctest --preset ninja-debug-asan --output-on-failure
```

### TSan (ThreadSanitizer) — Linux / clang or gcc only

TSan instruments the concurrency suite to detect data races and
lock-order violations in `cd::concurrency` (WorkStealingThreadPool,
JobGraph, ParallelFor, Latch, Channel, etc.).

```bash
# Linux only — requires clang or gcc (not clang-cl, not MSVC)
cmake --preset ninja-base-tsan \
  -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++
cmake --build --preset ninja-debug-tsan

# Run only the concurrency suite (the preset filter applies automatically)
ctest --preset ninja-debug-tsan --output-on-failure

# Manual equivalent with an explicit -R filter:
ctest --preset ninja-debug-tsan --output-on-failure \
  -R "cd_test_(concurrency|threadpool|deterministic_executor|work_stealing_deque|work_stealing_pool|hazard_ptr|job_graph)"
```

The `ninja-debug-tsan` test preset already embeds the concurrency-suite
`-R` filter so `ctest --preset ninja-debug-tsan` alone is sufficient.

#### Windows / clang-cl limitation

TSan is **not supported on Windows with clang-cl**. `lld-link` refuses
to mix TSan-instrumented TUs with non-instrumented vendored libraries
(tinygltf, glslang, volk, vma), producing:

```text
lld-link: error: /failifmismatch: mismatch detected for 'annotate_string'
```

The TSan preset is therefore targeted at the Linux CI lane
(`sanitizers` job in `.github/workflows/ci.yml`, `ubuntu-24.04`,
clang). The NVIDIA Windows self-hosted lane (`ci-nvidia-windows.yml`)
also lists the TSan preset for future use once a Linux runner is
registered under that workflow or the linker restriction is resolved.

See `ADR-20260528-job-system-design.md` §X1-FU-B for background.

## Common configure-time pitfalls

- **vcpkg not found** -> set `VCPKG_ROOT` env var or `git submodule update --init
  Dependencies/vcpkg` (we ship vcpkg as an optional submodule).
- **MSVC: cl.exe not on PATH** -> launch from a "x64 Native Tools Command Prompt
  for VS 2022/2026", or use `Developer PowerShell for VS`.
- **Compiler too old** -> CHROMODYNAMIC requires C++23. MSVC 19.40+ / clang 19+
  / GCC 14+ / AppleClang 16+.
- **Pre-existing build/ with mixed compiler cache** -> always wipe `build/`
  before switching compilers.

## Library-as-product consumption

After `cmake --install build/ninja-base --prefix=/some/where`, downstream
projects can:

```cmake
find_package(CHROMODYNAMIC CONFIG REQUIRED)
target_link_libraries(my_game PRIVATE cd::engine cd::asset cd::audio)
```

Per-library consumption (e.g. only `cd::concurrency` from a tool) is
documented per-subsystem in `engine/<lib>/README.md` (incrementally being
written; concurrency is filed under ADR-FU-E).

## Related documents

- `docs/DESIGN.md` - master engine architecture
- `docs/ADR/README.md` - architecture decision records index
- `docs/ADR/ADR-014-ci-cd-distribution.md` - CI / CD strategy
- `docs/ADR/ADR-016-vendor-matrix-replace-policy.md` - vendor pickabbility policy
- `docs/ADR/ADR-017-dtforhil-pattern-salvage.md` - heritage from DtForHil
- `docs/STATUS_AND_PLAN_W8.md` - current state of work
- `CLAUDE.md` - project rules (auto-loaded for AI collaboration)
