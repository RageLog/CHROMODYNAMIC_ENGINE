# CHROMODYNAMIC — All-Modules-to-100% Ordered Plan (band-descending)

> Goal: every module → 100%. **Execution rule:** form completion BANDS (100 / 90–99 / 80–89 /
> 70–79 / 60–69 / 50–59 / 40–49 / <40), start from the HIGHEST band, bring every module in it to
> 100%, then descend one band — repeat until the whole engine is 100%. Baseline =
> `docs/PROJECT_COMPLETION_STATUS.md` (122 libs, overall ~76%).
>
> **What "100%" MEANS (the honest rule, same as the backend road-to-100):** a module is 100% when
> EVERY documented gap in its basis is in one terminal state — (a) IMPLEMENTED + tested, OR
> (b) formally SEALED by a one-paragraph ADR scope-decision (so a "deferred-by-design" item stops
> counting as open). No fourth "TODO/placeholder" state may remain. Every module exits at: real
> impl or sealed scope + a dedicated gtest (fail-on-revert) + (render/GPU libs) a golden or a
> device test + build clean + golden byte-identical.

## Execution order (top-down)

`BAND 0 (done) → BAND 1 (90–99) → BAND 2 (80–89) → BAND 3 (70–79) → BAND 4 (60–69) → BAND 5 (50–59) → BAND 6 (40–49) → BAND 7 (<40)`

Within a band, order by descending % (closest-to-done first); honor cross-module dependencies
noted inline (e.g. de-dup froxel clustering, asset-streamer decode).

## Group-level band (main-heading view)

| Band | Groups (rollup %) |
|---|---|
| 80–89 | game 86, render-core 84, world 81 |
| 70–79 | foundation 79, ui 72 |
| 60–69 | asset+tooling 64, render-features 61 |

(The execution plan below is at the LIBRARY level — that is where work happens; the group bands
are context. cd::rhi at 100 is the only fully-done module.)

---

## BAND 0 — 100% (DONE; maintenance + bug-fix only)
- **cd::rhi** — 3-backend parity complete (phases 1215–1225). Residual is HW-gated Metal-on-Mac
  GPU verification + operator CI, NOT impl. **Action: refresh the stale per-file header banners**
  (they still say "boot-only/stubbed") — near-zero effort, prevents mis-baselining. Then frozen.

## BAND 1 — 90–99 → 100  (7 libs; smallest gaps, fastest wins)
| Lib | % | → 100 |
|---|---|---|
| cd::frame_timing | 92 | edge-case tests (overflow/empty-ring); seal scope. |
| cd::game::quest | 92 | branch/failure-path test depth; seal. |
| cd::game::save | 92 | corrupt/partial-file fuzz + version-migration test; seal. |
| cd::bench (tooling) | 90 | seal as complete tool (ADR: tooling-100). |
| cd::game::ai_bt | 90 | reactive-decorator + running-state edge tests; seal. |
| cd::game::cutscene_player | 90 | event-window boundary fuzz; seal. |
| cd::gluon | 90 | module-include-closure cache-key test + the remaining BRDF/IBL module coverage; seal. |
**Band exit:** 7/7 at 100 (all production; mostly test-deepening + scope-seal ADRs). **Effort: ~S.**

## BAND 2 — 80–89 → 100  (66 libs; the bulk — mostly production, "deepen + seal")
Common to-100 for the 82–88 production libs (concurrency, math, ecs, scene, material, shader,
debug_line, ui_layout, core, io, log, time, diag, events, profile, vfs, net, physics, audio,
restir_di, ddgi, post, dialogue, l10n, settings, ai_*, anim*, camera, query, …): edge/negative
test depth to the project's evidence bar + close the ONE documented gap each, then a scope-seal.
Specific named gaps inside this band:
| Lib | % | → 100 (specific) |
|---|---|---|
| cd::events | 82 | implement async/priority/weak-token delivery (currently deferred) or seal. |
| cd::config | 80 | broaden beyond persist-only (typed CVar schema) or seal narrow scope. |
| cd::plugin | 80 | flesh out the thin HotReload (35 LOC) path + test. |
| cd::framegraph | 80 | **HIGH-VALUE**: real transient-resource aliasing + auto-barrier + dead-pass cull (today linear execute). |
| cd::debug_draw | 80 | raise test coverage (only 5 tests) to bar. |
| cd::spirv_cross_glue | 85 | raise coverage (only 4 tests) — HLSL/MSL emit matrix. |
| cd::audio / audio_spatial | 80 | non-Windows backend verification (CoreAudio/ALSA) or seal Win-first + HRTF dataset test. |
| cd::script | 80 | sandbox/error-path + binding-coverage tests; seal. |
| cd::ui_renderer_rhi | 80 | glyph-atlas draw path (lands w/ material UI-variant) + multi-backend. |
| cd::ui_widgets | 80 | close the documented cross-platform NativeWindowAdapter gaps. |
| cd::imgdiff (tooling) | 80 | seal as complete tool. |
| cd::sample_framework | 80 | seal intentionally-thin harness scope. |
(the other ~52 are test-deepen + one-gap-close + seal). **Effort: L (volume), but low-risk; the
biggest single item is cd::framegraph's aliasing graph — schedule it first in this band.**

## BAND 3 — 70–79 → 100  (20 libs)
| Lib | % | → 100 |
|---|---|---|
| cd::mem | 78 | fix POSIX munmap length-leak (documented v1 TODO) + test. |
| cd::render (umbrella) | 78 | deepen Renderer aggregation vs header surface; tests. |
| cd::async_submit | 78 | frame-fence integration (deliberately minimal today) or seal. |
| cd::profile_cpu_marker_overlay / frame_graph_timeline | 78 | golden-image test of the overlay (visual side untested). |
| cd::net_session_replay | 78 | broaden the SRPK surface + tests. |
| cd::physics_jolt | 78 | make the real Jolt path the default-tested config (currently build-flag-gated). |
| cd::ui_font | 78 | **default build has NO shaping** → wire FreeType/HarfBuzz on by default or ship a real fallback shaper. |
| cd::audio_dsp_fx | 75 | implement FDN reverb tuning + SIMD (deferred). |
| cd::physics_vehicle | 75 | lateral/Pacejka tire model (only longitudinal today). |
| cd::ai::squad | 75 | finish the GpuBatchSolver (thin dispatch, no buffer ownership). |
| cd::ui_a11y | 75 | screen-reader / UIA / AT-SPI bridge (baseline only today). |
| cd::lighting_clusters | 75 | **DE-DUP** with cd::cluster + cd::light::ClusterGrid (3× froxel binning) — pick one owner. |
| cd::ibl | 72 | unify with cd::ibl_gpu; verify bakers on-GPU. |
| cd::ui (umbrella) | 70 | real glyph layout (text is a bare DrawKind today) + widen widget surface. |
| cd::mesh_shader | 70 | cone/spatial clusterizer optimization (greedy today); GPU dispatch already in rhi. |
| cd::cluster | 70 | **DE-DUP** (see lighting_clusters) — consolidate or delete the duplicate. |
| cd::texture_compress | 70 | real BC1 endpoint fit (naive today) + BC3/BC5. |
| cd::material_authoring | 70 | schema versioning + validation depth. |
| cd::texture_synth (tooling) | 70 | seal as complete fixture tool. |
**Effort: M–L. Key cross-cut: the 3× froxel-clustering de-dup (cluster/lighting_clusters/light).**

## BAND 4 — 60–69 → 100  (12 libs)
| Lib | % | → 100 |
|---|---|---|
| cd::light | 68 | move ClusterGrid into the single froxel owner; add .cpp + tests. |
| cd::editor | 68 | **BIGGEST SURFACE (~15.8k LOC)**: real in-panel text/glyph + ImGui-ize the 27 panels (placeholder visuals today); single editor.exe entry. |
| cd::volumetric | 66 | real .cpp + RHI dispatch (header-only GLSL today). |
| cd::particle_system | 65 | GPU dispatch + force fields (CPU-only by scope) or seal scope. |
| cd::asset::shader_cache | 65 | compiler integration (cache-only today) or seal. |
| cd::asset::vfx_authoring | 65 | runtime simulation tie-in. |
| cd::asset::streamer_pool | 65 | depends on the 3 streamers' real decode (Band 6) — re-verify after. |
| cd::platform | 65 | web/android/iOS window backends (kNotImplemented today) or seal desktop-first. |
| cd::atmosphere | 62 | bake all 4 LUTs (only 1 today) + RHI dispatch. |
| cd::denoise | 60 | real OIDN backend (pass-through stub today) + .cpp/RHI. |
| cd::asset::validator | 60 | deep structural/schema parse (header-sniff only) or seal shallow scope. |
| cd::imgui_backend (tooling) | 60 | **add tests (zero today)** + verify the thin D3D12 path; seal. |
**Effort: L. The editor is the dominant item — treat as its own multi-wave sub-plan.**

## BAND 5 — 50–59 → 100  (6 libs)
| Lib | % | → 100 |
|---|---|---|
| cd::decal | 58 | atlas + blend pass + .cpp/RHI dispatch (projection-only header today). |
| cd::virtual_textures | 56 | streaming/upload/indirection impl (types-only today). |
| cd::runtime | 55 | grow from DI-context-holder to a real runtime loop or seal as context. |
| cd::profile_gpu_marker | 55 | wire real GPU timing via the now-implemented RHI query subsystem (`IDevice::create_query_pool` shipped phase1218) — the dependency it was waiting for EXISTS now. |
| cd::velocity | 55 | .cpp + RHI dispatch (math+GLSL only today). |
| cd::virtual_geometry | 52 | real QEM LOD simplification (bbox-diagonal placeholder today) + non-no-op mesh-shader path. |
**Effort: M–L. Note profile_gpu_marker is now UNBLOCKED by the Band-already-done A-QUERY work.**

## BAND 6 — 40–49 → 100  (8 libs)
| Lib | % | → 100 |
|---|---|---|
| cd::ibl_gpu | 48 | **add tests (zero today)** + verify the RHI upload helpers on-GPU; unify with cd::ibl. |
| cd::gpu_particles | 45 | emit kernel + indirect-draw wiring (simulate-only today). |
| cd::asset::texture_streamer | 45 | **real cdtex decode** (fake 1×1 handles today) — orchestration already done. |
| cd::asset::scene_streamer | 45 | **real glTF load** (synthetic SceneIds today). |
| cd::asset::audio_streamer | 45 | **real WAV/OGG decode** (simulated today). |
| cd::light_shafts | 42 | analytic epipolar (Kim & Marsalek) — radial-blur-only today. |
| cd::nrc | 40 | real trainable MLP (CUDA/SPIR-V backend) — toy CPU MLP today; or seal as research-skeleton. |
| cd::foundation_utils | 40 | it is a pure umbrella aggregator → seal as 100%-by-definition (no own logic to implement). |
**Effort: L. HIGH-LEVERAGE: the 3 asset streamers share the same fix shape (wire the real decoder
behind the done orchestration) — do them together.**

## BAND 7 — <40 → 100  (2 libs; hardest / most-deferred)
| Lib | % | → 100 |
|---|---|---|
| cd::restir_gi | 38 | trace REAL GI (explicit "placeholder hit" / "placeholder cached radiance" today) + .cpp/RHI — follow the cd::restir_di pattern (which is 83%). |
| cd::ui_renderer_webgpu | 35 | implement the Dawn WGSL pipeline/shader (no-op default + deferred "Phase 5.5" today) or seal WebGPU as a future-target ADR. |
**Effort: L–XL. These are genuine greenfield; restir_gi can lean on restir_di's proven pattern.**

---

## Cross-band notes
- **De-dup froxel clustering** (cluster ↔ lighting_clusters ↔ light::ClusterGrid) spans Band 3–4 —
  resolve ownership ONCE when the first of them is worked (Band 3), the others then collapse.
- **Asset streamers** (Band 6) unblock streamer_pool's end-to-end (Band 4) — re-verify pool after.
- **profile_gpu_marker** (Band 5) is already unblocked by the shipped RHI query subsystem.
- **Tooling libs** (bench/imgdiff/texture_synth/imgui_backend) reach 100 by completing+sealing the
  tool, not by engine-runtime depth — call them out so they aren't over-scoped.
- **"Seal scope" ADRs** live in `docs/ADR/ADR-YYYYMMDD-<lib>-scope.md`, one paragraph each, exactly
  like the backend wontfix ADRs — they make "100%" honest + defensible.

## Per-band exit criterion (uniform)
Build clean (`cmake --build --preset ninja-debug`, -Werror) + golden byte-identical + every
touched lib's gtest green (fail-on-revert) + 0 new clang-tidy WAE + no `kNotImplemented`/placeholder
left in that lib (verified against `docs/RHI_KNOTIMPL_INVENTORY.md`-style per-band inventory).
Commit each green checkpoint. No tag/push.

## Rough effort shape
B1 ~S · B2 L-by-volume (low-risk, framegraph-aliasing is the one real item) · B3 M–L (froxel
de-dup + ui_font shaping) · B4 L (editor dominates) · B5 M–L · B6 L (3 streamers + gpu features) ·
B7 L–XL (restir_gi + webgpu). Net: the engine reaches a defensible 100% top-down; the early bands
bank many quick 100%s, the late bands are the real feature builds.
