# ADR-20260616 — ALL-MODULES-TO-100 BAND 7 (FINAL band, <40%) Kapsam Mührü (2 kütüphane)

- **Status**: Accepted
- **Date**: 2026-06-16
- **Branch**: dev (HEAD 9b86238)
- **Deciders**: Cemal TATLI
- **Author**: Developer (BAND 7 — the FINAL band — close-out: seal-the-functional-v1
  HONESTLY + harden-the-real-core-tests + correct-the-false-banner + promote-on-need)
- **Related**:
  - `docs/ROADMAP_ALL_MODULES_TO_100.md` §BAND 7 (the 2 lowest-completion libs +
    the honest "100% = IMPLEMENTED+tested OR formally SEALED by a one-paragraph
    ADR scope-decision" rule, §"What 100% MEANS"; named gaps: restir_gi "trace REAL
    GI (explicit 'placeholder hit' / 'placeholder cached radiance' today) + .cpp/RHI
    — follow the cd::restir_di pattern", ui_renderer_webgpu "implement the Dawn WGSL
    pipeline/shader (no-op default + deferred 'Phase 5.5' today) or seal WebGPU as a
    future-target ADR")
  - `docs/PROJECT_COMPLETION_STATUS.md` §5 (engine/render-features: cd::restir_gi 38,
    "real reservoir combine/clamp/weight CPU + GLSL CS BUT sample CS uses explicit
    'placeholder hit'/'placeholder cached radiance' — GI not actually traced") + §7
    (engine/ui: cd::ui_renderer_webgpu 35, "default CD_UI_WEBGPU_HAVE_DAWN=0 path is
    pure no-op (counts verts only); Dawn path allocates buffers but WGSL
    pipeline/shader deferred to unimplemented 'Phase 5.5'")
  - `docs/ADR/ADR-20260616-band3-ui-aisquad-scope.md` +
    `docs/ADR/ADR-20260616-band3-world-4-libs` siblings (the same band-seal template:
    seal-the-charter-complete-v1 + lock-the-untested-real-core-edges +
    promote-on-need for the platform/GPU/external-dep expansion)
  - `engine/render/restir_di/include/cd/restir_di/Reservoir.hpp` +
    `engine/render/restir_di/include/cd/restir_di/DispatchPass.hpp` (cd::restir_di
    at 83% — the proven ray-query + reservoir-SSBO + 3-pipeline dispatch pattern the
    restir_gi GI trace would reuse when promoted)
  - `docs/ADR/ADR-20260530-ui-widget-library.md` (cd::ui_renderer_webgpu Phase 5
    charter — this ADR §2 seals its no-op default + deferred WGSL pipeline to the
    BAND-7 honest-rule terminal state)
- **Scope guard**: This ADR is ONLY a scope-decision document. Sealed items are
  "deferred-by-design" — they no longer count as open. Each carries a precise
  "promote-on-need" exit gate. Changes in this pass are CONFINED to
  `engine/render/restir_gi/` (header banner + GLSL honesty comments + hardened
  tests + README) + `engine/ui/renderer_webgpu/` (hardened tests + README banner)
  + this `docs/ADR/` file. NO out-of-scope library touched (not restir_di, not the
  other ui/render libs, not rhi/, not samples/, not hello_*, not the % docs). The
  restir_gi header edits are COMMENT-ONLY (banner + GLSL string comments inside a
  raw string literal that no production target compiles) and the webgpu edits are
  TEST/README-only → public API surface stays fixed, ABI stays fixed, hello_engine
  render path is untouched → golden byte-identical.

---

## 1. Karar — cd::restir_gi (render/restir_gi) — 38 → 100  [reservoir-math-v1 SEALED; GI-trace promote-on-need; IMPLEMENTED(test)]

- **Bağlam**: 373-LOC INTERFACE header (`GiReservoir.hpp`). The CPU reference is
  REAL and correct: the WRS streaming `update`, the cross-pixel `combine` (RIS
  estimator with a caller-supplied target-PDF re-eval functor), the proportional
  `clamp_history`, the temporal `age` + `temporal_blend`, and `final_weight`
  (the unbiased RIS denominator). Three matching GLSL compute-kernel strings
  (sample / temporal-reuse / spatial-reuse) embed the same algebra. This reservoir
  math is byte-for-byte the same shape as `cd::restir_di` (83%, B2-done). **The
  gap**: the sample compute shader (`kRestirGiSampleCS`) fills each candidate with
  an explicit "placeholder hit" + "placeholder cached radiance" instead of issuing
  a ray query against `u_TLAS` — so GI is NOT actually traced. The top header
  banner OVERSTATED this ("traces one path (primary + one bounce)"). Tests were
  weak (5 cases / 8 asserts), and the README listed two nonexistent headers
  (`BounceSample.hpp` / `ReuseKernel.hpp`).
- **Karar**: **reservoir-resampling-math-v1 SEALED HONESTLY; real GI trace =
  promote-on-need.**
  - **reservoir-math-v1 SEALED**: the WRS update + combine + clamp + temporal
    age/blend + final_weight is the genuine, reusable, load-bearing core — the
    same proven algebra as cd::restir_di. That IS done. It is now DEEPLY tested
    (see below).
  - **GI-trace SEALED (promote-on-need)**: wiring a real GI gather means a
    substantial RHI-dispatch subsystem — the cd::restir_di::DispatchPass pattern:
    4 reservoir SSBOs (current/previous/temporal/spatial), 3 compute pipelines
    with descriptor-set + pipeline layouts, per-frame push constants, a scene-TLAS
    binding for the ray query, motion-vector + G-buffer-normal texture bindings,
    and a render-graph consumer to ping-pong + barrier the buffers and consume the
    final reservoir into the lighting integrator. cd::restir_di reached only 83%
    WITH that whole subsystem built and still defers the textured G-buffer
    bindings; replicating it for GI (plus the second-bounce radiance cache) is a
    multi-week feature build with no current consumer. Producing it now is
    dead-code/over-engineering. The header's reservoir-math is the right v1; the
    trace is the right promote-on-need.
  - **Banner corrected**: the top-of-file banner now states "reservoir-math-v1 =
    done + tested; GI-trace = deferred-by-design" and the sample shader's
    placeholder block is relabelled a clearly-marked DEFERRED-TRACE (behaviour
    byte-identical — comment-only). The README was rewritten to the honest scope
    and to reference the real header (`GiReservoir.hpp`), dropping the two
    phantom headers.
  - **IMPLEMENTED(test)**: tests deepened 5 → 23 cases, locking the SEALED math
    as fail-on-revert (NOT the placeholder trace): WRS weight-sum + M accumulation,
    zero-weight-streams-but-never-selects, the swap-rule boundary (rand just under
    vs. just over the ratio), final_weight = weight_sum/(M·p_hat) + its two
    non-positive guards, clamp proportionality + below-cap no-op + final_weight
    invariance under clamp, combine empty-donor early-return + M-growth +
    donor-selection-flips-visibility + zero-p_hat-never-swaps, temporal_blend
    alpha=0/alpha=1/midpoint-lerp + current-age carry, invalidate-to-zero, and the
    three-kernel-presence + DEFERRED-TRACE-marker honest-scope guards. All
    deterministic (no sleep_for, no RNG).
- **Gerekçe**: restir_gi's real value is the reservoir-resampling math, and that
  is correct + now deeply tested. The named gap ("trace REAL GI ... follow the
  cd::restir_di pattern") is honestly a "build the whole GI dispatch subsystem"
  ask — exactly the multi-week RHI feature that the roadmap's honest-rule (b)
  promote-on-need clause exists for. The math IS done (a); the trace IS sealed (b)
  with a precise trigger. The misleading banner/README that claimed GI was traced
  is now corrected so no future reader mis-baselines off it.
- **Promote-on-need**: a real GI gather — a `cd::restir_gi::DispatchPass`
  (mirroring `cd::restir_di::DispatchPass`: 4 reservoir SSBOs + 3 compute
  pipelines + descriptor/pipeline layouts + a scene-TLAS ray-query binding +
  motion-vector/G-buffer-normal textures) plus a radiance cache and a render-graph
  consumer that feeds the final reservoir into the lighting integrator — arrives
  with a real indirect-lighting consumer + a separate ADR; the `Sample` /
  `Reservoir` / `update` / `combine` / `clamp_history` / `temporal_blend` API and
  the GLSL struct layout stay fixed so the promoted trace drops in without a
  re-layout.

---

## 2. Karar — cd::ui_renderer_webgpu (ui/renderer_webgpu) — 35 → 100  [WebGPU-backend-as-future-target SEALED; no-op-default + buffer-accounting IMPLEMENTED(test)]

- **Bağlam**: 306 src + 184 hdr (`Submitter.hpp` / `Submitter.cpp`). Two
  compile-time backends gated by `CD_UI_WEBGPU_HAVE_DAWN`. The **default**
  (`== 0`, no Dawn) is a pure **no-op**: `WgpuDevice` / `WgpuCommandEncoder` are
  opaque integer structs; `create`/`upload`/`record`/`destroy` only do CPU
  buffer-accounting (vertex / index / command counts) so the build is green on
  every host without Dawn. The **Dawn path** (`== 1`, opt-in via vcpkg
  `chromodynamic[webgpu]`) allocates the ring vb/ib via `CreateBuffer` and uploads
  via `Queue::WriteBuffer` — BUT the WGSL vertex/fragment pipeline + shader are
  explicitly deferred to an unimplemented "Phase 5.5 `hello_ui_webgpu`". The
  README's status table OVERSTATED the Dawn path as "REAL" without flagging that
  the pipeline is missing. Tests were thin (4 build-only cases + 1 Dawn-gated).
- **Karar**: **WebGPU-backend-as-future-target SEALED HONESTLY.**
  - **future-target SEALED**: a real WebGPU UI backend needs the heavy Dawn
    dependency (Abseil + Python host tooling, ~800 MB Release / ~1.4 GB
    Debug+Release, 25–45 min cold build, a self-hosted CI runner with a cached
    sysroot) AND an authored WGSL UI pipeline/shader driven by a real swapchain
    surface. That is a future-platform target — directly analogous to the
    Metal-on-Mac HW-gated situation — not a gap to "fix" by padding code. The
    no-op default is the correct terminal state: it keeps the build green on
    every non-Dawn host (the project's actual CI tiers) while preserving the public
    API so the real backend drops in via a CMake flip + the deferred WGSL author.
  - **Banner corrected**: the README now states plainly that this is a
    FUTURE-TARGET, not a working backend — the default does NO GPU work (CPU
    counting only) and even with Dawn present the missing WGSL pipeline means it is
    not a working UI renderer (it allocates + uploads buffers and records draws
    against an unbound pipeline).
  - **IMPLEMENTED(test)**: tests deepened 4 → 16 cases, locking the real
    load-bearing contract the default MUST honour — the CPU buffer-accounting +
    Dawn-absent behaviour, NOT the deferred trace/pipeline: create rejects
    zero-limit configs, upload rejects vertex-overflow + index-overflow (and
    leaves last-good counts untouched), exact-capacity upload succeeds
    (inclusive boundary), empty-frame upload zeroes the per-frame snapshot,
    successive uploads re-snapshot (growth + shrink, no accumulation),
    scissor boundaries produce multiple counted DrawCommands, record-before-upload
    is a safe no-op, a default-constructed (never-created) submitter is inert +
    crash-safe, move transfers ownership, and (Dawn-absent only) the stub handles
    default to the null/zero opaque value with the defaulted `operator==`. All on
    the stub path; no GPU required; deterministic.
- **Gerekçe**: ui_renderer_webgpu's honest charter is the renderer-agnostic
  DrawBatcher → WebGPU bridge with a green no-op default. The named gap
  ("implement the Dawn WGSL pipeline/shader ... or seal WebGPU as a future-target
  ADR") explicitly offers the seal, and the seal is the right call: the WGSL
  pipeline depends on a vendored Dawn + a real surface + a self-hosted CI runner —
  none present — so building it now is wrong-layer / dead-code (same shape as the
  Metal Mac-gated seal). The roadmap's honest-rule (b) promote-on-need clause is
  exactly for this. The buffer-accounting + Dawn-absent contract IS the real,
  testable surface today, and it is now deeply locked (a). The misleading "REAL
  backend" README phrasing is corrected so no reader mistakes the future-target
  for a shipping backend.
- **Promote-on-need**: a working WebGPU UI backend — vendor Dawn into CI (cached
  sysroot, self-hosted runner) AND author the WGSL vertex/fragment pipeline +
  shader + render-pass descriptor + bind group for the atlas + projection
  push-constant, driven by a real `hello_ui_webgpu` swapchain surface — arrives
  with a real WebGPU deployment need + a separate ADR; the `Submitter` API
  (`create`/`upload`/`record`/`destroy` + the per-frame count accessors) and the
  `SubmitterCreateInfo` shape stay fixed so the real pipeline drops in behind the
  same surface.

---

## 3. Reddedilen alternatifler

- **restir_gi'ye gerçek GI trace (cd::restir_gi::DispatchPass) implement etmek**:
  4 reservoir SSBO + 3 compute pipeline + descriptor/pipeline layout + scene-TLAS
  ray-query binding + motion-vector/G-buffer-normal textures + radiance cache + bir
  render-graph consumer ister — cd::restir_di'nin 83%'e ULAŞMAK için kurduğu tüm
  alt-sistem (ve o bile textured G-buffer binding'leri defer ediyor). GI için
  replicate etmek (artı second-bounce radiance cache) multi-week, tüketicisi YOK
  → dead-code/over-engineering. RED — reservoir-math-v1 SEALED, GI-trace
  promote-on-need; gerçek matematik (WRS/combine/clamp/temporal) fail-on-revert
  kilitlendi.
- **ui_renderer_webgpu'ya WGSL pipeline/shader implement etmek (ya da Dawn'ı
  default-on yapmak)**: vendored Dawn (~800 MB, 25–45 dk cold build, Abseil+Python,
  self-hosted CI sysroot) + gerçek swapchain surface ister — hiçbiri mevcut değil;
  Metal-Mac-gated mührüyle aynı şekil. RED — future-target SEALED, no-op-default +
  buffer-accounting + Dawn-absent contract fail-on-revert kilitlendi.
- **Herhangi bir public header/src DAVRANIŞINI değiştirmek**: bu pass yalnız (a)
  restir_gi header'ında comment-only banner + GLSL-string-içi comment + (b) iki
  test dosyasına ekleme + (c) iki README + bu ADR yaptı → API/ABI sabit, golden
  byte-identical garantili. RED.
- **% docs'u / samples'ı / hello_*'ı / kapsam-dışı lib'leri (restir_di, diğer
  ui/render lib'leri, rhi/) düzenlemek**: kapsam DIŞI (brief SCOPE EXCLUSION). RED.

## 4. Sonuçlar

- (+) 2/2 BAND-7 (FINAL band) kütüphanesi honest-rule terminal durumuna geçti:
  cd::restir_gi (reservoir-math-v1 SEALED + GI-trace promote-on-need + 5→23 test)
  + cd::ui_renderer_webgpu (future-target SEALED + 4→16 test). Hiçbir kütüphanede
  placeholder/TODO-state YANLIŞ-banner olarak kalmadı (placeholder GLSL bloğu
  dürüstçe DEFERRED-TRACE olarak etiketlendi, no-op default dürüstçe future-target
  olarak belgelendi).
- (+) restir_gi: WRS weight-sum/M accumulation + swap-rule boundary + final_weight
  formülü + clamp proportionality/invariance + combine (empty/M-growth/visibility-
  flip/zero-p_hat) + temporal_blend (alpha 0/1/mid + age carry) + invalidate artık
  fail-on-revert kilitli (+18 test). Yanıltıcı banner ("traces one path") + README
  (iki phantom header) düzeltildi.
- (+) ui_renderer_webgpu: zero-limit-reject + vertex/index-overflow-reject + exact-
  capacity-inclusive + empty-frame-zero + successive-resnapshot + scissor-multi-
  command + record-before-upload-noop + default-constructed-inert + move-ownership +
  stub-handles-null artık fail-on-revert kilitli (+12 test). Yanıltıcı "REAL
  backend" banner'ı future-target olarak düzeltildi.
- (+) Her mühür precise promote-on-need tetikleyici taşır: restir_gi için gerçek
  GI gather (cd::restir_gi::DispatchPass + radiance cache + render-graph consumer,
  cd::restir_di pattern); webgpu için vendored-Dawn-CI + authored-WGSL-pipeline +
  gerçek surface. Genişleme yolu net ama bugün dead-code/wrong-layer olmaz.
- (+) Toplam +30 yeni test (18 restir_gi + 12 webgpu), hepsi edge/contract +
  anti-flakiness (sleep_for YOK; deterministik girdi). Build -Werror temiz; 0 yeni
  clang-tidy WAE defect-class. restir_gi header edits comment-only + webgpu edits
  test/README-only → public API/ABI sabit, hello_engine render path etkilenmez →
  golden byte-identical.
- (−) Mühürler gerçek GI trace dispatch subsystem'ini ya da WebGPU WGSL pipeline'ı
  bu pass'te ÜRETMEZ; gerçek bir indirect-lighting consumer / WebGPU deployment +
  vendored-Dawn-CI ihtiyacı doğunca ayrı ADR'larla gelir. Kabul: BAND 7 doğası
  gereği genuine greenfield (multi-week subsystem / external-dep) — honest terminal
  durum, charter-complete-v1'i SEAL et + gerçek çekirdeği derinleştir + yanlış
  banner'ı düzelt.
- (+) BAND 7 (FINAL band) kapandı → roadmap'in tüm 122 engine lib'i terminal-100%
  durumunda (gerçek impl ya da promote-on-need-ADR-sealed).

---

## Varsayımlar

- Brief'in "100% RULE per lib" yorumu: bu iki lib genuine large greenfield gap'li
  EN düşük-tamamlanma kütüphaneleridir; dürüst terminal durum = functional-v1'i
  SEAL et + gerçek çekirdek testlerini sertleştir + yanlış banner'ı düzelt; gerçek
  impl = precise promote-on-need (her gap multi-week subsystem / external-dep).
  Brief açıkça izin verdi: "If wiring a real ray-query GI trace is small+clean
  reusing restir_di's proven path, implement it; else SEAL (it is a substantial
  RHI-dispatch + ray-query subsystem)" → restir_di::DispatchPass'in boyutu (4 SSBO
  + 3 pipeline + descriptor/pipeline layout + TLAS, ve o bile textured binding'leri
  defer ediyor) küçük+temiz DEĞİL → SEAL doğru karar.
- restir_gi test target'ı `cd_test_restir_gi` (engine/render/restir_gi/tests/
  test_restir_gi.cpp, INTERFACE lib); webgpu test target'ı `cd_test_webgpu_submitter`
  (engine/ui/renderer_webgpu/tests/test_webgpu_submitter.cpp, stub path
  CD_UI_WEBGPU_HAVE_DAWN==0). İkisi de PASS (ctest -R "restir_gi|renderer_webgpu|
  ui_renderer_webgpu" ile doğrulandı; cd_test_rhi_vulkan KOŞULMADI).
- restir_gi header düzenlemeleri yalnız (a) dosya-başı banner yorumu + (b) GLSL
  raw-string literal'i İÇİNDEKİ yorum satırları (placeholder → DEFERRED-TRACE
  etiketi) → derlenen hiçbir production target davranışı değişmez (lib INTERFACE +
  GLSL string'i hello_engine bu pass'te derlemez). webgpu düzenlemeleri yalnız test
  + README. → public API/ABI sabit.
- "scope exclusion" kuralı uygulandı: tüm değişiklikler engine/render/restir_gi/ +
  engine/ui/renderer_webgpu/ + bu docs/ADR/ dosyası altında. restir_di + diğer
  ui/render lib'leri + rhi/ + samples/ + hello_* + % docs'a DOKUNULMADI.
- Golden byte-identical: bu pass public header/src DAVRANIŞINI değiştirmedi
  (comment + test + README only) → hello_engine render yolu hiç etkilenmez; fixture
  #5 capture baseline (research/reports/parity1121/baseline.png) ile bayt-bayt eşit
  doğrulandı (b7.png cmp → identical, sonra silindi).

## Sonraki

- BAND 7 (FINAL band) kapandı → all-modules-to-100 roadmap'i tamamlandı (BAND 0–7
  hepsi terminal). Geriye kalan iş, mühürlerin promote-on-need tetikleyicileridir,
  ayrı ADR'larla:
  - restir_gi: gerçek GI gather (cd::restir_gi::DispatchPass + radiance cache +
    render-graph consumer) — indirect-lighting consumer ihtiyacıyla.
  - ui_renderer_webgpu: vendored-Dawn-CI (cached sysroot, self-hosted runner) +
    authored WGSL UI pipeline/shader + render-pass descriptor + atlas bind group +
    projection push-constant + gerçek hello_ui_webgpu surface — WebGPU deployment
    ihtiyacıyla.
