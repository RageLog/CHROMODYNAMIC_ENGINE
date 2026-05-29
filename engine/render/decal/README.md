# cd::decal

**Purpose**: Screen-space deferred decals (Persson 2009). Renders decals (paint splatters, bullet holes, stickers) into G-Buffer in deferred rendering pipeline. Enables efficient decal placement without geometry creation; decals use existing normal/depth.

**Namespace**: `cd::decal`.

**Public Headers** (Header-only):
- `cd/decal/Decal.hpp` — decal descriptor (position, rotation, scale, material ID, fade distance).

**Primary Types**:
- `Decal` — decal instance (transform, material reference, lifetime).
- `DecalBatch` — collection of active decals for this frame.

**Build**:
```bash
cmake --build --preset ninja-debug --target cd_decal
ctest --preset ninja-debug -R decal --output-on-failure
```

**Dependencies**: cd::core, cd::math.

**Rendering** (Pipeline Integration):
- Decals rendered as screen-space quads in a separate G-Buffer pass.
- Each decal: project world-space quad → screen space → test depth/normal vs G-Buffer.
- Output: diffuse + normal modifications (blended into G-Buffer).

**Algorithm** (Persson 2009):
- Per-decal quad: world space → clip space → screen space.
- Compute screen-rect coverage; for each pixel in rect:
  - Check G-Buffer depth (is this pixel in front of / behind decal?).
  - Compute decal UV (convert screen-space pixel to decal-local UVs).
  - Sample decal texture; blend into albedo / normal / roughness.
- Soft fade-out based on distance to decal edges (anti-aliasing).

**Notes**:
- Header-only; no .cpp (all logic in shaders + inline types).
- Decal lifetime managed by application (add / remove each frame).
- Up to ~256 active decals per frame (limited by shader buffer).
- Real-time decal placement: pick surface, create Decal struct, add to batch.
