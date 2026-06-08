# SOTA: 3D Viewport Debug Visualisation for Runtime Data Structures

**Access date for all web sources: 2026-06-08**
**Engine grounding: CHROMODYNAMIC dev branch, phases 860–893**
**Researcher agent run: 2026-06-08 (Run 27 Strand C prep)**

---

## §1 Executive Summary — Highest-Payoff Systems

Three systems give the clearest "show me that my system is working" signal relative to implementation cost:

1. **DDGI probe grid** — renders 256 coloured spheres, one per `ProbeGrid` probe. Colours are directly sampled from the irradiance atlas. The user can immediately see whether GI is converging, which probes are in shadow, and whether the grid covers the scene. CHROMODYNAMIC already has `probe_world_pos()` and the atlas UV math in `Ddgi.hpp`; the geometry generation is a single instanced draw with one sphere mesh and an SSBO of probe world positions.

2. **Frustum culling AABB** — every visible-cluster or entity AABB drawn as a wireframe box, colour-coded green/yellow/red by cull result. Directly reveals whether `kClusterCullCS` is over-culling, and whether `cd::camera::test_aabb` is correct. The data (`m_visible` cluster index list from `GpuDispatcher`) already exists on the CPU.

3. **Meshlet colour-coding** — the cluster mesh shader already writes `cluster_id` into `kMaterializerFS`. A trivial permutation (`#define CD_DEBUG_CLUSTER_COLOR 1`) that replaces material lookup with `hash_to_color(cluster_id)` makes every meshlet boundary visible. No extra geometry, no extra pass — just a shader variant.

---

## §2 DDGI Probe Grid

### Reference engine: Godot 4 (SDFGI) + NVIDIA RTXGI

**Godot 4** ships `sdfgi_debug_probes.glsl` (4.2 branch). It uses a **procedural sphere tessellation** driven purely by `gl_InstanceIndex` — no vertex buffer. The instance ID encodes both the probe grid coordinate and the sphere-surface angle.

Two modes: `MODE_PROBES` samples the irradiance atlas (octahedral decode) to colour each sphere with its actual irradiance, `MODE_VISIBILITY` shows green→red occlusion weight.

**NVIDIA RTXGI-DDGI** (GitHub: NVIDIAGameWorks/RTXGI-DDGI) follows the same instanced-sphere pattern in HLSL; probes are drawn as a single `DrawInstanced(sphere_tris, probe_count)` with the probe world positions in an SSBO/SRV.

### What the user sees

256 small spheres (radius ≈ 0.15 m) floating at each `probe_world_pos()` location. Converged probes glow with their environment's dominant indirect colour. Unlit corners show near-black spheres, sky-facing probes show sky blue. Convergence age could be encoded as sphere radius or alpha fade.

### Minimal implementation sketch

```text
1. Build per-probe world-pos SSBO from ProbeGrid::probe_world_pos(px,py,pz)
2. Bind irradiance atlas as sampler2D
3. DrawInstanced(sphere_vertex_count, grid.probe_count())
   - VS: probe_pos = ssbo[gl_InstanceIndex]; position = probe_pos + sphere_vertex * 0.15
   - FS: uv = oct_encode(normal); color = texture(irradiance_atlas, atlas_uv(instance, uv))
```

### Performance budget

Debug-only. Godot gates this behind `Environment.sdfgi_show_cascades = true`. At 256 probes and a ~200-tri sphere, total vertex count is ~50 K — negligible.

### Pitfalls

- **Probe-vs-world-locked sphere**: the sphere vertex shader must add world-space probe pos to a local-space hemisphere. Do NOT billboard-face-camera — the probe is a 3D object with real lighting.
- **Atlas UV mismatch**: `probe_uv()` in `ProbeAtlas` uses `flat_index` order `px + probes_x * (py + probes_y * pz)`. The debug shader must use the same order or colours will bleed between probes.
- **Z-fighting**: probes that sit inside geometry fight the scene depth. Add a small `depth_bias` or set `DepthFunc = LessEqual` + tiny positive offset.

---

## §3 Frustum Culling — Wireframe AABB / Cluster Grid

### Reference engine: Bevy (bevy_gizmos/src/aabb.rs) + bgfx DebugDrawEncoder

**Bevy** (MIT) adds two ECS systems: `draw_aabbs` queries `ShowAabbGizmo` entities; `draw_all_aabbs` queries all `Aabb` when `AabbGizmoConfigGroup::draw_all = true`. Implementation calls `gizmos.aabb_3d()` which expands to 12 line segments. Entity-level `ViewVisibility` is checked before drawing so culled entities skip the gizmo draw too.

**bgfx DebugDrawEncoder** (MIT, bkaradzic/bgfx): `void draw(const bx::Aabb&)` draws 12-edge wireframe using `setWireframe(true)`. `setDepthTestLess(bool)` controls depth occlusion.

### What the user sees

Every cluster AABB drawn as a thin wireframe box. Green = inside frustum, yellow = intersecting, red = outside. At 3456 clusters (16×9×24) this gives an instant density map of where the frustum boundary falls.

### Minimal implementation sketch

```text
1. After GpuDispatcher::dispatch_cull(), CPU side has m_visible[] IDs
2. Walk all cluster bboxes: cull_result = test_aabb(frustum, bmin, bmax)
3. Append 12 line segments per box into a line-list vertex buffer
4. Colour: kInside→green, kIntersecting→yellow, kOutside→red
5. Draw as line list with depth-test enabled, depth-write disabled
```

### Pitfalls

- **Line width**: Vulkan core has no `lineWidth > 1` without `wideLines` feature. Use thin lines or draw each edge as a thin quad.
- **Overdraw at far clusters**: alpha-blend line draw at 50% opacity avoids the black mass.

---

## §4 GPU Particles — Billboard / Sphere in 3D

### Reference engine: Wicked Engine + opengl-tutorial.org

**Wicked Engine** GPU particles: after the simulation compute compacts alive particles into the head of the buffer, a single `DrawInstancedIndirect` renders `alive_count × 6` vertices (two triangles per billboard). The VS reads `gl_InstanceID` to fetch `particle[gl_InstanceID].position` and applies a camera-aligned billboarding matrix. No geometry shader — expansion is in the VS using `gl_VertexID % 6` to pick the corner offset.

### What the user sees

Actual coloured quads floating at each alive particle position, sized by `particle.max_life` ratio, alpha-faded by `particle.color.w`.

### Pitfalls

- **Camera-aligned vs world-locked**: cam_right + cam_up corners require the camera basis in a UBO. If the UBO lags the composite push constant, particle billboards will be 1 frame out of phase.
- **Alive-count race**: do not read `alive_count` from the CPU if `compact_alive()` ran on the GPU. Use `DrawIndirect` driven by the GPU-side counter.

---

## §5 Cascaded Shadow Map (CSM) Splits

### Reference engine: LearnOpenGL CSM tutorial + handmade.network debug post

Standard approach: extract the 8 corners of each cascade sub-frustum in world space, then draw 12 wireframe edges per frustum. `CascadedShadow.hpp` already computes `CascadeSplit::light_view_proj`; the cascade frustum corners can be recovered by inverse-projecting the NDC unit cube through the light VP matrix.

### What the user sees

Four concentric wireframe frustum pyramids overlaid on the scene, each coloured by cascade index (red→orange→yellow→green).

### Pitfalls

- **Light-space vs world-space**: `CascadeSplit::light_view_proj` is the light's combined matrix. To get world-space corners, inverse-transform NDC `[-1,+1]^3` through this matrix.
- **Z-fighting with shadow map quads**: draw with depth bias or foreground depth priority.

---

## §6 Light Cluster Grid — Froxel Wireframe + Light Count

### Reference engine: Unity HDRP Rendering Debugger

Unity HDRP (15.0.7 / 17.0.4) has "Fullscreen Debug Mode → Light Cluster". This is a **fullscreen screen-space overlay shader**: each pixel samples the cluster index from a `usampler3D`, looks up the light count for that cluster, and outputs a heat-map colour. For a 3D wireframe, a separate pass emits cluster-edge line segments using the GPU-driven line renderer pattern.

### What the user sees

Option A (screen-space heat map): fullscreen overlay where each pixel is coloured by the light count of the cluster it falls in. Red cells = saturated.

Option B (3D wireframe): 16×9×24 = 3456 cluster AABBs drawn as wireframe boxes.

### Pitfalls

- **Cluster index formula mismatch**: `linear_cluster_index = (z * y_tiles + ty) * x_tiles + tx` must match exactly the formula used in `assign_light()`.
- **Log-Z boundary at far plane**: clusters near far plane have very large Z extent. Clamp the cluster depth.

---

## §7 Meshlets — Per-Cluster Colour Code

### Reference engine: Nanite (Karis, Stubbe, Wihlidal — SIGGRAPH 2021) + nanite-webgpu (Scthe)

Karis 2021 §4 (advances.realtimerendering.com/s2021) describes a debug mode where the materializer FS replaces the PBR BRDF with `hash_to_rgb(cluster_id)`. A 64-entry LUT of perceptually distinct colours is indexed by `cluster_id % 64`.

The open-source **nanite-webgpu** (github.com/Scthe/nanite-webgpu, MIT) implements this: the visibility buffer FS reads `cluster_id` from the R32 target and outputs `palette[cluster_id % palette_size]`.

### What the user sees

The Sponza mesh drawn in a bright patchwork of 64+ distinct colours. Each meshlet boundary is a sharp colour transition. Cluster LOD boundaries are clearly visible as the camera moves.

### Pitfalls

- **Cluster IDs are not stable across frames** if the visibility buffer is rebuilt each frame with a GPU atomic counter. Use a per-cluster stable ID from the DAG.
- **Visibility buffer sentinel 0**: guard with `if (packed == 0u) discard;` before the colour lookup.

---

## §8 Navigation Mesh / Pathfinding A*

### Reference engine: Godot 4 NavMesh debug overlay

Godot draws a translucent green fill + slightly brighter wireframe outline per triangle, then blue lines for the path.

### What the user sees

Translucent green/blue triangles overlaid on top of the scene floor. Path waypoints connected by thick accent-coloured line segments.

### Pitfalls

- **Z-fighting with floor geometry**: add a small world-space Y-offset (0.01 m).
- **CPU-to-GPU vertex upload bandwidth**: rebuild the navmesh VBO only when `set_navmesh()` is called.

---

## §9 Decal OBBs — Wireframe Projection Volumes

### Reference engine: DirectXTK DebugDraw + Unreal FComponentVisualizer

**DirectXTK DebugDraw** (MIT, microsoft/DirectXTK): `Draw(PrimitiveBatch*, BoundingOrientedBox, color)` — extracts the OBB quaternion, constructs 8 rotated corners, draws 12 line segments.

### What the user sees

A wireframe box at each decal position showing exactly what volume will receive the projected texture. Projection direction shown as a short arrow.

### Pitfalls

- **OBB vs AABB confusion**: do NOT draw an AABB around the OBB.
- **Forward direction arrow Z-fight**: use foreground depth priority.

---

## §10 Area Lights (LTC) — Polygon Visualisation

### Reference engine: Unity Gizmos API + Filament Editor

Unity's `Gizmos.DrawWireQuad()` / `Gizmos.DrawWireSphere()` for light shapes. The polygon vertices are projected into screen space and drawn as a lit wire outline.

### What the user sees

A glowing wire rectangle at the area light position. When selected, the rectangle is solid-filled with the light's colour at reduced intensity. Emission arrow perpendicular to the plane.

### Pitfalls

- **Polygon winding for backface culling**: use `CullNone` rasterizer state for debug overlays.
- **HDR light intensity bleeds into wireframe colour**: clamp the wire colour to SDR `[0,1]`.

---

## §11 Volumetric Fog Froxel Grid

### Reference engine: Frostbite (Wronski 2014) + diharaw/volumetric-fog

Frostbite's debug is a **screen-space slice viewer**: a fullscreen FS samples the 3D LUT at a fixed slice `z = params.slice` and outputs the accumulated in-scatter colour. The user advances the slice with a slider to scroll through the fog volume.

### What the user sees

A fullscreen overlay showing one depth-slice of the froxel grid. Colour encodes in-scattered radiance.

### Pitfalls

- **No 3D LUT yet in CHROMODYNAMIC**: the current fog is inline compute in `kCompositeFS`. The slice viewer requires the 3D texture to exist.
- **Quadratic Z warp**: `slice_to_view_z(s, g) = near + (far - near) * s²`.

---

## §12 Recommended Attack Order for CHROMODYNAMIC hello_engine — Run 27

### Tier 1 — Ship in Run 27

1. **Meshlet colour-coding (§7)** — Effort: 1 hour. The cluster_id is already in the visibility buffer. One shader `#define` + a pipeline variant swap.
2. **DDGI probe sphere overlay (§2)** — Effort: 0.5 day. `probe_world_pos()` is ready in `Ddgi.hpp`. One sphere mesh + SSBO of probe positions + instanced draw + FS that samples atlas.
3. **Frustum AABB wireframe (§3)** — Effort: 0.5 day. `GpuDispatcher::visible_clusters()` returns the visible ID list. Debug line-list VBO + loop emitting 12 edges per cluster AABB.

### Tier 2 — Run 28+ (require more wiring)

- **CSM split visualisation (§5)** — blocked on 8-corner unproject helper from `CascadeSplit::light_view_proj`.
- **Light cluster heat-map (§6)** — needs `cd::render::lighting_clusters` GPU froxel SSBO readback.
- **NavMesh 3D injection (§8)** — `PathfindingViz` Sprint-2 already exists; upgrade panel-projection to scene-viewport injection.
- **Decal OBB wireframe (§9)** — trivial geometry, low priority.
- **GPU particle billboard (§4)** — wires naturally once `kSimulateCS` drives `DrawIndirect`.
- **Area light polygon (§10)** and **Froxel fog slice (§11)** — blocked on production render path.

---

**Next**: Tier-1 implementation can begin immediately against existing `GpuDispatcher`, `ProbeGrid`, `ClusterGrid` headers with no new library added. The debug line renderer should be factored as a standalone `cd::debug::LineRenderer` to serve all 10 systems.

## Sources

- [Godot 4.2 SDFGI Debug Probes Shader](https://github.com/godotengine/godot/blob/4.2/servers/rendering/renderer_rd/shaders/environment/sdfgi_debug_probes.glsl)
- [bgfx DebugDraw Header (bkaradzic)](https://github.com/bkaradzic/bgfx/blob/master/examples/common/debugdraw/debugdraw.h)
- [bgfx DebugDraw Example 29](https://github.com/bkaradzic/bgfx/blob/master/examples/29-debugdraw/debugdraw.cpp)
- [NVIDIA RTXGI-DDGI Repository](https://github.com/NVIDIAGameWorks/RTXGI-DDGI)
- [Bevy Engine AABB Gizmo (bevy_gizmos/src/aabb.rs)](https://github.com/bevyengine/bevy/blob/main/crates/bevy_gizmos/src/aabb.rs)
- [Bevy Gizmos 3D Example](https://github.com/bevyengine/bevy/blob/main/examples/gizmos/3d_gizmos.rs)
- [DirectXTK DebugDraw Wiki](https://github.com/microsoft/DirectXTK/wiki/DebugDraw)
- [GPU-Driven Debug Line Renderer — Gijs Kaerts](https://www.gijskaerts.com/wordpress/?p=190)
- [Unity HDRP Rendering Debugger — Light Cluster (15.0.7)](https://docs.unity3d.com/Packages/com.unity.render-pipelines.high-definition@15.0/manual/Render-Pipeline-Debug-Window.html)
- [Nanite: A Deep Dive — Karis, Stubbe, Wihlidal — SIGGRAPH 2021](https://advances.realtimerendering.com/s2021/Karis_Nanite_SIGGRAPH_Advances_2021_final.pdf)
- [nanite-webgpu (Scthe) — open-source Nanite implementation](https://github.com/Scthe/nanite-webgpu)
- [Unreal Engine Component Visualizers — Quod Soler](https://www.quodsoler.com/blog/unreal-engine-component-visualizers-unleashing-the-power-of-editor-debug-visualization)
- [Wicked Engine GPU Particle System](https://wickedengine.net/2017/11/gpu-based-particle-simulation/)
- [diharaw/volumetric-fog — OpenGL froxel fog demo](https://github.com/diharaw/volumetric-fog)
- [Frostbite Physically Based Volumetric Rendering — SIGGRAPH 2015 (Wronski)](https://www.slideshare.net/DICEStudio/physically-based-and-unified-volumetric-rendering-in-frostbite)
- [Handmade Network — Debugging CSM](https://handmade.network/p/75/monter/blog/p/2770-debugging_cascaded_shadow_map)
- [Unreal Engine DrawWireBox API (5.1)](https://docs.unrealengine.com/5.1/en-US/API/Runtime/Engine/DrawWireBox/)
