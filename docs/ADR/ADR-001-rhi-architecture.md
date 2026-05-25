# ADR-001 — Render Hardware Interface (RHI) Architecture

- **Status**: Accepted (Phase 1 Design)
- **Date**: 2026-05-17
- **Deciders**: Cemal TATLI
- **Related**: ADR-002 (Renderer), ADR-003 (Shader), ADR-005 (Foundation), ADR-016 (Vendor Matrix)

## Bağlam (Context)

CHROMODYNAMIC, library-oriented, hibrid 2D+3D, cross-platform/cross-API bir engine olarak Vulkan, D3D12 ve OpenGL backend'lerini paralel ve bir referans noktası olarak Vulkan'ı baz alarak desteklemelidir. Üst direktifler:

- T11.Q1 = C+D — iki katmanlı (low-level core + high-level frame graph)
- T11.Q7 = hepsi — compile-time + runtime polymorphism + concept-based dispatch
- T11.Q9 = C+D — üç backend paralel iskelet; Vulkan MVP, sonra hızlı parça parça D3D12+OpenGL
- E4 = b — MVP: mesh + texture + camera + tek directional light
- Kullanıcı vurgusu: production RHI ve Filament backend'i **aşan** tasarım

Performans kısıtı: VR-grade düşük gecikme (motion-to-photon < 20 ms), 60/144/240 FPS scalable; modern donanım (Vulkan 1.3 / D3D12 / OpenGL 4.6) hedef. Library-oriented prensibi `cd::rhi` modülünün tek başına bağımsız kullanılabilir olmasını gerektirir.

Mevcut SOTA: production engines RHI + RDG, Google Filament backend, bgfx, sokol_gfx, The Forge, Diligent Engine, WebGPU/Dawn, NVRHI, EA SEED Halcyon, Granite, AMD RPS SDK. Hiçbiri (a) iki-katman + bindless-first + concept-based dispatch + frame-graph subgraph algebra'sının tamamını birleştirmez.

## Karar (Decision)

**İki-katmanlı, bindless-first, concept-based RHI mimarisi** benimsenir:

### Katman 1 — `cd::rhi::core` (imperative encoder)

- **Strongly-typed handle resource model**: `Handle<Texture>`, `Handle<Buffer>`, `Handle<Pipeline>`, vb. — 32-bit packed `{type, generation, index}` (`cd::core::Handle` — ADR-017 P0 pattern).
- **Reference counting**: `cd::Rc<T>` intrusive ref-counted resource ownership (zero-allocation hot path).
- **Direct command buffer recording**: NVRHI-style, encoder API per worker thread (`cd::rhi::core::CommandList`).
- **Automatic state tracking** (default): resource transition barriers auto-deduced; opt-out via `BarrierMode::Manual` for power users.
- **3-queue model**: Graphics + Compute + Transfer explicit; auto-fallback to single queue when device caps fail.
- **Bindless-first descriptor model**: GPU-virtual descriptor heap; `FeatureLevel::Classic` emulation path for OpenGL 4.6 + pre-DescriptorIndexing Vulkan via per-frame heap rotation.

### Katman 2 — `cd::rhi::graph` (declarative frame graph)

- **Frame graph compiler** (Frostbite 2017 inspired): nodes declare reads/writes, scheduler computes barriers + transient memory aliasing.
- **Subgraph algebra**: `Subgraph` is a pure-value type with input/output bindings; user libraries (post-FX, GI) ship composable subgraphs that link at frame-build time.
- **Lowering boundary explicit**: graph emits `core` calls via single lowering pass; no cross-talk (production RDG conflates lifetime + allocator with FRHICommandList — we forbid this).

### Backend dispatch policy (T11.Q7 = hepsi)

```cpp
namespace cd::rhi {
    template <BackendTrait B>
    class Device;                          // compile-time monomorphic, zero v-table

    class IBackend { virtual ~IBackend(); ... };
    using DynamicDevice = std::unique_ptr<IBackend>;  // runtime polymorphic, plugin hot-swap
}
```

`CD_RHI_STATIC_BACKEND=ON` → compile-time selection, ~zero overhead. `CD_RHI_DYNAMIC_BACKEND=ON` → narrow `IBackend` v-table (~25 entry points) for plugin loading. Both modes coexist; `cd::concepts::Backend` constrains the compile-time path, `IBackend` is the runtime escape hatch.

### Backend roadmap (T11.Q9 = C+D)

1. **Vulkan 1.3 backend MVP** (Sprint 1-3): mesh + texture + camera + directional light. Three queues, bindless, sync-2.
2. **D3D12 backend skeleton** (Sprint 4): port MVP feature parity, ~3-4 weeks behind Vulkan.
3. **OpenGL 4.6 backend skeleton** (Sprint 5): port MVP, `FeatureLevel::Classic` validation.
4. Each subsequent feature (compute, indirect, RT, mesh shaders) lands in Vulkan first, ports to D3D12+OpenGL within same sprint window.

### Error policy

`std::expected<Resource, RhiError>` on every fallible call. `[[nodiscard]]` aggressive. Hot path zero-exception. `Result<T>` alias = `std::expected<T, cd::core::ErrorCode>` (ADR-017 P0 pattern from `dfh::common::error::Result`).

## Reddedilen Alternatifler

| Alternatif | Sebep |
|---|---|
| **Single-layer (Diligent/NVRHI pure)** | Transient memory, async compute scheduling, barrier batching kullanıcıya kalır; Frostbite-class wins kaybolur |
| **Frame-graph-only (Halcyon-style)** | Library consumers'a low-level path verilmez; bindless'a day-one commit zorunlu |
| **Type-erased single handle (uint64+tag)** | Compile-time safety kaybı; CLAUDE.md §1 modern-idiom kuralıyla çelişir |
| **COM-like interface hierarchy (Diligent)** | Virtual hot path, ABI freeze burden, C++23 concept leverage kullanılamaz |
| **DSL-driven graph (AMD RPSL)** | Extra toolchain (rps-hlslc), library-oriented hedefiyle çelişir, v1'de erteleme |
| **Two queues / auto async compute** | Modern donanım üçü ve fazlasını expose ediyor; explicit kontrol değerli |
| **bgfx 16-bit handles** | Cache-friendly ama generational debugging zayıf; 32-bit packed yeterli ve daha güvenli |
| **Pure compile-time backend** | Editor hot-reload/plugin senaryosunda backend swap mümkün değil |
| **Pure runtime polymorphism** | Hot-path vtable maliyeti A++ kalite çıtasının altında |

## Sonuçlar (Consequences)

**Pozitif**:
- Library boundary temiz; `cd::rhi::core` tek başına consumable.
- Frame graph opsiyonel; kullanıcı `core` ile minimal entegrasyon yapabilir.
- C++23 concept + `std::expected` + `[[nodiscard]]` modern idiom uyumlu.
- Bindless ready for GPU-driven rendering, ray tracing, work graphs.
- Backend roadmap parçalı geçiş güvenli (Vulkan first, others follow).
- C++26 reflection opt-in: descriptor layout auto-derive (S3 shader sprint ile birleşir).

**Negatif / Risk**:
- İki-katman API discipline gerektirir; cross-talk denetimi `architect` + CI conformance test.
- Bindless emulation OpenGL 4.6'da yavaş olabilir → `FeatureLevel::Classic` opt-in.
- Üç paralel backend Vulkan-bias riski → conformance test suite Sprint 2'de.
- C++26 reflection yolu opt-in; baseline'da macro/manual fallback.

**Replace-Ready (D1 disiplini)**:
- `cd::rhi::vendor::*` namespace altında Khronos resmi loader (Vulkan), DXC (HLSL), SPIRV-Cross (legacy GL). Bu üçü kaçınılmaz vendor; ileride değil.
- `cd::rhi::native::*` — tüm backend implementation kendi kodumuz. Hiçbir vendor RHI library (bgfx, Diligent, NVRHI) bağımlı değiliz.

## Açık Sorular

| ID | Soru | Karar | Çözüm noktası |
|---|---|---|---|
| Q1 | Intrusive `cd::Rc<T>` vs non-intrusive `std::shared_ptr` | Intrusive | Sprint 2 prototyp |
| Q2 | Frame graph user-supplied scheduler heuristics | Tek heuristic v1 | Sprint 3 |
| Q3 | D3D11/Metal fallback scope | Out-of-scope v1; Metal MoltenVK üzerinden | E4 onayı + Apple Silicon timeline |
| Q4 | WebGPU 4. backend timing | Post-MVP, Sprint 12+ | T4.Q6 = C (out-of-scope) sürdür |

## Cross-Cutting

- **ADR-002 (Renderer)**: Renderer `cd::rhi::graph` üzerinden tüketir (transient memory + async compute kazanımı için). Subgraph composition algebra Renderer Sprint 2 başlamadan önce stabil olmalı.
- **ADR-003 (Shader)**: PSO + descriptor binding API contract; Slang reflection → bindless descriptor layout autogen.
- **ADR-005 (Foundation)**: Handle storage `cd::core::HandleStore` (ADR-017 P0); allocator hierarchy GPU staging.
- **ADR-015 (Concurrency)**: Parallel command recording per-worker; one `CommandPool`/`CommandAllocator` per worker per frame.

## Kanıt

- production engines RHI: https://dev.epicgames.com/documentation/en-us/unreal-engine/render-dependency-graph-in-unreal-engine (acc 2026-05-16)
- Filament DriverApi + FrameGraph: https://github.com/google/filament (acc 2026-05-16)
- NVRHI: https://github.com/NVIDIA-RTX/NVRHI (acc 2026-05-16)
- Granite render_graph: https://themaister.net/blog/2017/08/15/render-graphs-and-vulkan-a-deep-dive/ (acc 2026-05-16)
- AMD RPS SDK: https://gpuopen.com/rps/ (acc 2026-05-16)
- Halcyon (EA SEED): https://www.wihlidal.com/blog/graphics/2018-11-30-halcyon-architecture/ (acc 2026-05-16)
- O'Donnell — Frostbite FrameGraph GDC 2017: https://www.gdcvault.com/play/1024612/ (acc 2026-05-16)

Akademik literatür (Demir Kural — PDF indirilmeden atıf yok):

- Kosarevsky & Medvedev (2024) "Designing Mobile Rendering Engines with Bindless Vulkan", ACM SIGGRAPH Talks, DOI: 10.1145/3641233.3664326 — **STUB**
- Sandu (2023) "History-Aware Frame Graph Resource Allocation", WSCG 2023 — **STUB**
- Otterness & Anderson (2021) "Exploring AMD GPU Scheduling Details", RTNS/ACM, DOI: 10.1145/3453417.3453432 — **STUB**

`academic-researcher` ajanı bu STUB'ları `research/library/MANIFEST.csv` onaylamadan LaTeX/whitepaper'da atıf yapılamaz.
