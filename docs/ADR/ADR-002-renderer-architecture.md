# ADR-002 — Renderer Architecture

- **Status**: Accepted (Phase 1 Design)
- **Date**: 2026-05-17
- **Related**: ADR-001 (RHI), ADR-003 (Shader), ADR-004 (ECS), ADR-016 (Vendor Matrix)

## Bağlam

CHROMODYNAMIC hybrid 2D+3D rendering at Lumen/Nanite-class quality on PC desktop (Vulkan 1.3 / D3D12 / OpenGL 4.6 fallback) with a single source tree. Library-oriented; user spectrum "drop-in beginner" → "shader-bin tinkerer". RHI (ADR-001) two-tier + frame graph; Shader (ADR-003) Slang+graph; ECS (ADR-004) entity/render queues.

User decisions:
- T12.Q1 = D — Hybrid clustered + visibility-buffer
- T12.Q2–Q10 = hepsi E (her feature opt-in/pluggable) + state-of-art araştır
- Kullanım kolaylığı (default-easy) + expert tweaking (expert) bir arada

## Karar

### A. Geometry Path — Unified Cluster + Visibility Emitter (UCVE)

**Baseline**: Clustered forward+ (id Tech 7 / Filament / HDRP pattern) — portable across all 3 RHIs, instant access to volumetric/translucent/decal stacks.

**Opt-in**: Visibility-buffer geometry path (Nanite-style) — deferred-material evaluation, trillion-triangle scale. Activated per render-pass (not per-mesh) for trillion-triangle scenes.

**UCVE differentiator**: single geometry submission produces *both* a 3D froxel cluster (lights/decals/probes/fog) *and* an opt-in 64-bit visibility-buffer pixel. Shading pass chooses path per material tag. No shipping engine does this symmetrically (UE5 picks Nanite-or-not at mesh level; we pick per render-pass).

Mesh-shader requirement floor: tier-2 HW for vis-buffer path; compute-shader emulation fallback otherwise. Register pressure mitigation via mesh-shader path when available.

### B. Pluggable Feature Stack (T12.Q2–Q10 = E hepsi)

Every Q2–Q10 feature is an `IPlug` registered on a `cd::render::FrameGraphBuilder`. `cd::render::PipelinePreset` enums (Mobile / Console / PC-Ultra / Cinematic) produce *bags of plugs* — users see one knob; experts see 40. Plug taxonomy:

| Category (T12.Q) | Plug Interface | Default | Opt-in stack |
|---|---|---|---|
| Q2 Lighting BRDF | `IBxDFPlug` | Cook-Torrance + Disney (diffuse/spec) | + sheen, clearcoat, anisotropic, SSS, cloth |
| Q3 GI | `IGIProvider` | SH probes / APV-style | DDGI → SDF-SW-RT (Lumen) → HW-RT |
| Q4 Shadows | `IShadowProvider` | CSM + PCSS | VSM-vari, RT shadow, contact-hardening |
| Q5 Post-FX | DSL `Pipeline{...}` | Curated preset (TAA + Bloom + Tonemap) | User-editable graph |
| Q6 HDR/Color | `IColorManager` | sRGB | scRGB-FP16, HDR10, Dolby Vision metadata |
| Q7 AA/Upscaler | `IUpscalerPlugin` | TAA | DLSS / FSR / XeSS / DLAA via vendor SDK DLL |
| Q8 Ray Tracing | `IRTEffect` per-effect flags | Off | Reflection / Shadow / AO / GI granular |
| Q9 Terrain & Vegetation | `ITerrainProvider` + `IFoliageScatter` | Off | Heightmap + biome graph + GPU scatter |
| Q10 Atmosphere | `IAtmosphereProvider` | Hillaire 2020 LUT | Bruneton precomputed; custom |

**Vendor SDK isolation**: DLSS / FSR / XeSS live as out-of-tree plugins (`cd::render::vendor::*`); core engine ships TAA + DLAA-style native AA only. Avoids EULA contamination of Apache 2.0 core.

### C. C++26 Reflection Strategy

`cd::render::MaterialDesc` (defined in ADR-003) is annotated with `[[cd::param]]`. Today: macro `CD_REFLECT(...)`. Future: `consteval` reflection lifts the macro to zero-boilerplate descriptor + binding code. ADR-001 RHI bindless descriptor heap consumes the reflected layout directly.

### D. PipelinePreset Facade

```cpp
namespace cd::render {
    enum class PipelinePreset { Mobile, Console, PC_Ultra, Cinematic, Custom };

    class Renderer {
        Renderer(IRhi& rhi, PipelinePreset preset = PipelinePreset::PC_Ultra);
        FrameGraphBuilder& frame_graph();  // expert access
    };
}
```

Preset = curated `IPlug` set. Builder = expert composition.

### E. Material Library Integration (ADR-003 bridge)

Renderer consumes `cd::material::MaterialInstance*` (from ADR-003) and calls `bind(cmdBuffer)`. Pass-specific permutation (depth-only, shadow, gbuffer, gbuffer-translucent) is a **Slang generic argument** (link-time specialization), not a separate file/permutation.

## Reddedilen Alternatifler

| Alternatif | Sebep |
|---|---|
| **Pure deferred (g-buffer thick)** | VRAM yüksek, poor MSAA, hostile to translucents, cluster-decal/volumetric agility kaybı |
| **Pure visibility-buffer (Nanite-only)** | Mesh-shader tier-2 assume; OpenGL fallback dışlanır; 2D path için overkill |
| **Pure forward (no cluster)** | >32 light scale yok; modern probe/decal pipeline dışı |
| **UE-Lumen-only stack** | License/complexity; SW-RT alone insufficient for fully-dynamic glossy reflections |
| **Single hard-coded post-FX chain (id Tech 7)** | Expert-tweak hedefiyle çelişir |
| **Vendor SDK in core (DLSS/FSR/XeSS in-tree)** | EULA contamination of Apache 2.0 |

## Sonuçlar

**Pozitif**:
- Dualism contract preserved (default-easy + expert-tweak).
- Portable 3-RHI; vendor SDKs isolated as plugins.
- C++26 reflection-ready API.
- Nanite-vari unlocked when ready.
- SOTA parity on lighting/GI/shadow/AA when plugs filled.

**Negatif / Risk**:
- Plug matrix multiplies test surface (CI: preset × HW tier × RHI).
- UCVE adds register pressure → mesh-shader path mitigation.
- SDF-SW-RT for GI = Lumen-class complexity → DDGI first, SDF-SW-RT later sprint.
- C++26 reflection delayed → temporary macro is ugly but tractable.

**Replace-Ready (D1)**:
- DLSS/FSR/XeSS plugin DLLs — core hiç vendor-locked değil.
- `IGIProvider` stack tamamen plug — yeni GI tekniği kolay ekleme.

## Açık Sorular

| ID | Soru | Çözüm |
|---|---|---|
| Q1 | Mesh-shader floor: emulation kabul mü? | RHI Capability flag fallback (ADR-001 cross) |
| Q2 | Material permutation explosion stratejisi | ADR-003 Slang link-time specialization |
| Q3 | DLSS/FSR/XeSS license boundary | Out-of-tree plugin (ADR-016 K3 opt-in) |
| Q4 | Lumen-class SW-RT 2026 small-team feasible? | Phase-3 deferral mostly likely |
| Q5 | APV vs DDGI default GI | APV leads; revisit after authoring tools |
| Q6 | Color manager: OpenColorIO mi slim? | ADR-013 onursal; slim subset likely |

## Cross-Cutting

- **ADR-001 (RHI)**: UCVE requires compute + indirect draw + mesh shaders + 64-bit atomics (for vis-buf depth+id pack). Capability flags expose to PipelinePreset for graceful downgrade.
- **ADR-003 (Shader)**: Uber-material with lobe flags via specialization constants; Slang reflection drives bindless descriptor.
- **ADR-004 (ECS)**: Visible-entity stream → `cd::render::Queue` SoA; ECS exposes stable `EntityRenderProxy` with material-id + AABB + transform-double-buffer (TAA motion vectors).
- **ADR-006 (Asset)**: PCG-style terrain/vegetation placement ECS-side; renderer consumes batched instance buffers.

## Kanıt

- Karis — Nanite SIGGRAPH 2021 PDF: https://advances.realtimerendering.com/s2021/Karis_Nanite_SIGGRAPH_Advances_2021_final.pdf
- Wright et al. — Lumen SIGGRAPH 2022 PDF: https://advances.realtimerendering.com/s2022/SIGGRAPH2022-Advances-Lumen-Wright%20et%20al.pdf
- Burns & Hunt — Visibility Buffer JCGT: https://jcgt.org/published/0002/02/04/paper.pdf — **STUB**
- Hillaire 2020 — Production Sky/Atmosphere: https://sebh.github.io/publications/egsr2020.pdf — **STUB**
- Majercik et al. — DDGI: https://morgan3d.github.io/articles/2019-04-01-ddgi/ — **STUB**
- Burley — Disney BRDF 2012/2015 PBR Course notes — **STUB**
- Geffroy — Doom Eternal id Tech 7 SIGGRAPH 2020: https://advances.realtimerendering.com/s2020/RenderingDoomEternal.pdf
- Filament Materials Guide: https://google.github.io/filament/Materials.md.html
- DirectX-Specs — Work Graphs: https://microsoft.github.io/DirectX-Specs/d3d/WorkGraphs.html
- UE5.7 Lumen Tech Docs: https://dev.epicgames.com/documentation/en-us/unreal-engine/lumen-technical-details-in-unreal-engine
