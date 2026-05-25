# ADR-003 — Shader & Material Pipeline

- **Status**: Accepted (Phase 1 Design)
- **Date**: 2026-05-17
- **Related**: ADR-001 (RHI), ADR-002 (Renderer), ADR-006 (Asset), ADR-016 (Vendor Matrix)

## Bağlam

2024–2025 landscape decisively changed:
1. **Khronos formally adopted Slang from NVIDIA (Nov 2024)** as official cross-target shading language.
2. **Microsoft announced Shader Model 7.0 with SPIR-V as DXIL's interchange format** — collapses two-IR world to one.

This eliminates the dilemma that drove HLSL+DXC dual-target strategies for the last five years.

User decisions:
- T13.Q1 = B (Slang) — first phase; state-of-art research overall
- T13.Q3 = D — graph editor + uber-shader codegen + instance system
- T13.Q5 = D — disk PSO cache + async warm-up + per-platform binary asset
- T13.Q2/Q4/Q6 = araştır

## Karar

### A. Source Language — **Slang** (Apache 2.0, Khronos resmi, NVIDIA-maintained)

Single canonical source language. Vendor: `cd::shader::vendor::slang`. Replace policy: K1 (Khronos resmi — asla replace).

- Modules, generics, interfaces, autodiff, parameter blocks.
- Native multi-target codegen: SPIR-V / DXIL (via DXC) / MSL / WGSL / GLSL / HLSL / CUDA / C++.
- Valve production proof (Counter-Strike 2, Dota 2).

### B. Cross-API Strategy — Single Source, Native Multi-Target

`cd_shader_compiler` invokes Slang's native multi-target backend. No SPIRV-Cross fan-out on the hot path. SPIRV-Cross retained as fallback only (`CD_SHADER_FALLBACK_SPIRVCROSS=1`) for shaders hitting Slang backend bugs on legacy GL.

**Per-target output**:
- Vulkan → SPIR-V (Slang native).
- D3D12 → DXIL (Slang → DXC pass).
- OpenGL → GLSL (Slang native textual emit) or SPIR-V loadable (`GL_ARB_gl_spirv`).
- Metal/iOS (future) → MSL (Slang native).
- WebGPU (future) → WGSL (Slang native).

### C. Material System (T13.Q3 = D)

**Pipeline**: `Material Graph (.cdmat JSON) → Slang Module Codegen → Slang Compilation → SPIR-V/DXIL + Reflection → PSO + Material Instance`.

**Graph → Codegen**: Nodes map 1:1 to Slang functions inside a generated module:
```slang
module cd.material.<hash>;
struct GenSurface : IMaterialSurface { ... }  // from graph
```
**Structured Slang emission** (not text concatenation) — defeats class of bugs UE Material Editor still ships (silently truncated HLSL).

**Permutation strategy** (uber-shader replacement):
1. **Authoring**: graph = uber-style; no permutation thinking by artist.
2. **Slang generic specialization** at material-instance bind time — `MaterialPipeline<TSurface : IMaterialSurface>` is link-time specialized when instance is created. Produces exactly *one* compiled variant per realized combination (lazy, demand-driven, cached forever).
3. **Dynamic dispatch fallback** via Slang interfaces for hot-swapping rarely-used variants without recompile.

Collapses permutation matrix from "all 2^N pre-cooked" (UE) to "only realized N visited, cached forever" (Slang).

**Material instance + parameter block**:
- `Slang ParameterBlock<MaterialParams>` canonical binding.
- C++26 reflection on matching C++ struct emits setters automatically (zero-boilerplate target UE never reached).
- Macro fallback `CD_REFLECT(...)` until C++26.

### D. Hot-Reload (T13.Q4)

- File watcher: `ReadDirectoryChangesW` (Win), `FSEvents` (macOS), `inotify` (Linux) — `cd::shader::FileWatcher` with 50 ms debounce (DtForHil pattern miras edilebilir — `cd::events::EventBus` cross).
- **Recompile pipeline**: dirty Slang module → re-link affected materials → reflection diff → layout-compatible: swap SPIR-V + rebuild PSO no restart; incompatible: force material instance re-init (logged).
- **Editor/Dev/Ship**: compiled in editor + dev builds (`CD_SHADER_HOT_RELOAD=1`); compiled out of ship builds (no `slangc` dependency shipped).
- **Crash safety**: new PSO compiled on background thread, atomically swapped only on success; old PSO retained until first successful frame.

### E. PSO Cache (T13.Q5 = D — 3-tier)

1. **Driver-level**: `VkPipelineCache` + `ID3D12PipelineLibrary` — serialized to disk per-GPU-per-driver-version under `%LOCALAPPDATA%/CHROMODYNAMIC/pso_cache/<gpu_uuid>/<driver_ver>/`. Re-cache on driver change (production engines 6+ pattern).
2. **Application-level**: hash-keyed blob store (`xxh3_128(pipeline_desc + shader_spv_hash + render_state)`). Survives driver changes; rehydrates driver cache on cold start.
3. **Async warm-up at session start**: background thread submits known-good PSOs from a **PSO trace manifest** (captured during dev playtests, à la production 6 PSO Tracing + UE PSO Precaching). Manifest lives in asset bundle.

**Per-platform binary asset**: `.cdshd` container, magic `CDSH`, sections `{slang_ir | spirv_vk | dxil_d3d | glsl_gl | msl_apple | reflection_json | pso_hints}`. Loader picks right section based on active RHI.

### F. Shader Reflection (T13.Q6)

- **Primary**: Slang reflection API (compile-time + runtime) — preserves *semantic* information (generic args, interface conformances, parameter block grouping) that SPIR-V loses.
- **Fallback**: SPIRV-Reflect for hand-written `.spv` and third-party shaders.
- **Validation layer** (debug builds): cross-check `MaterialParameterStorage` against Slang reflection at instance creation. Emit structured diagnostic, not GPU crash.
- **Auto-binding**: C++26 reflection scans C++ struct → Slang reflection provides per-target offsets → bridge generates `MaterialParams::bind(cmdBuffer)` at compile time.

### G. Standalone Tool — `cd_shader_compiler`

Standalone CLI + library, **engine'siz kullanılabilir** (library-oriented). Asset pipeline integration via ADR-006.

## Reddedilen Alternatifler

| Alternatif | Sebep |
|---|---|
| **HLSL+DXC dual target** | Redundant once Slang covers same outputs with modules/generics |
| **GLSL+glslang+SPIRV-Cross** | 2-pass cross-compile lossy on edge cases; no D3D12 native path |
| **Custom DSL** | 18+ dev-months to reach Slang frontend parity; negative ROI |
| **Pre-cook 2^N permutations like UE** | Eliminated by Slang link-time specialization |
| **Driver cache only** | Insufficient (driver-version invalidation); pair with app-level |
| **WGSL as primary** | Feature-lagging vs SM6.7/Vulkan 1.4; weak for AAA |

## Sonuçlar

**Pozitif**:
- Single source language, fewer shader bugs.
- Near-zero permutation explosion (link-time specialization).
- Hot-reload first-class.
- Library-oriented tool reusable in asset pipelines.
- SM7/SPIR-V transition handled by Slang.

**Negatif / Risk**:
- Build dependency on Slang (mitigated: Apache 2.0, vendored under `Dependencies/`).
- Slang debugger story still maturing (use RenderDoc + Slang debug info).
- Team learning curve from HLSL→Slang (low; Slang is HLSL-superset-ish).
- Slang backend bugs on niche features (mitigated: SPIRV-Cross fallback flag).

**Replace-Ready (D1)**: Slang is K1 (Khronos resmi) — no replace. Other vendor deps (DXC) also K1. No replacement work needed.

## Açık Sorular

| ID | Soru | Çözüm |
|---|---|---|
| O1 | Slang version pinning: submodule vs FetchContent vs system? | Vendored submodule under `Dependencies/slang/` |
| O2 | `cd_shader_compiler` daemon mode? | Sprint 4 profiling karar |
| O3 | Graph editor: in-process ImGui vs external Qt/Avalonia? | ADR-012 (editor) sonra |
| O4 | PSO trace format: UE `.ushaderpipelinecache` mi custom? | Custom `.cdpso` trace; Sprint 5 |
| O5 | C++26 reflection fallback: macro vs Boost.PFR vs codegen? | Macro `CD_REFLECT` v1, C++26 lift later |
| O6 | Material graph file format versioning policy? | Schema-versioned JSON via SchemaRegistry (ADR-017 P2) |
| O7 | Neural shader / slang-rhi integration timeline? | Phase 4+ defer |

## Cross-Cutting

- **ADR-001 (RHI)**: PSO API + descriptor binding model; bindless descriptor heap consumes Slang reflection.
- **ADR-002 (Renderer)**: Material library = renderer's binding contract; pass-specific permutation via Slang generic, not file.
- **ADR-006 (Asset)**: `cd_shader_compiler` = asset-pipeline executable; `.cdshd` = cooked output; DDC tracks `(graph_hash, slang_version, target_triple) → blob_id`.

## Kanıt

- Khronos Group Launches Slang Initiative: https://www.khronos.org/news/press/khronos-group-launches-slang-initiative-hosting-open-source-compiler-contributed-by-nvidia
- Slang Compilation Targets: http://shader-slang.org/slang/user-guide/targets
- Slang Reflection API: http://shader-slang.org/slang/user-guide/reflection
- Slang Module System & Link-Time Specialization: https://deepwiki.com/shader-slang/slang/4.4-module-system-and-link-time-specialization
- DirectX Adopting SPIR-V: https://devblogs.microsoft.com/directx/directx-adopting-spir-v/
- HLSL as First-Class Vulkan Shading Language: https://www.khronos.org/blog/hlsl-first-class-vulkan-shading-language
- UE Substrate Materials 5.7 Production Ready: https://dev.epicgames.com/documentation/en-us/unreal-engine/overview-of-substrate-materials-in-unreal-engine
- production 6 PSO Tracing: https://discussions.unity.com/t/prevent-shader-compilation-stutters-with-pso-tracing-in-unity-6/951031
- Foley & Hanrahan — Spark: modular composable shaders SIGGRAPH 2011 — **STUB**
- He et al. — Slang language mechanisms SIGGRAPH 2018 — **STUB**
