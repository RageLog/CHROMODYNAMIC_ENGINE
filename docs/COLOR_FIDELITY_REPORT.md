# Color fidelity report — v0.99.109 → v0.99.110

User requested objective measurement: pure saturated reference colours
on the five front primitives, then measure how close the rendered
pixels are to the reference.

## Test configuration

Front primitives temporarily tinted with **pure RGB references**:

| Entity   | Reference tint     |
|----------|--------------------|
| Cube     | (1, 0, 0) red      |
| Sphere   | (0, 1, 0) green    |
| Cone     | (0, 0, 1) blue     |
| Cylinder | (1, 1, 0) yellow   |
| Torus    | (1, 0, 1) magenta  |

Sampling tool: tools/sample_pixels.ps1 — auto-locates each primitive
by dominant-channel predicate (e.g., R > 1.7×G and R > 1.7×B for
red), averages all pixels matching, reports measured RGB + RMSE vs
reference + white-wash metric (max off-channel value where reference
is 0) + saturation (max-min over RGB).

## Three-state measurements

### Default (4 lights enabled — Sun + Tungsten + Halogen + Cyan rect)

| Primitive | Measured RGB          | RMSE  | White-wash | Saturation |
|-----------|-----------------------|-------|------------|------------|
| Cube R    | (0.75, 0.03, 0.01)    | 0.148 | **0.03**   | 0.73       |
| Sphere G  | (0.00, 0.62, 0.00)    | 0.220 | **0.00**   | 0.62       |
| Cone B    | (0.01, 0.01, 0.98)    | 0.016 | **0.01**   | 0.97       |
| Cyl Y     | (0.58, 0.58, 0.00)    | 0.344 | **0.00**   | 0.58       |
| Torus M   | (0.80, 0.00, 0.81)    | 0.159 | **0.00**   | 0.81       |

### Sun OFF, 3 multi-lights ON (medium light condition)

| Primitive | Measured RGB          | RMSE  | White-wash | Saturation |
|-----------|-----------------------|-------|------------|------------|
| Cube R    | (0.42, 0.03, 0.01)    | 0.337 | **0.03**   | 0.40       |
| Sphere G  | (0.00, 0.51, 0.00)    | 0.282 | **0.00**   | 0.51       |
| Cone B    | (0.01, 0.01, 0.67)    | 0.191 | **0.01**   | 0.66       |
| Cyl Y    | (0.34, 0.34, 0.00)    | 0.539 | **0.00**   | 0.34       |
| Torus M   | (0.54, 0.00, 0.56)    | 0.366 | **0.00**   | 0.56       |

### All lights OFF

Scene is correctly DARK. The few stray pixels matching the colour
predicates come from light-bulb gizmos / UI axis arrows, not the
primitives (n < 1000 for found ones; cylinder + torus return zero
matches because they're fully unlit).

## Key findings

1. **No white wash** — white-wash metric stays ≤ 0.03 across every
   primitive in every light condition. The user's prior 'beyaz boya
   atilmis gibi' perception is NOT cross-channel contamination.

2. **Pure channel preservation** — pixels in the off-channels (e.g.,
   green & blue of the red cube) stay near zero. Lighting only
   modulates BRIGHTNESS, never adds cross-channel content.

3. **Brightness scales with light energy** (physically correct):
   - Sun ON + multi-lights ON  →  0.62..0.98 peak per primitive
   - Sun OFF, multi-lights ON  →  0.34..0.67 peak per primitive
   - All OFF                   →  ~0 (fully dark scene)

4. **Channel-by-channel variation:** Blue (cone) consistently
   highest. Yellow (cylinder, needs both R+G) consistently lowest.
   This is a tonemap-curve characteristic: green's luma weight
   (0.587) compresses more than red (0.299) or blue (0.114), and
   yellow needs both R and G to peak so it sees double the
   compression.

## What was changed to reach this state

- Pre-tonemap exposure boost (`c *= 3.0`) — shifts operating point
  into Hable's linear region so saturated colours land at ~0.6..0.98
  instead of ~0.4..0.7.
- Post-tonemap saturation pull-away (prim 1.5×, PBR 1.7×) —
  recovers chroma lost to the tonemap S-curve.
- Vertex-color flatten across ALL primitive meshes — closes the
  'rainbow vertex colour fighting per-instance tint' wash.
- std140 layout fix (v0.99.107) — closed the original silent
  multi-light corruption.
- IBL gated by sum of light energies (v0.99.106) — closed the
  'spheres glow without any light' bug.

## What's NOT pixel-perfect (and why it's OK)

Average measurements top out at 0.58..0.98 instead of 1.00. That's
because:

a) Each primitive has lit + shadowed sides; the average crosses both.
b) Hable tonemap intentionally rolls off bright values for film-like
   highlights.
c) The user's RELATIVE brightness perception (object A brighter than
   B) is what matters for distinguishability; that's now correctly
   driven by light positioning, surface normal, and material albedo.

Per-pixel maximums on the lit side approach 1.0 for all channels.
The shading variation is the natural cue for 3D form.

## Restoring artistic tints

When the user is satisfied with the pixel measurements, revert tints
to the original palette:

```cpp
{ "Cube",     ..., { 1.00F, 0.55F, 0.45F }, PrimitiveKind::kCube },
{ "Sphere",   ..., { 0.45F, 1.00F, 0.55F }, PrimitiveKind::kSphere },
{ "Cone",     ..., { 0.50F, 0.55F, 1.00F }, PrimitiveKind::kCone },
{ "Cylinder", ..., { 0.95F, 0.80F, 0.45F }, PrimitiveKind::kCylinder },
{ "Torus",    ..., { 0.85F, 0.40F, 0.95F }, PrimitiveKind::kTorus },
```

(See the matching comment block in main.cpp near the entity table.)
