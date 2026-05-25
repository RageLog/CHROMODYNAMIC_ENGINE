# ADR-006 — Asset Pipeline

- **Status**: Accepted (Phase 1 Design)
- **Date**: 2026-05-17
- **Related**: ADR-001 (RHI), ADR-002 (Renderer), ADR-003 (Shader), ADR-004 (ECS), ADR-016 (Vendor Matrix)

## Bağlam

Library-oriented engine için `cd_asset_compiler` standalone tool + `libcd_asset` library zorunlu. SOTA: asset DDC + Iostore + Nanite, asset addressables, Bevy AssetServer, Godot 4 ResourceFormatLoader, O3DE AssetProcessor, Filament matc, id Tech 7 megatextures. Vendor minimum (T5.Q2); E2 matrix: glTF kendi parser, FBX (ufbx) opsiyonel, USD opsiyonel, KTX2 vendor (Khronos referans), Box2D vendor (E2 reverse).

## Karar

### A. Import Formats (T15.Q1 = D)

| Format | Strateji | Library |
|---|---|---|
| glTF 2.0 | **First-class kendi parser** (json + base64 + GLB) | core |
| FBX | Opt-in plugin `ufbx` (MIT, single-header) | `CD_ASSET_FBX=ON` |
| USD | Opt-in plugin Pixar SDK | `CD_ASSET_USD=ON` |
| OBJ/PLY/STL | Trivial kendi parser | core |
| DAE | Deprecated, düşük öncelik | optional |

glTF KHR extensions native: `KHR_mesh_quantization`, `KHR_draco_mesh_compression`, `KHR_texture_basisu`.

### B. Texture (T15.Q2)

**KTX2 + Basis UASTC supertranscode** (libktx + basisu vendor — K1, Khronos referans):
- Color: UASTC (yüksek quality)
- Lightmaps/UI: ETC1S
- Normal maps: BC5 / EAC_RG11
- Mobile: ASTC supertranscode path

Aşma noktası: Virtual Texture + KTX2 tile — VT page'leri runtime'da Basis transcode et (Filament/production engines yok).

### C. Mesh

**Custom binary `.cdmesh`**: 64 B header + meshlet cluster table (128 tri × 64 vert, AMD/NV uyumlu) + LOD chain DAG (Nanite-vari) + interleaved vertex (pos/norm/tan/uv0/uv1) + opsiyonel BVH. mmap + GPU upload zero-copy.

glTF/FBX/USD sadece source; cooked her zaman `.cdmesh`. **Aşma noktası**: meshlet cluster + cone culling + LOD DAG (Nanite cluster hierarchy) Sprint 6 temel, ileride genişler. C++26 reflection ile MeshletHeader schema autoreg.

### D. Asset Baking (T15.Q4 — DDC pattern)

**`cd_asset_compiler`** standalone CLI + library:
- Content-addressed (CAS), Blake3 hash input → cooked output.
- Shared cache: `%LOCALAPPDATA%/cd_ddc` (Win) veya `~/.cache/cd_ddc` (Linux).
- Opsiyonel HTTP shared DDC (derived-data sharing pattern).
- Editor mode: runtime import fallback (dev quality of life).
- Ship mode: zorunlu cooked.

**Aşma noktası**: Merkle DAG dep graph → asset dependency cache invalidation otomatik (texture değişirse material rebuild). production engines manuel; Bevy/Godot'ta yok. Bazel Remote Execution API-uyumlu (gelecek CI inanılmaz hızlı).

### E. Streaming (T15.Q5)

Sprint 6 temel:
- Per-mesh LOD streaming (screen-space error metric).
- Virtual texture başlangıç (256×256 tile, 8K page pool).
- Meshlet streaming temeli (cluster bazlı IO request).
- Tile-based world streaming (spatial hash + frustum predict + async io_uring/IORing).

Phase 2-3: Nanite-vari software raster, megatexture-vari unique texel.

**Aşma noktası**: GPU-driven streaming feedback — GPU shader residency feedback buffer'a yazar, CPU async IO scheduler okur (id Tech 7 + Nanite). production engine/Godot/Bevy/Filament yapmıyor — open-source ilk impl hedef.

### F. Registry (T15.Q6 — Hybrid)

- **Authoritative**: SQLite (`asset_registry.db`, UUID → cooked path, hash, deps, type, version).
- **Human-facing manifest**: JSON (`asset_manifest.json`, git-diff friendly, CI'da SQLite'tan generate).
- **FTS5**: full-text search (editor "Find Asset" anında).
- **Recursive CTE**: dep graph query.
- C++26 reflection ile asset struct → SQLite schema otomatik.

production binary `.bin`; production `.meta` YAML — biz iki dünyanın iyi yanını alıyoruz.

### G. Hot Reload (T15.Q7)

Cross-platform watcher (`efsw` MIT vendor veya kendi yazımız ~500 LOC). Değişen source → cd_asset_compiler tetikle (background) → cooked hash diff → engine'e IPC message → **bindless texture/mesh slot atomic swap** (RHI handle değişmez, descriptor table yenilenir).

**Aşma noktası**: bindless slot stability — production engines hot-reload material/shader rebuild expensive. Bizde slot stabil; sadece içerik değişir → frame içinde swap. production engines/Filament bunu yapmıyor (4 katmanlı: mesh + texture + material + shader hot-reload).

### H. Format Matrix Özet

| Asset Type | Source | Cooked | Runtime | Tool | Vendor/Custom |
|---|---|---|---|---|---|
| Texture | PNG/JPG/EXR/HDR/TGA | `.cdtex` (KTX2 + UASTC + BCn/ASTC variants) | KTX2 mmap + transcode | `cd_asset_compiler --tex` | libktx+basisu (K1) |
| Mesh | glTF / FBX / OBJ | `.cdmesh` (binary + meshlet + LOD + BVH) | mmap + GPU upload | `cd_asset_compiler --mesh` | glTF custom; FBX ufbx (opt) |
| Audio | WAV/FLAC/OGG | `.cdaud` (Opus stream + uncompressed SFX) | mmap stream | `cd_asset_compiler --aud` | dr_wav + dr_flac + libopus (K1) |
| Animation | glTF / FBX anim | `.cdanim` (ACL compressed) | mmap | `cd_asset_compiler --anim` | ACL (K2) |
| Scene/Prefab | JSON / glTF scene | `.cdscene` (flatbuffer-vari binary) | mmap | `cd_asset_compiler --scene` | custom |
| Material | `.cdmat` (TOML/JSON) | `.cdmatx` (SPIR-V + reflection blob) | mmap + RHI bind | `cd_shader_compiler` | Slang+DXC (K1) |
| Script | `.lua` / WASM | `.cdscript` (bytecode/wasm validated) | VM load | `cd_asset_compiler --script` | TBD (ADR-future) |

## Reddedilen

- **Runtime glTF/FBX import production** — parse cost, deterministik değil.
- **Assimp** — 250+ format ama LGPL bağımlılık; ufbx + kendi parser daha temiz.
- **Pure JSON registry** — O(n) lookup; dep graph query yetersiz.
- **PNG/JPG runtime decode** — GPU upload yavaş.
- **glTF runtime mesh format** — meshlet yok, LOD chain yok.
- **USD authoring runtime** — Pixar SDK ağır; opsiyonel kalır.

## Sonuçlar

**Pozitif**:
- Standalone tool, engine'siz kullanılabilir, CI-friendly.
- SOTA seviyesi tüm 7 kategoride.
- Vendor minimum, hepsi MIT/Apache/BSD/PD.
- Hot-reload 4-katmanlı (mesh+texture+material+shader).

**Negatif**:
- `.cdmesh`/`.cdtex` format versioning disiplin gerektirir.
- Virtual texture + meshlet implementation karmaşık (multi-sprint).
- DDC remote cache HTTP server opsiyonel — yıl 2.

**Replace-Ready (D1)**: libktx+basisu K1 (replace yok). ufbx (FBX) opt-in vendor — kendi yazma değer üretmez. USD K3, opsiyonel.

## Açık Sorular

| ID | Soru | Çözüm |
|---|---|---|
| Q1 | DDC remote cache: Bazel Remote Execution API mi custom HTTP? | Sprint 8 |
| Q2 | Meshlet generator: meshoptimizer vendor (MIT, ~10k LOC) mı kendi mi? | Vendor önce, kendi Phase 3 |
| Q3 | Virtual texture page size: 128² vs 256²? | Sprint 6 prototyp |
| Q4 | Audio codec: Opus vs Vorbis (libopus BSD)? | **Opus** (modern, ADR-007 da onaylar) |
| Q5 | Script asset: WASM vs Lua veya ikisi? | ADR-future scripting |
| Q6 | UUID: UUIDv7 (time-ordered) vs Blake3 hash (CAS native)? | Blake3 (CAS uyumlu) |
| Q7 | Hot reload editor-only vs shipping debug flag? | Editor + dev; ship'te kapalı |

## Cross-Cutting

- **ADR-001 (RHI)**: KTX2 mip chain upload + bindless descriptor slot allocation; staging buffer ring; Vulkan VK_KHR_descriptor_indexing + D3D12 ResourceHeapTier2 zorunlu; mobile ASTC fast path.
- **ADR-002 (Renderer)**: Mesh streaming LOD (screen-space error), meshlet cluster culling (cone+frustum+occlusion HZB), virtual texture feedback (compute → CPU readback).
- **ADR-003 (Shader)**: `.cdmat` → SPIR-V cook via DXC → SPIRV-Reflect binding metadata → runtime bindless register.
- **ADR-004 (ECS)**: Prefab `.cdscene` binary; component blobs via C++26 reflection; asset reference = UUID + bindless handle.

## Kanıt

- asset DDC: https://dev.epicgames.com/documentation/en-us/unreal-engine/derived-data-cache
- KTX2 spec: https://github.khronos.org/KTX-Specification/
- Basis Universal: github.com/BinomialLLC/basis_universal
- Karis Nanite SIGGRAPH 2021: https://advances.realtimerendering.com/s2021/Karis_Nanite_SIGGRAPH_Advances_2021_final.pdf
- Wihlidal "GPU-Driven Rendering Pipelines" SIGGRAPH 2015 — **STUB**
- van Waveren "id Tech 5 Megatextures" SIGGRAPH 2009 — **STUB**
- Brainerd "id Tech 6/7" SIGGRAPH 2016/2020 — **STUB**
- meshoptimizer: github.com/zeux/meshoptimizer
- SQLite amalgamated: sqlite.org/amalgamation.html
