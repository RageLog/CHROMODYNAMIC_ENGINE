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
