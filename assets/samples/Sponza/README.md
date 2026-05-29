# Sponza Atrium — Asset Placeholder

This directory holds the **Khronos glTF-Sample-Models 2.0 Sponza** scene used as the
primary test environment for `hello_engine`. The binary assets (textures, geometry,
`.gltf`, `.bin`) are **gitignored** (total ~50 MB) and must be fetched separately.

## License & Attribution

**Sponza Atrium** was originally created by **Frank Meinl** at **Crytek** and released
under the **Creative Commons Attribution 3.0 Unported (CC-BY 3.0)** license.

The glTF 2.0 conversion was authored by the **Khronos Group** and contributors to
[glTF-Sample-Models](https://github.com/KhronosGroup/glTF-Sample-Models).

Attribution required per CC-BY 3.0:

> "Sponza Atrium" by Frank Meinl / Crytek, converted to glTF 2.0 by the Khronos Group.
> Licensed under CC BY 3.0 — https://creativecommons.org/licenses/by/3.0/

Original source: https://github.com/KhronosGroup/glTF-Sample-Models/tree/main/2.0/Sponza

## Download

### PowerShell (Windows)

```powershell
# From repo root:
powershell -ExecutionPolicy Bypass -File scripts/fetch_sponza.ps1
```

### Bash (Linux / macOS / Git-for-Windows)

```bash
# From repo root:
bash scripts/fetch_sponza.sh
```

Both scripts are **idempotent** — re-running skips files already present.

### Expected files after download

| File | Size (approx) |
|------|--------------|
| `Sponza.gltf` | 163 KB |
| `Sponza.bin` | 9.3 MB |
| 68 texture JPG/PNG files | ~40 MB |
| **Total** | **~50 MB** |

Reference git SHA of `Sponza.gltf`: `02252eba518f40cce4b36d45000c7f21139e5494`
Reference git SHA of `Sponza.bin`:  `caa2a36d0309aa985adadd879991a4b3399c4ef4`

## What is Sponza?

The **Sponza Atrium** is the canonical architectural scene used by virtually every
modern rendering engine (Unreal Engine, Unity HDRP, Filament, bgfx, The Forge) for
PBR material validation, IBL/shadow testing, and area light evaluation.

- **Geometry**: stone columns, arched ceilings, fabric drapes, carved ornaments,
  potted plants, flagstone floor — ~260 k triangles in the Khronos 2.0 variant
- **Materials**: PBR (baseColor + metallic/roughness + normal maps), vegetation
  with alpha-test, fabric with anisotropy hints
- **Scale**: the atrium interior is roughly 24 m × 11 m × 10 m (Sponza local units)
  which maps to approximately `24 × 11 × 10` in the Khronos export

## hello_engine Auto-Load Behaviour

`HelloGltf.hpp::try_auto_load_gltf` checks candidate paths in order. Sponza is
placed **first** in the list:

1. `assets/samples/Sponza/Sponza.gltf` (absolute from CWD)
2. `../../../../assets/samples/Sponza/Sponza.gltf` (relative, for out-of-tree builds)
3. Absolute path fallback: `C:/UserFiles/Project/CHROMODYNAMIC_ENGINE/assets/samples/Sponza/Sponza.gltf`
4. CesiumMan.glb, DamagedHelmet, FlightHelmet, … (existing fallbacks)

When Sponza is downloaded, it loads automatically on next `hello_engine` launch.
When Sponza is absent, the engine falls back gracefully to CesiumMan or any other
asset present in `assets/samples/`.

## Canonical Camera Positions

### View 1 — Looking down the nave (east-west axis)

The classic Sponza framing used in most benchmark screenshots:

```
Position : X=0,  Y=1.5, Z=0    (centre of atrium, eye height)
Look-at  : X=10, Y=1.5, Z=0    (down the long axis)
FOV      : 60 deg
```

In hello_engine free-look mode: spawn at origin, pitch flat, yaw 0 deg.

### View 2 — Column detail with IBL

```
Position : X=3,  Y=2.0, Z=-1
Look-at  : X=0,  Y=1.5, Z=0
```

Useful for inspecting stone material PBR response under area light.

### View 3 — Looking up at the arches

```
Position : X=0,  Y=0.5, Z=0
Look-at  : X=0,  Y=6.0, Z=0    (straight up)
```

Validates IBL indirect specular on the curved ceiling geometry.

## Known Limitations

1. **Index overflow**: `HelloGltf.hpp` uses `uint16_t` indices. Sponza has more than
   65535 unique vertices across all merged primitives. The current loader silently
   drops triangles that would exceed the 16-bit limit. The geometry still renders
   but some primitives near the mesh budget will be clipped.
   - Fix: switch to `uint32_t` indices (tracked as a follow-up).

2. **Alpha-test vegetation**: the potted plants use `alphaMode: MASK` with a
   `alphaCutoff` threshold. The current PBR shader does not evaluate alpha cutoff,
   so foliage appears as opaque silhouettes. This is a known limitation of the
   current shader — not a loader bug.

3. **Multiple materials per mesh**: `try_auto_load_gltf` picks only the **first**
   material's baseColor texture and applies it uniformly. Sponza has ~28 distinct
   materials; all geometry renders with the first material's texture only.
   Full per-primitive material dispatch is tracked as a follow-up.

4. **Scale**: Sponza exports in centimetre units in some variants. If the scene
   appears very small (< 1 unit), apply a `100x` scale to the transform. The
   Khronos 2.0 variant used here exports in metres; no scale adjustment needed.
