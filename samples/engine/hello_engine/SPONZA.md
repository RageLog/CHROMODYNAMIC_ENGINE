# Sponza Atrium — hello_engine Test Environment

## What is Sponza?

**Sponza Atrium** is the canonical architectural test scene used by virtually every
modern rendering engine (Unreal Engine, Unity HDRP, Filament, bgfx, The Forge) for:

- PBR material validation (stone, fabric, metal)
- IBL (image-based lighting) correctness verification
- Directional + area light shadow evaluation
- Multi-material scene complexity benchmarking

The variant used here is the **Khronos glTF-Sample-Models 2.0** port (~50 MB,
~260 k triangles, 69 texture files), derived from the original Crytek Sponza.

## License Attribution

> "Sponza Atrium" by Frank Meinl / Crytek.
> glTF 2.0 conversion by the Khronos Group and contributors.
> Licensed under **CC BY 3.0** — https://creativecommons.org/licenses/by/3.0/

Original source: https://github.com/KhronosGroup/glTF-Sample-Models/tree/main/2.0/Sponza

## How Auto-Load Picks Sponza

`HelloGltf.hpp::try_auto_load_gltf` iterates a candidate list in order. Sponza
entries are placed **before** all other assets:

```
Priority 1: "assets/samples/Sponza/Sponza.gltf"        (CWD-relative)
Priority 2: "../../../../assets/samples/Sponza/Sponza.gltf"  (build-tree relative)
Priority 3: absolute path fallback
...
Priority N: CesiumMan.glb (original fallback, preserved)
```

When Sponza is present, it loads first. When absent, the engine falls back
gracefully to the next available asset (CesiumMan, DamagedHelmet, etc.).

## Getting the Assets

Assets are gitignored (~50 MB). Fetch from repo root:

```powershell
# Windows PowerShell
powershell -ExecutionPolicy Bypass -File scripts/fetch_sponza.ps1
```

```bash
# Bash (Linux / macOS / Git-for-Windows)
bash scripts/fetch_sponza.sh
```

Both scripts are idempotent. Re-running skips already-present files.

See `assets/samples/Sponza/README.md` for full attribution and expected file list.

## Canonical Camera Positions

### View 1 — Looking Down the Nave (recommended first view)

The classic Sponza benchmark framing:

```
eye    : (0.0, 1.5, 0.0)
target : (10.0, 1.5, 0.0)
up     : (0, 1, 0)
fov    : 60 deg
```

In hello_engine free-look: start at origin, pitch flat, yaw toward +X.
This view shows the full column row, stone arches, and fabric drapes lit by sun.

### View 2 — Column + Area Light Detail

```
eye    : (3.0, 2.0, -1.0)
target : (0.0, 1.5,  0.0)
fov    : 60 deg
```

Useful for inspecting stone material PBR (roughness, metallic) under the
default cyan + magenta area light panels.

### View 3 — Looking Up at the Arches

```
eye    : (0.0, 0.5, 0.0)
target : (0.0, 6.0, 0.0)
fov    : 75 deg
```

Validates IBL indirect specular on curved ceiling geometry.

## Known Limitations

| Limitation | Root Cause | Workaround |
|---|---|---|
| Some triangles disappear near index budget | `uint16_t` index limit (65535 verts) — Sponza exceeds this when merged | Tracked follow-up: switch to `uint32_t` |
| All geometry shaded with first material's texture | `try_auto_load_gltf` applies only material[0] baseColor | Full per-primitive dispatch tracked as follow-up |
| Vegetation appears opaque (no alpha cutoff) | PBR shader does not evaluate `alphaCutoff` | Plants render as solid silhouettes — known shader gap |
| No per-instance transform | Loader merges all primitives into single GpuMesh | Scene-graph import tracked as future work |

## Rendering Observations

- **IBL**: the sun + IBL bake illuminates the atrium walls and column capitals
  with correct indirect specular. Look for the characteristic warm highlights on
  the carved stone.
- **Shadow map**: the directional sun shadow cascades across the floor flags.
  With the default sun direction, stripes of shadow fall between the columns —
  the canonical Sponza shadow pattern.
- **Area lights**: the default cyan + magenta rect lights are visible on the
  near wall surfaces when positioned inside the atrium entrance.
- **PBR materials**: stone columns show correct roughness (high roughness →
  matte diffuse-dominant response). Fabric drapes show softer specular.
