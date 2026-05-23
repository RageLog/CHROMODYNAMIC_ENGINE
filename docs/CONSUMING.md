# Consuming CHROMODYNAMIC from a downstream project

This guide walks from `git clone` of the engine, through `cmake --install`,
to a separate project's `find_package(CHROMODYNAMIC)` call. If you only
want to *build* the engine, see [the README](../Readme.md). This
document is for the case where you want to **link against** an installed
copy of the engine from your own application.

> **Status:** Phase 11 Track C Part 1. The install tree shipped in
> v0.25.0 covers 47 of the 55 libraries — the 8 excluded ones
> (`cd::imgui_backend`, `cd::script`, `cd::rhi_vulkan`, `cd::shader`,
> `cd::rhi_native`, `cd::cluster_gpu`, `cd::material`, plus the
> sample-only `cd::sample_common`) leak vendored FetchContent paths
> and have to be built from source. See
> [ADR-20260523-wave122](ADR/ADR-20260523-wave122-v0.24.0-install-pipeline.md)
> for the cut list rationale.

---

## 1. Build and install the engine

```bash
# Clone, configure, build.
git clone https://github.com/RageLog/CHROMODYNAMIC_ENGINE.git
cd CHROMODYNAMIC_ENGINE

cmake --preset ninja-base -DCD_DISABLE_VCPKG=ON
cmake --build --preset ninja-debug

# Stage the install tree under a prefix you control. Use any path; the
# engine doesn't write to system locations unless you point it there.
cmake --install build/ninja-base \
    --prefix /opt/chromodynamic-0.25 \
    --config Debug
```

After this you should see:

```
/opt/chromodynamic-0.25/
├── bin/                              # any executables (cookers, tools)
├── include/cd/<lib>/                 # public headers, one dir per cd::<lib>
├── lib/
│   ├── *.lib  (Windows) / *.a (Linux/macOS)
│   └── cmake/CHROMODYNAMIC/
│       ├── CHROMODYNAMICConfig.cmake          # find_package entry point
│       ├── CHROMODYNAMICConfigVersion.cmake   # SemVer matching
│       ├── CHROMODYNAMICTargets.cmake         # cd::* imported-target defs
│       └── CHROMODYNAMICTargets-debug.cmake
└── share/doc/CHROMODYNAMIC/
    ├── Readme.md  CHANGELOG.md  CLAUDE.md
    ├── ARCHITECTURE.md  LIBRARIES.md  DESIGN.md  PLAN.md
    └── ADR/*.md                       # full ADR archive
```

## 2. Wire your downstream project

In your own project's `CMakeLists.txt`:

```cmake
cmake_minimum_required(VERSION 3.28)
project(my_app LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(CHROMODYNAMIC 0.25 REQUIRED)

add_executable(my_app main.cpp)
target_link_libraries(my_app PRIVATE
    cd::core            # Result, ErrorCode, Handle, CVar, Version
    cd::diag            # CD_ASSERT / CD_VERIFY / CD_PANIC
    cd::log             # ILogger + sinks + RingBufferSink + PanicDump
    cd::math            # Vec, Mat, Quat, Transform
    cd::asset           # AssetRegistry, AssetId
    cd::asset_image     # PNG/JPG load + write_png_rgba
    cd::ecs cd::scene   # ECS + scene graph + transforms
    cd::imgdiff         # FLIP / SSIM / PSNR
)
```

Then configure with the prefix you installed to:

```bash
cmake -S . -B build -G Ninja \
    -DCMAKE_PREFIX_PATH=/opt/chromodynamic-0.25 \
    -DCMAKE_BUILD_TYPE=Debug
cmake --build build
```

You should see CMake print:

```
-- Found CHROMODYNAMIC 0.25.0
```

If it doesn't, see [§5 Troubleshooting](#5-troubleshooting).

## 3. What's in vs. what's not

### In the install tree (you can `find_package` these)

| Tier | Targets |
|---|---|
| Foundation | `cd::core` `cd::diag` `cd::log` `cd::math` `cd::mem` `cd::time` `cd::events` `cd::vfs` `cd::profile` `cd::config` `cd::platform` `cd::plugin` `cd::bench` `cd::io` `cd::concurrency` |
| Asset | `cd::asset` `cd::asset_image` `cd::asset_obj` `cd::asset_gltf` `cd::asset_cdmesh` `cd::asset_cdtex` `cd::asset_ktx2` `cd::asset_wav` `cd::asset_json` `cd::asset_pak` |
| Render (no shader/vulkan deps) | `cd::rhi` `cd::framegraph` `cd::camera` `cd::async_submit` `cd::ibl` `cd::cluster` `cd::cluster_pbr` `cd::volumetric` `cd::render` `cd::rhi_d3d12` `cd::rhi_metal` |
| World | `cd::ecs` `cd::scene` `cd::physics` `cd::anim` `cd::audio` `cd::input` `cd::net` |
| UI / Runtime | `cd::ui` `cd::editor_ui` `cd::editor` `cd::runtime` |
| Side | `cd::imgdiff` |

### Excluded from install (build from source)

These leak vendored FetchContent paths into their `INSTALL_INTERFACE`:

| Target | Reason |
|---|---|
| `cd::imgui_backend` | dear-imgui pulled via FetchContent |
| `cd::script` | Lua 5.4 vendored |
| `cd::rhi_vulkan` | volk + Vulkan-Headers + VMA vendored |
| `cd::shader` | glslang + SPIRV vendored |
| `cd::rhi_native` | depends on `cd::rhi_vulkan` |
| `cd::cluster_gpu` | depends on `cd::shader` |
| `cd::material` | depends on `cd::shader` |
| `cd::sample_common` | sample-only INTERFACE library |

If your downstream needs any of these, the cleanest path today is
to include CHROMODYNAMIC as a git submodule and `add_subdirectory()`
its `Engine/...` paths. The Phase 11 plan covers lifting the
vendored-dep wrappers so these libraries can re-enter the export set
in v1.x.

## 4. A minimal main.cpp that exercises three foundation libraries

Save as `main.cpp` next to the `CMakeLists.txt` above. This is
deliberately as small as possible — it links `cd::core`, `cd::diag`,
`cd::log` and exits 0 on success.

```cpp
#include <cd/core/ErrorCode.hpp>
#include <cd/core/ErrorFormat.hpp>
#include <cd/diag/Assert.hpp>
#include <cd/log/RingBufferSink.hpp>

#include <cstdio>

int main()
{
    // cd::core — typed ErrorCode + log-friendly formatting.
    const auto ec = cd::core::core_errors::make(
        cd::core::core_errors::Code::kNotFound, "no such asset");
    const auto pretty = cd::core::format(ec);
    std::fprintf(stdout, "error: %s\n", pretty.c_str());

    // cd::diag — assertion that's debug-only. If you want it in release
    // too, use CD_VERIFY instead.
    CD_ASSERT(1 + 1 == 2);

    // cd::log — bounded in-memory log mirror.
    cd::log::RingBufferSink sink { 64 };
    cd::log::LogRecord r;
    r.message = "downstream consumer alive";
    sink.on_log_record(r);
    std::fprintf(stdout, "log ring holds %zu record(s)\n", sink.size());

    return 0;
}
```

Build and run:

```bash
./build/my_app
# error: core::NotFound: no such asset
# log ring holds 1 record(s)
```

A copy-pasteable version of this project lives at
[examples/consuming/](../examples/consuming/) in this repo.

## 5. Troubleshooting

### "CMake Error: Could not find a configuration file for package ... "

Make sure `CMAKE_PREFIX_PATH` points at the prefix you installed to.
You can also use the explicit env var:

```bash
export CMAKE_PREFIX_PATH=/opt/chromodynamic-0.25
cmake -S . -B build
```

### "Found CHROMODYNAMIC but it set CHROMODYNAMIC_FOUND to FALSE"

Means `CHROMODYNAMICTargets.cmake` didn't import `cd::core`. The
install tree is incomplete — re-run `cmake --install` against the
same build directory, ensuring you used the matching `--config` flag
(Debug install vs. Release configure mismatches surface here).

### "undefined reference to cd::core::format"

You're linking against the engine's headers but not the library.
Add `cd::core` to your `target_link_libraries` — the function lives
in `Engine/foundation/core/src/ErrorFormat.cpp`, not the header.

### "target cd::rhi_vulkan not found"

`cd::rhi_vulkan` is one of the EXCLUDE_FROM_INSTALL libraries.
See [§3](#3-whats-in-vs-whats-not) for the cut list and current
workaround (consume as a subproject via `add_subdirectory()`).

### Debug vs. Release ABI mismatch

The MSVC runtime selection (`/MD` vs `/MDd`) must match between the
engine install and your consumer. If you installed `--config Debug`,
configure your consumer with `-DCMAKE_BUILD_TYPE=Debug` (Ninja
single-config) or build the Debug configuration (Visual Studio
multi-config). Mixing them yields linker errors about CRT symbols.

### Version-range matching

`find_package(CHROMODYNAMIC 0.25 REQUIRED)` accepts any 0.25.x. The
config writes a `SameMajorVersion` policy — `find_package(CHROMODYNAMIC
1.0 ...)` will reject 0.25.0 install trees and vice versa, which is
the v1.0 ABI promise in machine-readable form.

## 6. What we recommend you ship a smoke for

When your downstream project's CI starts, add at least these
assertions:

- `cd::core::kEngineVersion.major` is what your project pinned.
- `cd::core::kEngineName` is `"CHROMODYNAMIC"`.
- A `Result<T>` round-trip through your code path (proves the
  `std::expected` toolchain compatibility you actually link against).
- Optionally, a `cd::imgdiff::compare(a, b, tolerance)` pair on a
  trivial 2×2 reference image — this catches misaligned PNG decode +
  alpha-channel surprises early.

The Phase 11 acceptance for Axis C in the v1.0 maturity gate is "a
real downstream project ships against this install tree." If you
build one, please open an issue or PR linking it — the engine
documents downstream consumers in `docs/DOWNSTREAM.md` (TODO).

## 7. References

- [ADR-20260523-wave122](ADR/ADR-20260523-wave122-v0.24.0-install-pipeline.md) — install tree design + cut list
- [ADR-20260523-wave125](ADR/ADR-20260523-wave125-v1.0-rollback.md) — the v1.0 maturity gate definition
- [ADR-20260523-wave132](ADR/ADR-20260523-wave132-v0.25.0-baseline.md) — v0.25.0 baseline
- [ARCHITECTURE.md](ARCHITECTURE.md) — engine stack + DAG
- [LIBRARIES.md](LIBRARIES.md) — per-library catalogue
- [examples/consuming/](../examples/consuming/) — copy-pasteable starter template
