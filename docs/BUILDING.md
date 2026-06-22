# Building CHROMODYNAMIC

This document covers the standard build flow and optional feature flags.
For consuming an installed copy from a downstream project see
[CONSUMING.md](CONSUMING.md). For CI / self-hosted runner setup see
[CI_SELF_HOSTED.md](CI_SELF_HOSTED.md).

---

## 1. Prerequisites

- **CMake ≥ 3.25** and **Ninja** (recommended) or Visual Studio 17 2022.
- **vcpkg** cloned and `VCPKG_ROOT` set (manifest mode; `vcpkg.json` is in the
  repo root).
- **Vulkan SDK** (any recent version; needed by `cd::rhi_vulkan` and ImGui
  Vulkan backend).
- Windows 10/11 (primary CI target). Linux and macOS paths exist but are
  community-supported.

---

## 2. Default build (no optional backends)

```bash
# Configure — vcpkg installs deps automatically from vcpkg.json
cmake --preset ninja-base

# Build (debug)
cmake --build --preset ninja-debug

# Run tests
ctest --preset ninja-debug --output-on-failure
```

`CD_ENABLE_WEBGPU` defaults to `OFF`; the WebGPU stub compiles as a clean
no-op (CI tier). No Dawn download occurs unless you opt in (see §4).

---

## 3. Available CMake presets

| Preset | Description |
|---|---|
| `ninja-base` | Configure with Ninja + vcpkg manifest |
| `ninja-debug` | Build (Debug, ASAN/UBSAN enabled) |
| `ninja-release` | Build (Release, optimised) |

---

## 4. WebGPU / Dawn (opt-in)

Dawn is a large optional dependency (~800 MB build including Abseil and Python
host tooling). It is disabled by default so that CI and contributor builds
remain fast.

### 4a. Install the Dawn vcpkg feature

```bash
vcpkg install --x-feature=webgpu \
              --triplet x64-windows \
              --x-manifest-root=<path-to-repo>
```

Alternatively, add `"webgpu"` to the `"default-features"` list in your local
`vcpkg.json` and run a plain `vcpkg install`.

### 4b. Configure with Dawn enabled

```bash
cmake --preset ninja-base \
      -DVCPKG_MANIFEST_FEATURES=webgpu \
      -DCD_ENABLE_WEBGPU=ON
```

Key details:

- The CMake target to link against is **`dawn::webgpu_dawn`** (not
  `Dawn::webgpu_cpp`; that alias is not exported by the vcpkg port).
- Enabling Dawn sets the compile definition `CD_UI_WEBGPU_HAVE_DAWN=1` across
  the `cd::ui_renderer_webgpu` library.
- A POST_BUILD rule copies `webgpu_dawn.dll` next to the test executable
  (`cd_test_webgpu_dawn_device`) so the binary is self-contained.
- The Dawn-gated test target `cd_test_webgpu_dawn_device` is only added to the
  build when `CD_ENABLE_WEBGPU=ON` and the `dawn::webgpu_dawn` target is
  found; it is absent from default CI manifests.

### 4c. Build and test

```bash
cmake --build --preset ninja-debug

# Run only the Dawn device test
ctest --preset ninja-debug -R cd_test_webgpu_dawn_device --output-on-failure
```

### 4d. Reverting to the default (stub) build

```bash
cmake --preset ninja-base \
      -DCD_ENABLE_WEBGPU=OFF \
      -DVCPKG_MANIFEST_FEATURES=""
```

This reconfigures without Dawn; the WebGPU stub is restored and all other
libraries are unaffected.

---

## 5. Other optional flags

| Flag | Default | Description |
|---|---|---|
| `CD_ENABLE_WEBGPU` | `OFF` | Enable Dawn/WebGPU backend (§4) |
| `CD_DISABLE_VCPKG` | `OFF` | Skip vcpkg toolchain (use system deps) |
| `CD_ENABLE_ASAN` | preset-driven | AddressSanitizer (active in `ninja-debug`) |
| `CD_ENABLE_UBSAN` | preset-driven | UBSanitizer (active in `ninja-debug`) |
