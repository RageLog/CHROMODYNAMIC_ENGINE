# ADR-012 — Editor Mechanics

- **Status**: Accepted (Phase 1 Design)
- **Date**: 2026-05-17
- **Related**: ADR-004 (ECS), ADR-006 (Asset), ADR-009 (UI), ADR-015 (Concurrency)

## Bağlam

Editor mimari: **hybrid in-process** + ileride IPC-ready (E5=c). Hot-reload: hepsi (asset + script + shader + DLL). Git first-class. UI: T22.Q6=A custom (ADR-009 dual system). Scripting: hem engine script hem editor-specific (T22.Q4=C both). T22.Q5 gizmo/level editing state-of-art araştır.

## Karar

### A. 3-Katmanlı Mimari

```
cd::editor::gizmo    — composable composable subgizmo
cd::editor::level    — modeling/spline/terrain/decal/lighting/outliner
cd::editor::viewport — orbit/fly/fps/ortho cam, 1/2/4-pane layout
```

Edit pipeline: tüm mutasyonlar `EditOp` event-sourced — undo/redo + IPC + future collab.

### B. Gizmo System

Interactive Tools Framework (`IGizmoStateTarget`, `IGizmoTransformSource`, `IGizmoAxisSource`, `IGizmoClickTarget`) decoupling ilham. Blender modal operator + numeric input ("G, X, 5, Enter") power-user için zorunlu.

```cpp
namespace cd::editor::gizmo {
    struct AxisSource     { Vec3 origin; Vec3 axis; };
    struct PlaneSource    { Vec3 origin; Vec3 normal; };
    struct TransformSource{ virtual Transform get() const = 0; };
    struct TransformTarget{ virtual void apply(const Transform&) = 0; };

    enum class Space    { Local, World, View, Parent, Gimbal, Normal };
    enum class SnapMode { None, Grid, Angle, Scale, Vertex, Surface, Edge };

    class Gizmo { /* hover/active state, raycast */ };
    class TranslateGizmo : Gizmo {};
    class RotateGizmo    : Gizmo {};
    class ScaleGizmo     : Gizmo {};
    class UniversalGizmo : Gizmo { /* hover-detects T/R/S, UE-vari, default */ };

    class GizmoManager {
        PivotMode pivot;   // Median / Active / IndividualOrigins / Cursor
        Space space;
        SnapSettings snap;
        void set_targets(std::span<EntityId>);
    };
}
```

**Kararlar**:
- **Retained-mode** (S9 editor_ui uyumlu) — 3D world space'te "editor entity" olarak yaşar.
- **Multi-select pivot**: Median + Active (Sprint 12 min), Individual Origins + 3D Cursor (Sprint 13).
- **Snap detector** ayrı subsystem: `ISnapProvider` interface — grid/vertex/edge/surface farklı provider, gizmo sorgular. Surface snap için S4 ECS + physics raycast.
- **Universal gizmo default** (modern AAA standart).
- **Numeric input modal** (Blender G,X,5): Sprint 13 ertelenebilir ama API tasarımına bugün dahil.

**IPC-readiness**: per-frame drag IPC için ağır; **uncommitted preview** in-process, **commit** event IPC-serialize.

```
GizmoDragEvent { entity_ids, delta_xform, space, snap_applied }  // local
EditOpCommit  { op_id, entity_ids, before_xform, after_xform }   // IPC + undo
```

### C. Level Editing (T22.Q5)

#### C.1 In-Engine Modeling — Mini ProBuilder

**Karar**: `cd::editor::level::modeling`:
- `HalfEdgeMesh` runtime mesh repr (Blender BMesh subset).
- **Core ops** (8): Extrude, Inset, Bevel, LoopCut, Bridge, Bool(union/sub/intersect), Subdivide, Merge.
- Vertex/edge/face selection mode (Blender 1/2/3 tuş şeması).
- Soft selection (radial falloff) Sprint 13.
- UV editing minimal (auto-unwrap + box-project) Sprint 13.

**Effort**: 3-4 ay engineer; ~30-40 KLOC dahil viewport+gizmo.

**Reddedilen**: Vendor (OpenMesh/CGAL) lisans + STL bağımlılığı; Full ProBuilder klon 12+ ay; Brush-CSG only (Trenchbroom) modern PBR/organic için kısıtlayıcı.

#### C.2 Spline Editor

```cpp
enum class SplineType { Linear, Bezier, CatmullRom, BSpline };
struct ControlPoint { Vec3 pos; Vec3 tan_in, tan_out; float roll; float scale; };
class Spline { std::vector<ControlPoint>; SplineType; bool closed; };
class SplineEditor { /* gizmo-integrated */ };
// Consumers: SplineMeshDeformer, FoliageScatterAlongSpline, DecalRibbon
```

Sprint 12: Bezier + Linear; Sprint 13: Catmull/B-Spline.

#### C.3 Terrain Sculpt

`cd::editor::level::terrain` (Sprint 12-14, ~6 hafta):
- **Heightmap brush**: raise/lower/smooth/flatten/noise (5 brush yeterli).
- **Splat map paint**: 4-8 layer texture splat (RGBA mask per chunk).
- **Foliage paint**: instanced mesh placement (density + scale jitter + slope/altitude rule).
- **Chunked LOD**: 64×64 chunks, quadtree LOD.

Kendi yazılır (vendor terrain license/perf uyumsuz).

#### C.4 Decal Placement

production engines DecalActor + DBuffer pattern. Surface-projected + spline-along-path. Sprint 13.

#### C.5 Light Placement + Probe Bake UI

- Light gizmo: cone/sphere/box viz.
- Probe placement: reflection + GI probes, scatter-on-grid + manual.
- **Bake trigger UI**: progress bar, cancel, incremental rebake. Asıl bake S? GI sisteminin işi (ADR-002).

#### C.6 Outliner

Tree view, drag-drop reparent, multi-select, search/filter, color tag. Layer system (visibility/lock). Collection/Folder. Sublevel/scene streaming (World Partition-vari Sprint 14+).

### D. Camera & Navigation

```cpp
enum class CamMode { Orbit, Fly, FirstPerson, Orthographic };
enum class OrthoView { Top, Bottom, Front, Back, Left, Right };
struct CameraBookmark { Transform xform; float fov; CamMode mode; std::string name; };

class Viewport {
    Camera cam; CamMode mode;
    RenderTarget rt;  // dockable
    void frame_selected(std::span<EntityId>);
    void snap_to_grid();
    void apply_bookmark(int slot);
};

class ViewportManager {
    Layout layout;  // Single | Quad | HSplit | VSplit | Custom
    std::array<Viewport, 4> vps;
};
```

**Kararlar**:
- **4-pane quad mandatory** (Hammer/Trenchbroom LD geleneği; modern UE single-view trendine direnme).
- **Bookmark**: 10 slot (UE-vari) + isimli unlimited.
- **Frame Selected** (F key), **Focus On** (Shift+F orbit lock).
- **Camera speed scroll** (RMB+scroll, UE-vari).
- **Gizmo cube** (Maya ViewCube) ortho navigation faz-2.

### E. Hot-Reload Tools

Tools plugin DLL (`cd::plugin::Loader` ADR-017 P2). State-preserving reload — gizmo state'i kaybetmez (production engines Live++'tan üstün).

### F. Editor Scripting (T22.Q4=C)

Her tool hem C++ hem script API çağrılabilir — Blender Python operator paradigmasını C++-first replicate. Ayrı ADR (scripting).

### G. Aşma Noktaları

1. **C++23 concept-based** gizmo target/snap (vs production engines runtime virtual dispatch).
2. **Hot-reload first** — state-preserving plugin reload.
3. **Editor scripting** C+++script aynı surface (T22.Q4).
4. **IPC-ready EditOp log** — replay + collaborative editing (faz-3).
5. **Quad-view + perspective parite** — LD geleneği koru.
6. **Universal gizmo default** — ayrı T/R/S yerine başlangıç.
7. **Spline + terrain + modeling birleşik selection mod** — Blender ortak, production ayrı (her toolda farklı).

## Reddedilen

- **Vendor terrain/mesh (OpenMesh, CGAL, Recast)**: lisans + STL+exception+template-bloat uyumsuzluğu.
- **Full ProBuilder klonu**: 12+ ay effort; 8-core MVP yeterli.
- **Brush-CSG only (Trenchbroom)**: modern PBR + organic kısıtlayıcı.
- **Single-viewport only (modern UE)**: LD topluluğu quad-view bekliyor.
- **Immediate-mode gizmo (Blender RNA-bound)**: editor retained UI (S9) çakışır.
- **Klasik T/R/S only**: Universal gizmo default.

## Sonuçlar

**Pozitif**: AAA-grade editor temeli, IPC-ready edit log, hot-reload first; tool DLL community-extensible.

**Negatif**: Half-edge + 8 op + spline + terrain + 5 gizmo + viewport = ~3 sprint × tek dev (~12 hafta). 30-40 KLOC eklenir.

**Risk**: Universal gizmo hover heuristic UX testleri gerekir; quad-view layout S9 docking framework'a baskı; terrain chunk streaming render + asset stream koordinasyon.

**Replace-Ready (D1)**: Editor framework saf kod (vendor yok).

## Açık Sorular

| ID | Soru | Çözüm |
|---|---|---|
| Q1 | Mini-ProBuilder MVP Sprint 12'ye sığar mı? | Sprint 13'e bölünme realist |
| Q2 | Universal gizmo default mi ayrı T/R/S default mı? | UX test |
| Q3 | Soft selection + proportional editing MVP'de mi? | Sprint 13 |
| Q4 | Terrain VT mı klasik splat mı? | Klasik splat v1 |
| Q5 | Editor scripting gizmo/tool — C++-first mı script-first mı? | C++-first (T22.Q4=C both ama C++ primary) |
| Q6 | Multi-user collab editing roadmap? | EditOp event-sourced hazırlık var; faz-3 |
| Q7 | Brush CSG (Hammer-vari) ek mod mu? | Plugin |
| Q8 | World Partition / sublevel streaming Sprint? | Sprint 14-15 |
| Q9 | Foliage instanced render bağımlılığı? | RHI instancing pipeline |
| Q10 | Quad-view 4× render target VRAM cost? | Mitigation: dropping LOD per viewport |

## Cross-Cutting

- **ADR-009 (UI editor retained)**: gizmo manager dock, outliner dock, viewport dock — S9 docking framework + property panel. **Hard dependency.**
- **ADR-004 (ECS)**: gizmo target = `EntityId`; transform component mutation; spline/terrain/light entity tipleri. EditOp ECS command buffer. **Hard dependency.**
- **ADR-006 (Asset)**: asset browser drag-drop → viewport placement; terrain texture splat layer asset, foliage prefab, decal material. Hot-reload (asset değişince viewport refresh). **Hard dependency.**
- **Physics/Raycast (ADR-008)**: vertex/surface snap, click-to-select picking → broadphase raycast veya GPU-id-buffer.
- **ADR-002 (Renderer)**: editor overlay pass (gizmo geometry, grid, wireframe, selection outline), per-viewport RT, foliage instancing, terrain rendering.

## Kanıt

- Interactive Tools Framework: dev.epicgames.com/documentation
- Blender BMesh: docs.blender.org/api/current/bmesh.html
- ProBuilder: docs.unity3d.com/Packages/com.unity.probuilder
- Trenchbroom: github.com/TrenchBroom/TrenchBroom
- Conner et al. (1992) Three-Dimensional Widgets SI3D — **STUB**
- Schmidt et al. (2008) Sketching and Composing Widgets — **STUB**
- Botsch & Kobbelt (2010) Polygon Mesh Processing — book
- Losasso & Hoppe (2004) Geometry Clipmaps — **STUB**
