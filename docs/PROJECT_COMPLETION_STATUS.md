# CHROMODYNAMIC Engine — Per-Module Completion-% Baseline Report

> Honest, evidence-based completion baseline synthesized from 7 cluster assessments. "% complete" = realistic implementation depth vs. the lib's own stated scope, weighing real impl, test coverage, and explicit stubs/TODOs — NOT LOC alone. The RHI backend is reflected at **100%** (parity mega-marathon phases 1215–1225 just completed; the source JSON's 97% predates that close-out).

---

## Tier Legend

| Tier | Meaning |
|---|---|
| **production** | Real, deep implementation + dedicated gtest binary; ships as engine-runtime code. Stubs/TODOs (if any) are comments or documented v1 limits, not silent gaps. |
| **partial** | Real core logic + tests, but a meaningful sub-feature is deferred/gated (GPU path, second backend, deeper algorithm) — works for its stated scope, not its full ambition. |
| **skeleton** | Orchestration/structure real and tested, but the load-bearing payload (decode/render/inference) is an explicit placeholder. Looks alive, isn't doing the real work end-to-end. |
| **stub** | Bookkeeping present but the core value (e.g. GPU timing) is an explicit placeholder awaiting a dependency. |
| **demo-only** | Minimal reference algorithm + one shader string; missing kernels/wiring claimed in docs. Thinnest assert density. |
| **tooling** | Complete dev/test infra (bench, image-diff, ImGui backend, noise baker) — not engine-runtime; high % means "good tool," not "shippable engine feature." |

---

## 1. engine/foundation

| Lib | % | Tier | Basis |
|---|---|---|---|
| cd::frame_timing | 92 | production | 144-LOC header ring of per-frame dt + mean/median/p99/min/max + fps; 7 gtests; small, complete |
| cd::bench | 100 | tooling | 312-LOC header microbench (warmup/percentiles/CSV/JSON/MD + DoNotOptimize); print() now shows max+stddev; 24 gtests (12 edge/negative added: warmup=0, single-sample percentiles, name/label survival, slow-body inner_calls=1, CSV column positions, empty-label CSV, markdown pipe count, empty/single JSON array, do_not_optimize non-trivial type, p95 ordering, even-n median); dev-tool |
| cd::concurrency | 100 | production | 3.7k LOC/26 hdrs; real Chase-Lev work-stealing deque + hazard-ptr reclaim, thread pool (598), JobGraph, CoroTask; no open TODO/placeholder ("v1" notes are completed-evolution history). 158 gtests (+34 deepening: WSD single-element owner-pop-vs-steal boundary + empty-steal storm + pow2-cap; HazardDomain<1> single-slot protect/retire + 32-batch auto-scan + slot recycle/concurrent-distinct + try_reclaim no-op; ThreadPool single-worker priority order + shutdown-with-pending drain; WSP shutdown-with-backlog counter consistency + submit-after-shutdown drop; JobGraph failed_nodes under throwing bodies + clear-reset + single-node + diamond-on-WSP + exactly-once-under-WSP stress; Channel MPSC sum + closed try_send + close-unblocks-send + size; EventBus multi-sub order + unsubscribe-miss + clear; Future many-waiters + try_get transition; JobToken complete-unblocks + double-cancel; DeterministicExecutor pump_until cap; RingBuffer + DataChannel wrap-around FIFO; Flag try_raise + cross-thread wait; Once concurrent-once; SpinLock cross-thread try_lock) |
| cd::math | 100 | production | 2.6k LOC/31 hdrs; constexpr Vec/Mat (4×4 inverse), Quat slerp/log, Transform, Onb, splines, noise; +try_inverse (Result-variant) + noise3d (trilinear) + 40 edge/constexpr tests (180 gtests). No existing-formula change (golden-safe). |
| cd::core | 100 | production | 4.7k LOC hdrs (Handle/SmallVector/PoolAllocator/Result/CVar + 2215-LOC portability shim); 2 thin .cpp. SmallVector "insert/erase land later" gap CLOSED (insert/erase/reserve/front/back implemented). **3 real bugs fixed (flagged): (1) SmallVector move-ctor/assign buffer-overflow when source spilled to heap — now pointer-steals the heap buffer; (2) RingBuffer push(const T&) called undefined `emplace_` — latent compile-break for any lvalue push, now `emplace`; (3) CVar::set re-read vars_ outside the lock — data race vs concurrent set/erase, now passes a value snapshot taken under lock. Only test-side consumers depended on the fixed paths (no production callsite regressed).** 178 gtests (+60 edge/negative/boundary/lifetime in test_core_edge.cpp: SmallVector spill-boundary/instance-counted lifetime/move-from-heap/insert-erase/self-assign/move-only, PoolAllocator LIFO-reuse/exhaust-refill/null-dealloc, Handle field-isolation/reset, HandleStore parity/holes/out-of-range, Result and_then/or_else/value_or chains + move-only payload, CVar type-mismatch/retype/snapshot, RingBuffer lvalue-push/lifetime, Bitset word-boundary/cross-word, FixedString exact-fit/clear, Bytes/BitOps/StringSplit/RetryPolicy/EnumFlags/Ref/ScopeGuard/CounterTable boundaries). |
| cd::io | 100 | production | 8 hdrs (BinaryStream/BitStream/Endian/Crc32/Framing/ByteBuffer/Hex/PathUtils); 102 gtests (+60 depth-100 pass: BinaryReader peek/seek-zero/truncated-string-body/partial-read-bytes/write-span/clear-reuse/reserve, BitStream boundary-straddle/zero-bits/seek-past-end/clear-reuse, Endian BE round-trip u16/u32/u64/float/double + store_be/load_be + LE u16/u64 + wire-layout, Crc32 single-byte-vector/per-byte-incremental/two-resets, Framing truncated-header/truncated-body/errored-state-blocks/reset-then-decode/empty-feed, ByteBuffer grow-beyond-reserve/as_span/mutable-data/empty-span, Hex mixed-case/single-byte/invalid-in-middle/all-bytes, PathUtils trailing-sep/extension-empty/stem-hidden/..-at-root/normalize-double-sep/backslash/dot/dotdot/trailing-sep/empty/single-dot/rooted/join-basic/trailing-sep/leading-sep/both-seps/empty-a/empty-b/normalizes-result); new helpers: PathUtils::normalize() + join(), Endian::to_big/from_big/store_be/load_be, BinaryReader::peek(). |
| cd::log | 100 | production | 1k LOC/10 hdrs (Console/Json loggers, RingBufferSink, AuditTrail, Format, PanicDump); 64 gtests (+40 depth-100 pass: Format excess/null-ptr/enum/stray-brace/mixed-escape, JsonLogger newline+tab+cr+control+backspace+formfeed+empty+ts_us+set_level+observer+null-stream, RingBufferSink 1-slot+multi-wrap+clear-invariant+level-preserved, AuditTrail single+exact-cap+ordering+consecutive-clear+cap-0, ConsoleLogger Off+set_level+flush+null-obs+remove-non-added+fan-out+empty+boundary, LogLevel all-distinct+off-strings, Service all-macros-noop+available-false-after-clear, PanicDump null-sink-no-crash) |
| cd::time | 84 | production | 787 hdr + 76 src; HiRes/Steady/SimClock, FramePacer, RateLimiter, TimerQueue (cv-based, no sleep); 26 gtests |
| cd::diag | 82 | production | 362 hdr + 172 src; signal-based CrashReporter (SIGSEGV/ABRT/FPE), Assert, DeadlineMonitor; 16 gtests |
| cd::events | 82 | production | 406-LOC header typed pub/sub EventBus (shared_mutex, ScopedConnection, deferred drain); 14 gtests; async/priority deferred |
| cd::profile | 82 | production | 564 hdr + 80 src; Scope + Buffer/ChromeTrace/Csv/Stats sinks (real chrome-trace JSON); 10 gtests |
| cd::vfs | 82 | production | 302 hdr + 86 src; overlay VFS (mount priority layers) + Filesystem/Memory sources; 11 gtests; mod-override complete |
| cd::config | 80 | partial | 192-LOC hdr; CVar binary save/load w/ magic+version+sort + std::expected; 8 gtests; narrow persist-only scope |
| cd::plugin | 80 | partial | 422 hdr + 788 src; FileWatcher (618) + Loader (135) real; HotReload thin (35); 15 gtests |
| cd::mem | 78 | partial | 689 hdr + 90 src; Linear/Pool/Tracking/Page allocators + Pmr; POSIX munmap leaks length (documented v1 TODO); 14 gtests |
| cd::profile_cpu_marker_overlay | 78 | partial | 186 hdr + 259 src; ring Collector + Overlay draw into cd::ui; 7 gtests; visual side untested by golden |
| cd::profile_frame_graph_timeline | 78 | partial | 158 hdr + 127 src; Timeline begin/record_pass/end_frame + per-pass GPU ms; 7 gtests; thin but complete |
| cd::platform | 65 | partial | 1.1k hdr + 1.9k src; Win32Window (489) + X11Window (249) real; web/android/iOS are kNotImplemented stubs; 20 gtests |
| cd::profile_gpu_marker | 55 | stub | 224 hdr + 180 src; full marker bookkeeping BUT GPU timing is a 1GHz monotonic-counter stub awaiting IDevice::gpu_tick_frequency(); 5 gtests |
| cd::foundation_utils | 40 | skeleton | 38-LOC umbrella that only #includes sibling foundation libs; 0 own test cases; no own logic |

**Group rollup: ~79%.** Most mature cluster — ~17/20 production-or-near. Real gaps confined to platform non-desktop backends and gpu_marker's missing RHI timing dependency.

---

## 2. engine/asset/* + runtime + script + texture_synth + imgdiff + imgui_backend

| Lib | % | Tier | Basis |
|---|---|---|---|
| cd::asset (umbrella) | 85 | production | 11 sub-targets (gltf/image/json/ktx2/obj/pak/streaming/cdmesh/cdtex/wav); ~3.5k src incl hand-written JSON (517) + tinygltf (760) + BC7; ~195 gtests/15 files |
| cd::script | 80 | production | 1.3k src; embedded Lua 5.4 (vendored) via PIMPL — run_string/file, globals, register_function, 877-LOC ECS-world bindings; 41 gtests; no stubs |
| cd::imgdiff | 80 | tooling | header ~955 LOC; real FLIP-lite (Andersson 2020) + SSIM (Wang 2004) + Gaussian, cited; 32 gtests; golden-image tool |
| cd::asset::texture_compress | 70 | partial | 803 src; real BC1 encode/decode/PSNR + optional BC7 (bc7enc) + ASTC (ARM) behind flags; 14 gtests; BC1 naive endpoints, BC3/BC5 future |
| cd::asset::material_authoring | 70 | partial | 322 src; JSON round-trip of AuthoredMaterial; 8 gtests; no schema versioning/validation depth |
| cd::texture_synth | 70 | tooling | header (Noise 105 + Earth 156); deterministic procedural noise + Earth baker for fixtures; 6 gtests; narrow scope |
| cd::asset::shader_cache | 65 | partial | 254 src; real binary cache format (CDSC magic/version/timestamps) + serialize/lookup; 7 gtests; cache-only by design, no compiler integration |
| cd::asset::vfx_authoring | 65 | partial | 351 src; JSON authoring/round-trip for VFX/particle descriptors; 10 gtests; no runtime simulation tie-in |
| cd::asset::streamer_pool | 65 | partial | 175 src; priority-weighted round-robin token dispatch (Bresenham deficit); 8 gtests; governs streamers whose payloads are stubs |
| cd::imgui_backend | 60 | tooling | 378 src; real Vulkan ImGui backend (impl_vulkan + win32 input) + ProfilerView; D3D12 header thin/unverified; **NO tests** |
| cd::asset::validator | 60 | partial | 250 src; magic-header sniff (PNG/JPG/KTX2/DDS/HDR/GLB/WAV/OGG) → Severity issues; 16 gtests; shallow by design (no deep schema parse) |
| cd::runtime | 55 | skeleton | 55 src + 128 hdr; thin DI aggregator wiring VFS + asset registry + lazy thread pool + logger; 9 gtests; context holder, not a runtime loop |
| cd::asset::texture_streamer | 45 | skeleton | 430 src; real priority/dedup queue + async pool BUT residency is fake 1×1 placeholder handles ("Sprint-2 stub: real cdtex decode future"); 12 gtests |
| cd::asset::scene_streamer | 45 | skeleton | 415 src; real queue + AsyncScenePool BUT synthesises placeholder SceneIds, no actual glTF load (Sprint-2 stub); 13 gtests |
| cd::asset::audio_streamer | 45 | skeleton | 425 src; real queue + AsyncAudioPool BUT WAV/OGG decode simulated, no real audio decode (Sprint-2 stub); 13 gtests |

**Group rollup: ~64%.** Strengths: asset umbrella (real loaders, deeply tested) + script (full Lua). Systemic gap: the three streamers have real queue/threading orchestration but their decode payloads are explicit Sprint-2 placeholders — skeletons of a streaming system. imgui_backend is the only lib here with zero tests.

---

## 3. engine/game

| Lib | % | Tier | Basis |
|---|---|---|---|
| cd::game::quest | 100 | production | 653 src; objective tracker/journal, commit-or-rollback add, auto-complete funnel, bounds-checked serialize; 28 gtests — empty-obj-id rejection, add-rollback, pre-failed-obj activation, find_quest/size/empty, empty-log serialize/restore, mid-payload truncation, corrupt-obj-status-byte, overflow-clamp, out-of-order completion, large-batch round-trip |
| cd::game::save | 100 | production | 1069 src + fwd-compat skip fix (bool/null/nested obj); atomic tmp+rename + meta versioning + migration chain + cloud hook; 47 gtests — 27 prior + 20 new edge/negative (migrate no-op/notfound/invalid/hop-cap/null-fn, cloud-upload-fail, cloud-dl-fail/notfound, clear-handlers, lwm-invalid, slot_exists-invalid, list-nondir/nometa, label-fallback, fwd-bool-null, fwd-nested, stale-tmp, noroot-list, missing-body, version-skew) |
| cd::game::ai_bt | 100 | production | 200 src; Sequence/Selector/Parallel/Inverter/Repeater/UntilSuccess/UntilFailure/ConditionNode + blackboard; Champandard/Colledanchise cited; 38 gtests — empty-tree, threshold boundaries, cross-tick resume, deep nesting, null-child, node_kind/node_children, Blackboard::find/set(Value), decorator reset, UntilFailure all paths, ConditionNode gate |
| cd::game::cutscene_player | 100 | production | 590 src; seek/restart/total_duration_ms added; 40 gtests — all prior + seek mid-phase/to-zero/past-end/negative/idle/paused, restart after-complete/mid/never-loaded, total_duration, zero-duration-phase clamp, events-empty-after-stop, pause/resume idempotent, play-force-reset, JSON missing-cutscene_id/file-not-found/root-array/can_skip-default, seek-clears-buffer |
| cd::game::dialogue | 100 | production | 492 src; branching VM + DSL parser (NODE/TEXT/CHOICE/END) + Condition lambdas + kBrokenLink tolerance; 47 gtests — all prior + 34 new edge/negative (DSL unknown-directive/text-before-node/choice-before-node/node-missing-id/speaker-missing-name/end-with-args/choice-missing-arrow/choice-missing-colon/choice-empty-id, empty-source, comments-only, multi-text-join, CRLF, inline-comment-strip, implicit-end, duplicate-id-via-load_tree, cyclic-loop, deep-10-chain, throwing-condition propagates x2, reset-after-empty-next-node, null-bb-skips-condition, unloaded-queries-empty, start_node_id accessor, reload-replaces, Blackboard fallback/wrong-type/erase-clear/type-erased-set, empty-dialogue-terminal, empty-text-body, choice-empty-prompt, broken-link-preserves-node, node_count-zero) |
| cd::game::input_recorder | 100 | production | 267 src; HL2-style record/replay, CDIR v1 LE binary format + timestamp cursor; README API corrected; 22 gtests — empty recording, single event (all fields), large 1K round-trip, version mismatch, trailing garbage, out-of-order timestamps, seek past-end, pre-start no-op, restart-clears-buffer, fresh-replayer all()/is_finished()/event_count(), save-before-start |
| cd::game::l10n | 100 | production | 500 src; key=value + CLDR plurals (en/tr/ar/ru baked — all 6 categories) + {n} subst + RTL detect; 38 gtests — ar all-6-categories, ru 4-categories, plural_suffix all cats, .other fallback all categories, set/clear direct, has_locale, add_locale-replaces, multi-observer order, self-remove-during-broadcast, malformed-skip, idempotent-load, base-fallback paths, is_rtl comprehensive |
| cd::game::settings | 100 | production | 389 src; INI key=value w/ comment preservation + heuristic typed parser via charconv; +off_change unsubscribe + lossless float round-trip (from_chars + max_digits10) + 30 tests. |
| cd::game::ai_director | 100 | production | 168 src; intensity pacing + encounter-template select + saturating score + saturating multiply overflow fix; 39 gtests/108 asserts — zero/negative/extreme-dt, intensity-min/max saturation, score sub-unit/overflow/boundary-multiply, configure-replace/clear, single-template, tier-gap skip/select, all-same-tier tie-break, kRelief accelerated-decay + no-kBuildUp exit + direct-to-kIdle, natural-decay-to-idle, event-id advisory, EncounterTemplate field round-trip, exact-threshold boundary selection |
| cd::ai::pathfinding | 100 | production | 541 src; A* navmesh (Hart/Nilsson) + Triangulator (Recast/Snook); 30 gtests/97 asserts — has_navmesh state, heuristic-admissibility corridor, determinism, 3-component unreachable, same-tri explored=1, path-reconstruction total_distance, nearest_triangle index; empty-input no-throw, degenerate collinear drop, OOB-vert drop, configure round-trip, radius=0 disables filter, non-manifold edge no-throw, winding-independence |
| cd::game::anim_graph | 85 | production | 323 src; PlayClip + 1D/2D blend-tree tick over cd::anim Pose/Skeleton; 8 gtests/56 asserts |
| cd::game::asset_hot_reload | 85 | production | 309 src; note-then-flush over FileWatcher + create/delete probe + throttle; 7 gtests/61 asserts |
| cd::game::camera | 85 | production | 314 src; Cinemachine-style CameraBrain priority/blend + VirtualCamera; 11 gtests/39 asserts |
| cd::game::dialog_tree | 100 | production | 229 src; BG3-style condition-routed graph + kCondition chaining + O(1) hash index; +cyclic-kCondition depth-guard (1024) fix + 10 tests (18 gtests/84 asserts: chained conditions, broken-link, missing-false-branch, cycle, duplicate-id, empty-tree, deep-chain, reload, vars-survive-load) |
| cd::game::fsm | 100 | production | header-only by design (478-LOC Harel statechart: flat+hierarchical + shallow/deep history); .cpp is ABI anchor; 26 gtests/~92 asserts; gaps closed: guard-reject/unhandled-no-op/outer-before-inner/start-idempotent/accessor/shallow-vs-deep-3-level/history-never-visited(shallow+deep)/self-transition-hfsm/null-predicate/set_state-region-exit/orthogonal-regions/entry-exit-ordering |
| cd::game::particles_event | 85 | production | 228 src; recipe-staging burst dispatcher (no GPU coupling by design) + dense-handle + tombstone; 10 gtests/73 asserts |
| cd::game::query | 85 | production | 422 src; ray/AABB slab + frustum over spatial-hash broad phase + dedup, RTR4e/Williams; 10 gtests/40 asserts |
| cd::game::save_compression | 100 | production | 374 src; custom RLE codec + optional LZ4 block (gated); +7 edge tests (20 gtests: single-byte ratio, 128/129 run boundary, 256 two-packet, all-distinct worst-case, empty benchmark, size-mismatch guard, LZ4 corrupt-guard) |
| cd::game::trigger | 82 | production | 274 src; TriggerWorld enter/stay/exit + despawn-exit; brute-force O(V·S) broad phase (query-lib integ future); 12 gtests/51 asserts |
| cd::ai::squad | 75 | partial | 281 src; CPU squad/formation complete BUT GpuBatchSolver (gated) is thin dispatch, configure() stores capacity only, no buffer ownership (Sprint-3); 14 gtests/52 asserts |

**Group rollup: ~86%.** Most uniform cluster — 18/20 production, every lib has a substantive .cpp, documented format, citations and a dedicated gtest binary. Zero genuine kNotImpl/stub code (all "stub/TODO" hits are comments). Only ai_squad's GPU path is genuinely deferred.

---

## 4. engine/render — CORE (rhi + shader chain + framegraph/material/brdf/camera + render umbrella)

| Lib | % | Tier | Basis |
|---|---|---|---|
| cd::rhi | **100** | production | **24.5k src; Vulkan ~8.2k + D3D12 ~8.6k + Metal ~8.6k all real; IDevice 75 virtuals + ICmdBuf 46; 340 tests incl cross-backend PIXEL-PARITY + readback-parity capstone. Backend parity mega-marathon CLOSED (phases 1215–1225). ⚠ per-file header banners are STALE ("boot-only/50 virtuals stubbed") — don't baseline off comments.** |
| cd::gluon | 100 | production | Shader-MODULE lib: 21 embedded .glsl modules (~1.5k LOC SOTA-cited BRDF/tonemap/IBL/LTC) + 47-LOC resolver; 36 tests (+12 GPU-free registry/resolver-contract: canonicalisation, determinism, find_module boundaries, guard triad, catalogue integrity); no shader-math change (golden-safe); consumed by all 3 RHI backends |
| cd::material | 100 | production | 1035 src + 2.6k hdr (StandardPbr/LitPbr/Skinned/Sky/BrdfLut/RtClosestHit); 66 tests (+15 CPU-contract: alpha clamp/mode, albedo/emissive HDR clamp, release/move lifecycle, inert update guard, AlphaMode predicates, extra_set_layouts multi-set, missing-stage rejection); no BRDF/render change (golden-safe) |
| cd::shader | **100** | production | 756 src; CachedCompiler + real glslang-backed GlslangCompiler + FileWatcher hot-reload; **55 tests** incl live-edit + corrupt-cache self-heal + per-field cache-key sensitivity + closure (nested/unresolved/cyclic) + compute/HLSL/debug-info/all-stages + deterministic forced-mtime watcher edges; corrupt-cache-file EVICT path added (ADD-ONLY, no SPIR-V change). Slang path SEALED out-of-charter (ADR-20260620-shader-slang-gate-seal) — gate-stub retained |
| cd::camera | 85 | production | header-only 803 LOC; Camera/Frustum/FirstPerson/Orbit/CameraPath/Lens with real math bodies; 29 tests |
| cd::spirv_cross_glue | 85 | production | 394 src SPIRV-Cross bridge; GLSL/HLSL/MSL emit all compiled + exception→Result; only 4 tests (thin coverage) |
| cd::debug_line | 100 | production | header-only 363 LOC; RHI-independent CPU line-batch (AABB/OBB/frustum/circle → kLineList stream); 46 tests (phase1252: +26 edge/negative covering degenerate AABB/OBB/frustum, circle seg clamping 0/neg/2, capacity retention, full RGBA propagation, per-shape vertex counts, polyline 2-pt, arrow ±Y fallback + head_frac clamp, grid neg half_lines, sphere zero-radius + per-axis plane confinement); bgfx/Bevy cited |
| cd::hdr_display | 85 | production | header-only 100 LOC; PQ ST-2084 encode/decode + Rec2020 matrix + scRGB pack, cited constants; 6 tests |
| cd::brdf | 82 | production | header umbrella aggregating ltc/sheen_clearcoat/sss lobes; ~16 cited fns (Heitz/Estevez-Kulla/Burley); 17 tests; GPU lobes live in gluon |
| cd::scene_ingest | 82 | production | 275 src; ingest_gltf_scene DFS → GPU upload + ECS entity/transform + AABB accum + rollback; 13 tests |
| cd::framegraph | 80 | partial | 322 src + 495 hdr; real add_pass/compile/execute + per-pass timing + barrier helper; 20 tests; mostly linear execute, no full transient-resource aliasing/auto-cull |
| cd::debug_draw | 80 | partial | 213 src + 143 hdr immediate-mode shape accumulator; **only 5 tests** (weakest coverage in cluster); real impl, no stubs |
| cd::render (umbrella) | 78 | partial | Renderer.cpp 371 + 12 hdr (DrawBucket/SortKey/PostProcessChain/PlanarShadow); 42 tests; real aggregation, modest src depth vs header surface |
| cd::async_submit | 78 | partial | header-only (157+159); real thread+cv worker, single + N-slot render-thread primitive; 10 tests; deliberately minimal (no frame-fence integration) |
| cd::mesh_shader | 70 | partial | header-only Meshlet.hpp 218 LOC; real greedy clusterizer + bounds_sphere + cone_axis_cutoff (Karis Nanite); 6 tests; cone/spatial opt future, GPU dispatch in rhi |

**Group rollup: ~84%.** Anchored by cd::rhi at 100% — three genuinely-deep backends with a pixel-parity capstone. No real stub/skeleton libs here; the spread is production-vs-partial, not real-vs-fake. WARNING for the new plan: rhi's stale header banners badly understate completeness.

---

## 5. engine/render — FEATURES tier

| Lib | % | Tier | Basis |
|---|---|---|---|
| cd::restir_di | 83 | production | 1512 hdr + 2649 src/5 TUs; full RHI ReSTIR DI (initial/temporal/spatial reuse CS) + complete SVGF denoiser + orchestrators; 32 tests/162 asserts (real GPU); deepest GPU impl in cluster |
| cd::ddgi | 82 | production | 1522 hdr + 1529 src; full RHI 4-pass (trace/blend-irradiance/blend-visibility/sample) + descriptor writes/UAV barriers + ray_query; 32 tests/122 asserts (GPU) |
| cd::post | 80 | production | ~3.8k LOC/13 hdrs (composite 1147, exposure 532+102 .cpp, bloom 457, taa 214) — tonemap/bloom/TAA/SSR/GTAO/DOF/motion_blur/SMAA; 84 tests/197 asserts; broadest+best-tested |
| cd::lighting_clusters | 75 | production | 344 hdr + 850 src; clip-space sphere binning + AABB-sphere tight test + prefix-sum + glslang cluster-cull CS w/ dispatch; 13 tests/57 asserts; ⚠ overlaps cd::cluster |
| cd::ibl | 72 | partial | 762 LOC/5 hdrs; CPU bakers — split-sum BRDF LUT/irradiance/prefiltered specular/equirect→cube; 9 tests/24 asserts; GPU side in cd::ibl_gpu |
| cd::cluster | 70 | partial | 691 hdr + 506 src; RHI two-pass count/write compute + CPU reference + PBR lookup; 17 tests/36 asserts; ⚠ duplicates cd::lighting_clusters froxel assign |
| cd::light | 68 | partial | 739 LOC/5 hdrs; attenuation, Planckian color-temp, CSM split/matrix, ClusterGrid (data-only); 18 tests/40 asserts; ⚠ ClusterGrid overlaps lighting_clusters; no .cpp |
| cd::volumetric | 66 | partial | 889 LOC (VolumetricFog 445 inject/integrate/composite froxel GLSL + clouds + fog); 42 tests/89 asserts; header-only, no .cpp/RHI dispatch |
| cd::atmosphere | 62 | partial | 183 LOC header; Hillaire CPU transmittance-LUT baker (40-step) + GLSL CS; 6 tests/16 asserts; only 1 of 4 promised LUTs baked, no RHI dispatch |
| cd::denoise | 60 | partial | 210 LOC header; edge-aware a-trous (Dammertz) CPU + GLSL CS; 6 tests/10 asserts; OIDN backend is explicit pass-through stub; no .cpp/RHI |
| cd::decal | 58 | partial | 143 LOC header; world→decal-OBB projection + OBB-vs-AABB binning + GLSL helper; 6 tests/10 asserts; no atlas/blend pass, no .cpp/RHI |
| cd::virtual_textures | 56 | partial | 139 LOC header; PageId/AtlasSlot/feedback types + GLSL; 6 tests/19 asserts; no streaming/upload/indirection impl |
| cd::velocity | 55 | partial | 83 LOC header; motion-vector math (clip→uv delta) + velocity VS/FS GLSL; 5 tests/11 asserts; correct primitive, no .cpp/RHI dispatch |
| cd::virtual_geometry | 52 | partial | 1081 LOC/3 hdrs (ClusterDAG 474 BFS + GpuDispatcher 462); 19 tests/41 asserts; ⚠ LOD simplification is bbox-diagonal placeholder (no QEM), mesh-shader path no-op when unavailable — partial Nanite |
| cd::ibl_gpu | 48 | skeleton | 354 LOC INTERFACE header; real RHI upload helpers (cubemap/prefiltered/BRDF-LUT, float_to_half, barriers) BUT **0 tests** (deferred to renderer integ); correctness unverified |
| cd::gpu_particles | 45 | demo-only | 103 LOC header; CPU advance + compact_alive + one simulate GLSL CS; 6 tests/7 asserts (thinnest); no emit kernel, no indirect-draw wiring |
| cd::light_shafts | 42 | demo-only | 108 LOC header; sun-screen-projection + Mitchell radial-blur GLSL; 3 tests/4 asserts; promised analytic epipolar (Kim&Marsalek) NOT implemented |
| cd::nrc | 40 | skeleton | 150 LOC header; tiny single-hidden-layer CPU MLP (Muller NRC); 3 tests/4 asserts (weak — allows zero gain); CUDA-NN/SPIR-V backends stubbed; not a usable cache |
| cd::restir_gi | 38 | skeleton | 373 LOC header; real reservoir combine/clamp/weight CPU + GLSL CS BUT sample CS uses explicit "placeholder hit"/"placeholder cached radiance" — GI not actually traced; 5 tests/8 asserts |

**Group rollup: ~61%.** Two tiers: a STRONG RHI-wired GPU set (restir_di/ddgi/post/lighting_clusters/cluster) and a MID header-only "CPU-reference + GLSL-string" set whose on-screen behaviour depends on the consumer (hello_engine) compiling the embedded GLSL — so library-level % overstates end-to-end readiness. WEAK: restir_gi/nrc/ibl_gpu/gpu_particles/light_shafts/virtual_geometry carry explicit placeholders for their core work. **Systemic caveat: froxel light-cluster binning is duplicated 3× (cluster ↔ lighting_clusters ↔ light::ClusterGrid).**

---

## 6. engine/world

| Lib | % | Tier | Basis |
|---|---|---|---|
| cd::ecs | 100 | production | ~2.1k LOC hdr-inline (sparse-set World + Scheduler 407 + ArchetypeWorld 543 side-layer + SystemGraph); 179 .cpp; 101 gtests/6 files; dual storage, no stubs. Smallest-pool `each<>` driver REALISED (was dead MVP code) + snapshot-safe mid-walk mutation; edge/negative/stress depth: generation-recycle invariants, tombstone reuse, large-id sparse growth, non-trivial-dtor lifetime nets through swap-pop + cross-archetype migration, scheduler acyclicity invariant, query-cache structural invalidation; README de-lied (no phantom deferred-lifecycle/commit API) |
| cd::scene | 100 | production | ~2.1k LOC/21 hdrs (Scene graph + Serializer 314 + BinarySerializer 259 + Frustum/SpatialHash/Light/Trigger); 137 gtests (+51 edge: JSON/binary serializer corrupt/truncated/forward-compat branches, frustum exact-plane boundary, spatial-hash dense/negative/boundary cells, trigger re-entry, deep-hierarchy transform compose, detach-mid-tree, LOD/Layer/NameRegistry/Polyline degenerate); no transform/cull/query change (golden-safe) |
| cd::anim | 85 | production | ~1.7k LOC hdr-inline (DualQuat/Kavan, StateMachine, BlendTree2, PoseBlend, Skeleton, GpuSkinning); 61 gtests; GPU skinning host-side data prep only |
| cd::anim_ik | 85 | production | 492 LOC CCD solver + joint limits (Sprint-2); 294 hdr; 11 gtests; real convergence; FABRIK not present |
| cd::gameplay_input_binding | 85 | production | 269 LOC ActionMap (axis/button binding, threshold heuristics); 245 hdr; 11 gtests; complete Sprint API |
| cd::gameplay_time | 85 | production | 82 LOC Time.cpp (game clock/fixed-step/scale); 154 hdr; 10 gtests; small but complete |
| cd::input | 85 | production | 818 LOC/9 hdrs (Axis/Hold/DoubleClick/KeyChord/Gamepad/MouseDrag) + thin 38-LOC pump; 38 gtests; header-inline FSM |
| cd::net | 85 | production | ~2.4k LOC hdr-inline (ReliableChannel 390/Retransmit 566/QosDispatcher/SnapshotReconciler/Delta/Prediction/RLE) + real UdpConnection 254 (winsock); 89 gtests |
| cd::physics | 85 | production | ~900 LOC/14 collision primitives (Aabb/Obb/Capsule/Sphere/Ray/Triangle/Sweep/Inertia + IPhysicsWorld); 194 .cpp; 55 gtests |
| cd::net_lobby | 82 | production | 464 src (Lobby 168 + Packet 132 + SocketTransport 133); 507 hdr; 20 gtests; real lobby + socket transport |
| cd::world_container | 82 | production | header-only 946 LOC/7 (LevelStreamer 188 residency + ProjectIo 363 JSON + Level/Layer/Project/World); 19 gtests; real v1.7 level-switch streaming |
| cd::audio | 80 | production | ~1.6k src/6 backends (WASAPI 543/CoreAudio 302/ALSA 275/FileSink/Null/Native); 86 gtests; platform-gated, mostly Win-verified |
| cd::audio_spatial | 80 | production | 705 src (HrtfMixer 437 + AudioSpatial 268); 23 gtests; real HRTF mix + spatialization |
| cd::net_matchmaker | 80 | production | 302 .cpp (SkillScorer + FillStrategy, phase784); 283 hdr; 11 gtests; registry + scoring complete |
| cd::physics_soft_body | 80 | production | 451 .cpp PBD/Verlet + Sprint-2 self-collision (spatial-hash + impulse); 259 hdr; 10 gtests; real cloth/soft sim |
| cd::sample_framework | 80 | production | 109 src (App lifecycle 87 + run 22); 156 hdr; 5 gtests; small but complete app-harness, intentionally thin |
| cd::net_session_replay | 78 | production | 230 .cpp SRPK record/replay binary format; 149 hdr; 7 gtests; Sprint-1 complete, small surface |
| cd::physics_jolt | 78 | partial | 1164 .cpp real Jolt 5.x backend (CD_PHYSICS_JOLT_REAL) + Euler stub fallback when vendor absent; 414 hdr PIMPL; 13 gtests; real path gated on build flag |
| cd::audio_dsp_fx | 75 | partial | header-only DSP (311: filters/FDN reverb scaffolding) + 179-LOC anchor .cpp (no SIMD yet); 11 gtests; Schroeder FDN tuning/SIMD deferred |
| cd::physics_vehicle | 75 | partial | 638 src bicycle model (longitudinal) + Jolt WheeledVehicle delegation; 16 gtests; lateral/Pacejka deferred, Jolt path gated |
| cd::particle_system | 65 | partial | 258 LOC SoA CPU sim (semi-implicit Euler, swap-pop, spawn accum); 7 gtests; GPU dispatch + force fields explicitly out-of-scope (documented) |

**Group rollup: ~81%.** Mature and uniform — ~660 gtests cluster-wide, all libs ship real impl. Caveats are all documented: particle_system is CPU-only by scope; physics_jolt/vehicle reach the real Jolt backend only with the vendored build flag; audio_dsp_fx defers SIMD/FDN tuning. The 22 stub-text hits are confined to those documented fallback paths, not silent gaps.

---

## 7. engine/ui

| Lib | % | Tier | Basis |
|---|---|---|---|
| cd::ui_layout | 100 | production | Flex + Constraint; incremental 3-phase solver (compute_main/compute_cross/position_children real impl); kAbsolute inset placement; margin main+cross; kDuplicate detection; 51 gtests (23→51, +28 edge/negative/error-path); gap_cross sealed (multi-line only, no-wrap scope) |
| cd::ui_animation | 85 | production | 213 src + 232 hdr; full Penner easing (quad/cubic/elastic/back/bounce) + Tweener + Timeline, cited; 14 gtests; self-contained |
| cd::ui_input | 85 | production | 559 src + 450 hdr; HitTester (z-order) + FocusManager (tab + modal stack) + GestureRecognizer FSM (press/drag/long/double/pinch/swipe); 21 gtests |
| cd::ui_theme | 85 | production | 175 src + 293 hdr; Material 3 tokens, dark/light/high-contrast + brand override + WCAG on-color + spacing/motion/elevation; 22 gtests |
| cd::ui_renderer | 85 | production | 134 src + 200 hdr; CPU batcher, quad/textured/glyph emit, u16 indices, scissor stack, adjacent-merge; 10 gtests; no auto-split >65535 (documented) |
| cd::ui_renderer_rhi | 80 | partial | 712 src + 240 hdr; ring vb/ib over cd::rhi + Route A (caller pipeline) + Route B glslang inline solid-quad + material UI-variant; 12 gtests; draws only on real backend (Vulkan), glyph atlas path lands w/ material variant |
| cd::ui_widgets | 80 | partial | 6797 LOC, 8 widgets (ColorPicker 786/CurveEditor 819/DockSpace 939/Table/TreeView/PopoutDock) + NativeWindowAdapter (208, Win32-only); 99 gtests/9 files — strongest non-editor coverage; TODOs are documented cross-platform gaps |
| cd::ui_font | 78 | partial | 1528 LOC; stb_truetype always-on raster + skyline pack + MSDF + optional FreeType/HarfBuzz/msdfgen behind gates; 13 gtests; **default build has no real shaping** (identity + stb raster) |
| cd::ui_a11y | 75 | partial | 153 src + 251 hdr; A11yTree register/meta/tab-nav + constexpr WCAG contrast + focus rect; 11 gtests; baseline only — no screen-reader/AT-SPI/UIA bridge |
| cd::ui (umbrella) | 70 | partial | Widget.cpp 52 (retained-mode tree hit-test/dispatch/draw) + 373 hdr (Panel/Label/Button) + 9 header widgets; 58 tests; minimal widget surface, text is just a DrawKind command (no glyph layout) |
| cd::editor (shell + editor_ui + 27 panels) | 68 | partial | ~15.8k src / ~10k test / 497 cases; shell wires ecs::World+scene+input+widget tree; 27 panels bind real engine systems (IkChainEditor 756, Cutscene 555) BUT most render simplified DrawBatcher visuals (accent bars for text, gradient placeholders) — only Inspector uses ImGui; heavily tested, visually incomplete vs a real DCC |
| cd::ui_renderer_webgpu | 35 | skeleton | 306 src + 184 hdr; default CD_UI_WEBGPU_HAVE_DAWN=0 path is pure no-op (counts verts only); Dawn path allocates buffers but WGSL pipeline/shader deferred to unimplemented "Phase 5.5"; 5 build-only tests |

**Group rollup: ~72%.** Clean architecture (renderer-agnostic widget tree → batcher → RHI/WebGPU). Strong feature libs (layout/input/animation/theme). The honest gap is feature-completeness/visual-fidelity, not unimplemented skeletons: the editor is the biggest single area and most LOC-overstated (real bindings + exhaustive tests, but placeholder visuals + no real in-panel text); webgpu backend is a no-op stub; default font build has no shaping.

---

## Overall Engine Completion

| Group | Libs | Rollup % |
|---|---|---|
| engine/foundation | 20 | ~79% |
| engine/asset + runtime + tooling | 15 | ~64% |
| engine/game | 20 | ~86% |
| engine/render — CORE | 15 | ~84% |
| engine/render — FEATURES | 19 | ~61% |
| engine/world | 21 | ~81% |
| engine/ui | 12 | ~72% |
| **ENGINE TOTAL** | **122** | **~76%** |

**Weighting method: lib-count-weighted mean of all 122 per-lib %s (equal weight per library).** This is the honest baseline. Note two distortions if you re-weight: (a) **LOC-weighting would push the number UP** — the single biggest lib, cd::rhi (24.5k LOC, now 100%), plus the large game/world/render-core production libs dominate code volume, while most of the FEATURES laggards are tiny header-only files; a LOC-weighted estimate lands closer to ~80-82%. (b) **End-to-end "shippable feature" weighting would push it DOWN** — many FEATURES-tier libs are header-only algorithm+GLSL-string with no standalone GPU runtime, so their library-% overstates on-screen readiness until the consumer wires them. The ~76% lib-count figure is the most defensible single number for planning.

---

## Strongest / Weakest / Highest-Leverage

**Strongest (real, deep, well-tested):**
- **cd::rhi — 100%.** Three genuinely-deep backends (Vulkan/D3D12/Metal), 75+46-virtual interface, 340 tests with a cross-backend pixel-parity + readback-parity capstone. The crown jewel. (⚠ stale header banners lie about completeness — fix them so the next reader isn't misled.)
- **engine/game (~86%) and engine/world (~81%)** as whole clusters — uniformly production, every lib has substantive .cpp + documented formats + citations + a dedicated gtest binary.
- Standout individual libs: cd::concurrency, cd::math, cd::ecs, cd::scene, cd::material, cd::gluon, cd::restir_di, cd::post.

**Weakest (explicit placeholders for core work):**
- **cd::restir_gi (38%)** — GLSL has explicit "placeholder hit"; GI is not actually traced.
- **cd::nrc (40%)** — toy CPU MLP with a weak test; production CUDA/SPIR-V backends stubbed.
- **The three asset streamers (texture/scene/audio, ~45%)** — real queue+threading, but decode is an explicit Sprint-2 placeholder (fake 1×1 handles / synthetic IDs).
- **cd::ui_renderer_webgpu (35%)** — default path is a pure no-op; Dawn pipeline deferred to an unimplemented phase.
- **cd::gpu_particles (45%), cd::light_shafts (42%), cd::ibl_gpu (48% + 0 tests), cd::virtual_geometry (52%, bbox-diagonal LOD instead of real QEM).**

**Highest-leverage for the new plan:**
1. **Asset streamer decode backends** (3 libs × ~45% → production) — they already have the hard part (orchestration) done; wiring real WAV/glTF/cdtex decode unlocks actual streaming and lifts the whole asset cluster.
2. **De-duplicate froxel clustering** (cd::cluster ↔ cd::lighting_clusters ↔ cd::light::ClusterGrid) — the same binning exists 3×; consolidating removes a standing maintenance hazard and clarifies ownership.
3. **The editor (68%, ~15.8k LOC)** — biggest single surface; the bindings and tests exist, so investment in real text/glyph rendering + ImGui-izing panels converts a lot of latent work into visible DCC capability.
4. **FEATURES GPU-wiring tier** (denoise/atmosphere/volumetric/velocity/decal/virtual_textures, 55-72%) — these are CPU-reference + GLSL-string libs; giving them real .cpp RHI dispatch (the cd::ddgi/restir_di pattern) closes the "library-% vs end-to-end" gap that most inflates the current baseline.
5. **Refresh cd::rhi header banners** — near-zero effort, prevents the next planner from re-baselining off "boot-only/stubbed" comments that are now false.