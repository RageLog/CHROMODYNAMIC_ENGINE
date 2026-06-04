# CHROMODYNAMIC Engine — Finale Plan (Close-Out All Backlog)

**Yazılma tarihi:** 2026-06-04, M17 sonu (phase 725)
**Strateji:** Yeni özellik ekleme DURDURULDU. Tüm yarım kalan / Sprint-1 placeholder / vcpkg gated / strategic gap item'larını sırayla finale erdireceğiz.

**Toplam scope:** 59 backlog item → 8 finale marathon → ~64 agent shipment.

---

## Finale Faz Sırası (Önem)

| Faz | Marathon | Odak | Item ID'leri | Effort | Agent | Token tahmini |
|---|---|---|---|---|---|---|
| **FINALE-1** | M18 | **Quick wins + default flip'ler** | A1, A2, A5, A6, A7, D1, D3, D4-D6 | S-M | 10 | ~600K |
| **FINALE-2** | M19 | **Editor Sprint-2 placeholders → real data** | B1-B10 | M | 12 | ~900K |
| **FINALE-3** | M20 | **External deps (vcpkg promote)** | C1-C7 | M | 8 | ~500K |
| **FINALE-4** | M21 | **Network + async** | E1-E5 | M | 6 | ~500K |
| **FINALE-5** | M22 | **Critical-path heavy (rendering+physics)** | A3, A8, A9, A10, A11 | L | 10 | ~1.2M |
| **FINALE-6** | M23 | **Strategic gaps (multi-window, mesh shader, RT)** | F1, F5, F6, F7, A4 | XL | 8 | ~1.5M |
| **FINALE-7** | M24 | **Nanite + mobile/web port scaffolding** | A12, F2, F3, F4 | XL | 6 | ~1M |
| **FINALE-8** | M25 | **Source culture polish + close-out** | H1-H5, D3 final, push prep | S | 6 | ~400K |

**Toplam tahmin:** ~6M token / ~64 agent shipment / 8 marathon.

---

## FINALE-1 Detay (M18, dispatching now)

**10 agent, 4 wave, OOM-aware (default parallelism):**

### W1 Anında missed retry (2 sonnet)
- **A1** IK Sprint-2 joint limits (M17 W4B missed) — phase 726
- **A2** auto_save_indicator panel (M17 W2C missed) — phase 727

### W2 Default flip + visual verify (3 opus)
- **A5** DDGI composite CD_COMPOSITE_USE_DDGI default-ON + visual verify — phase 728
- **A6** ReSTIR composite CD_COMPOSITE_USE_RESTIR default-ON + visual verify — phase 729
- **A7** Material UI Route A CD_USE_MATERIAL_UI_ROUTE_A default-ON + visual verify — phase 730

### W3 Hygiene batch (2 sonnet + 1 haiku)
- **D1** clang-strict missing-include sweep (across all flagged files) — phase 731
- **D3** modernize-* family sweep (use-auto, use-ranges, use-emplace, use-std-numbers) — phase 732
- **D4-D6** bugprone sweep (incorrect-roundings + implicit-widening + arg-comment) — phase 733

### W4 Verify + auto_save_indicator wire (1 sonnet + 1 haiku)
- Wire auto_save_indicator (from A2) into apps/editor status bar — phase 734
- Final ctest gate + cumulative summary

**Beklenen FINALE-1 sonuç:** 9 commit, 237→245+ test, A1-A7+D1+D3+D4-D6 closed, default-ON visual verify ile apps/editor "moment of awe" potansiyeli.

---

## FINALE-2 Plan (M19)

**Editor Sprint-2 placeholders → real data, ~12 agent**

- **B6** debug_viz real G-buffer sampling (opus)
- **B7** perf_profiler real cd::profile instrumentation wire (sonnet)
- **B8** frame_graph_timeline real cd::framegraph instrumentation (sonnet)
- **B9** gpu_marker real `ICommandBuffer::write_timestamp` (opus, possibly BLOCKED if RHI gap)
- **B1** material_preview real PBR (opus, depends on A7 from FINALE-1)
- **B2** behavior_designer full BT graph (sonnet)
- **B3** cutscene_player pan/zoom (sonnet)
- **B4** pathfinding_viz 3D viewport overlay (sonnet)
- **B5** ik_chain_editor 3D viewport target manipulation (sonnet)
- **B10** build_panel real compilation pipeline (sonnet)

---

## FINALE-3 Plan (M20) — External Deps

**~8 agent, vcpkg promote'lar**

- **C1** HarfBuzz Tier-1 promote (retry from M1 BLOCKED) — FetchContent ordering fix
- **C2** Jolt 5.x vcpkg promote — `find_package(Jolt CONFIG)` wiring
- **C3** Dawn (WebGPU) promote
- **C4** LZ4/zstd for save_compression
- **C5** bc7enc / astc-encoder for texture_compress
- **C6** openal-soft / steam-audio for HRTF spatial audio
- **C7** msdfgen for ui_font multi-channel SDF

Her promote için: vcpkg.json güncelle, `find_package` wire, build verify, kütüphane gerçek backend'e geçer.

---

## FINALE-4 Plan (M21) — Network + Async

**~6 agent**

- **E1** lobby socket transport (TCP/UDP real impl, replace MockTransport)
- **E2** scene_streamer async (worker thread pool)
- **E3** audio_streamer async (worker thread pool)
- **E4** matchmaker real region/skill scoring heuristics
- **E5** input_recorder editor binding (live keyboard/mouse hook)

---

## FINALE-5 Plan (M22) — Critical-Path Heavy

**~10 agent**

- **A3** soft_body Sprint-2 self-collision (particle-particle)
- **A8** RT TLAS / hit-shader live GLSL closest-hit wire (PBR sphere fiilen Sponza yansıtsın)
- **A9** NavMesh triangulation pipeline (mesh → navmesh)
- **A10** Vehicle Jolt real link (depends on C2 vcpkg promote)
- **A11** AI Squad GPU CD_AI_SQUAD_ENABLE_GPU default-ON + visual verify

---

## FINALE-6 Plan (M23) — Strategic Gaps

**~8 agent, XL items**

- **F1** Native multi-window cd::platform Sprint-1 (real OS window primitives)
- **F5** Mesh shader RHI (VK KHR_mesh_shader + DX12 native, paralel)
- **F6** D3D12 RT pipeline + dispatch_rays
- **F7** D3D12 upload heap defrag (ring buffer / free list)
- **A4** soft_body GPU Sprint-3 compute dispatch (no mesh shader needed)

---

## FINALE-7 Plan (M24) — Nanite + Mobile/Web Scaffolding

**~6 agent, XL but Sprint-1 only**

- **A12** Nanite virtual geometry GPU Sprint-1 (cluster dispatch + visibility buffer, depends on F5 mesh shader)
- **F2** iOS Sprint-1 (Metal RHI complete from M10, so iOS scaffolding ready)
- **F3** Android Sprint-1 (NDK preset complete, add SDL2/VK_KHR_android_surface)
- **F4** Web Sprint-1 (Emscripten preset complete, add Dawn integration)

**NOT:** F2/F3/F4 mobile/web ports gerçek hardware CI gerektirir; finale'de scaffolding shipped, real device test user'a kalır.

---

## FINALE-8 Plan (M25) — Source Culture Polish + Close-Out

**~6 agent, S items**

- **H1** Toast animation extras (slide direction config + multi-toast stacking)
- **H2** Splash audio cue
- **H3** Keyboard shortcut chord (Ctrl+K Ctrl+S)
- **H4** Status bar live FPS sparkline
- **H5** Welcome dialog "Custom" layout option
- **D3** Final modernize sweep close-out
- **G1-G5** Push readiness + tag candidate + PR template
- Cumulative final report

---

## Out-of-Scope (Bilinçli Geri Çıkarılan)

Bu finale serisi şunları **YAPMAYACAK** (user-owned):
- **G1 Push to origin** — kullanıcı isteğinde
- **G2 Windows pagefile artırma** — sistem-level
- **G3 hello_engine visual verify** — kullanıcı manuel test
- **G4 Tag v0.99.x** — explicit user request
- **G5 PR template** — kullanıcı GitHub flow'una göre
- **F2/F3/F4 mobile/web gerçek device CI** — hardware sahipliği gerek

---

## OOM Mitigation Devam Eder

Her finale marathon:
- default parallelism parallelism (build OOM mitigation)
- Surgical Edit > Write
- Opt-in flags new compile units için (dormant by default)
- Max 5 paralel agent / wave (heuristic hard rule)

---

## Başarı Kriterleri

FINALE-1 sonu: A1-A7 closed, D1+D3+D4-D6 sweep done, default-ON flag'ler visually verified.
FINALE-8 sonu: 59 item'in **min 50'si** closed. 9'unu user-owned/external-CI gerektiren olarak deferred.

**Hedef commit count:** phase 725 → phase ~800.
**Hedef test count:** 237 → ~290+ (gerçek instrumentation çok test ekler).
**Hedef library count:** 184 → ~190 (yeni library yok, mevcut olanlar finale).
**Hedef editor panel:** 26 → 28-30 (yeni panel sadece eksik wire'da).
