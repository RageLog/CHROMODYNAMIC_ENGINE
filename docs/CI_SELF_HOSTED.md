# Self-hosted CI runners

## AMD RADV (Phase 123)

The `ci-amd-radv.yml` workflow targets a self-hosted runner labelled
`amd-gpu` + `linux`. Until the lab has hardware available the
workflow stays dormant — `workflow_dispatch` calls will queue and
eventually time out without consuming hosted minutes.

### Hardware

Any AMD GCN5 / RDNA / RDNA2 / RDNA3 GPU with the open-source RADV
driver (kernel `amdgpu` module). Tested baseline:
- AMD Radeon RX 6600 (RDNA2) on Ubuntu 24.04
- Mesa 24.x

### Software

```bash
sudo apt-get install -y \
  mesa-vulkan-drivers \
  vulkan-tools \
  libvulkan-dev \
  libwayland-dev libxkbcommon-dev \
  clang ninja-build cmake
```

Verify:

```bash
vulkaninfo --summary | grep driverName
# Expect: driverName = radv
```

### Runner registration

Follow GitHub's self-hosted runner instructions. The required labels
on the runner are:

- `self-hosted` (default)
- `linux`
- `amd-gpu`

The workflow `if: runs-on` matches that exact label triple.

### Why a separate lane

`ci.yml`'s `linux-vulkan-sw` job already covers the Linux Vulkan
code path via lavapipe (software ICD on the hosted ubuntu-24.04
runner). Lavapipe catches "does this path execute at all" but not
vendor-specific:

- AMD's GLSL miscompile patterns (driver-specific spirv-cross
  intermediate forms).
- Wave / subgroup-size assumptions baked into shaders.
- Descriptor-indexing edge cases that hit hardware bindless paths.
- Floating-point rounding mode discrepancies between RDNA waves
  and lavapipe's scalar reference path.

The RADV lane is the future regression detector for that vendor
surface.

### Golden references

`tests/golden/radv/` is the per-vendor reference set the workflow
compares against (Phase 11 Track B Part 2 pattern, vendor-keyed).
Captures are populated by running the `Run all samples` job with
`--golden-capture` against a known-good build and committing the
output PNGs.

The directory is gitignored by default until the lab generates the
first batch; once it contains references the workflow flips from
"skip" to "compare every push" behaviour.

### Status

Workflow file present; runner pending. No CI minutes consumed until
a labelled runner comes online.

---

## NVIDIA Windows (Phase 25 / Marathon Run 25, X3)

The `ci-nvidia-windows.yml` workflow targets a self-hosted Windows
runner labelled `windows` + `nvidia-gpu`.  It is the Windows analogue
of the AMD-RADV lane: real NVIDIA hardware exercises code paths the
GH-hosted Windows matrix bypasses (rhi_vulkan + imgui_backend +
wired samples + RT extension surface).

### Hardware

Any NVIDIA RTX 30xx / 40xx (or other RT-capable card).  Tested
baseline:

- NVIDIA RTX 3060 Ti on Windows 11 23H2
- Driver 545.84+ (for VK_KHR_ray_tracing_pipeline + acceleration_structure)

### Software

- Windows 10 / 11 with current NVIDIA Game Ready or Studio driver.
- Visual Studio 2022 (Build Tools or full IDE) -- the `ci-msvc`
  preset path.
- LLVM Clang 16+ -- the `ci-clangcl-win` preset path.
- Vulkan SDK 1.3.290+ -- adds `vulkaninfo` to PATH plus the runtime
  loader.
- CMake 3.28+, Ninja, Python 3.11+ on PATH.

### Runner registration

Follow GitHub-s self-hosted runner instructions.  The required
labels on the runner are:

- `self-hosted` (default)
- `windows`
- `nvidia-gpu`

The workflow `runs-on` matches that exact label triple.

### Matrix

The workflow drives 4 preset combinations in parallel:

| Configure preset       | Build preset                 | Test? | Purpose                              |
|------------------------|------------------------------|-------|--------------------------------------|
| `ci-msvc`              | `ci-msvc-debug`              | yes   | MSVC debug; primary smoke + samples  |
| `ci-clangcl-win`       | `ci-clangcl-win-release`     | no    | Clang-CL release; build-only gate    |
| `ninja-base-asan`      | `ninja-debug-asan`           | yes   | Clang ASan + UBSan                   |
| `ninja-base-tsan`      | `ninja-debug-tsan`           | yes   | Clang TSan                           |

The MSVC lane also runs a sample-smoke step that boots
`hello_engine` for 5 frames on the real GPU and reports its exit
code (phase1139: hello_imgui folded; phase1142: hello_pbr folded).
This is the only path covering full Vulkan rendering on Windows.

### Why a separate lane

`ci.yml`-s Windows matrix runs the same 4 preset combinations on
GH-hosted runners, but those runners:

- Have no GPU; `rhi_vulkan` builds but skips at test load time.
- Cannot run wired samples (window creation works on the headless
  VM but the swapchain has no real adapter to attach to).
- Cannot exercise the RT-pipeline / dispatch_rays path even when
  it lands in v1.7.

The NVIDIA self-hosted lane is the future regression detector for
the GPU-specific surface.

### Golden references

`tests/golden/nvidia/` is the per-vendor reference set the workflow
compares against once captured.  Wired as a follow-up; the
infrastructure is the marathon-shippable piece.

### Status

Workflow file present (this commit); runner pending.  No CI minutes
consumed until a labelled runner comes online.
