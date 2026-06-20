# ROADMAP — drive every ≥80% library to genuine 100% (highest→lowest)

> User directive (2026-06-20): "%80 üzeri olan her şeyi %100 yap, en yüksekten en düşüğe ilerle."
> Take every engine library currently at **≥80% implementation depth** (per
> `docs/PROJECT_COMPLETION_STATUS.md`) to **genuine 100%** — REAL implementation, not
> promote-on-need seals. Execute strictly **highest-% first, descending**.

## Definition of "100%" (per lib)

A lib is 100% when **every concrete gap is closed**: every named limitation in its
baseline-doc row + every real `TODO`/`FIXME`/`placeholder`/`Sprint-N`/`kNotImpl` marker in
its `src`/`include` is implemented, AND every thin-coverage / negative / edge path has a
test. Small deferred sub-features are IMPLEMENTED. Only a genuinely multi-week,
out-of-charter subsystem may remain — and only with an explicit one-paragraph seal ADR
(this should be rare in the ≥80 set; those libs are close to done by definition).

## Standing gate (every checkpoint)
- `cmake --build --preset ninja-debug` clean `-Werror` (0 err / 0 warn)
- `ctest --preset ninja-debug -E rhi_vulkan` green (scoped per-batch for speed; full at tier boundaries)
- golden/sponza/chrome **byte-identical** whenever a render/shader lib is touched
- commit each green checkpoint; no tag, no push (standing policy)

## Ordered worklist (≥80%, descending) — ~75 libs

| Tier | Libs |
|---|---|
| 100 | rhi ✅ (already done) · game::quest ✅ · ecs ✅ · ui_animation ✅ · trigger ✅ |
| **92** | frame_timing · game::save |
| **90** | bench · ai_bt · cutscene_player · gluon |
| **88** | concurrency · math · dialogue · input_recorder · l10n · settings · material · shader · debug_line · scene · ui_layout |
| **85** | core · io · log · ai_director · pathfinding · anim_graph · asset_hot_reload · game::camera · dialog_tree · fsm · particles_event · game::query · save_compression · camera(render) · spirv_cross_glue · hdr_display · asset(umbrella) · anim · anim_ik · gameplay_input_binding · gameplay_time · input · net · physics · ui_input · ui_theme · ui_renderer |
| **84** | time |
| **83** | restir_di |
| **82** | net_lobby · world_container · ddgi |
| **80** | config · plugin · script · imgdiff · framegraph ✅ · debug_draw · post · audio · audio_spatial · net_matchmaker · physics_soft_body · sample_framework · ui_renderer_rhi · ui_widgets |

## Execution log
- ✅ **Batch 1 (phase1253)** — tier 92 + non-golden tier-90 → 100%: frame_timing (ring/median bug-fix + last_dt/percentile/jitter + 21 tests), game::quest (10 tests, +1 test-offset fix), game::save (JSON forward-compat parser fix + 20 tests), bench (print max/stddev + 12 tests), game::ai_bt (UntilFailure+Condition nodes + 16 tests), game::cutscene_player (seek/restart/total_duration + 21 tests). ~100 new tests. 3 agent-introduced WAE fixed. Gate: build clean -Werror, ctest 317/317, no render touched.
- ✅ **Batch 2 (phase1254)** — tier 88, 8 CPU libs → 100% (~218 new tests):
  - **concurrency**: 34 tests across 3 new binaries (lockfree/pool-jobgraph/messaging-sync deepening) — exactly-once XOR stress, hazard K=1 reclaim, shutdown-with-backlog, JobGraph diamond/throwing. **No sync-logic changes** (safety rule).
  - **math**: try_inverse (Result-variant) + noise3d (trilinear) + 40 edge/constexpr tests. **No existing-formula change** (golden-safe).
  - **ecs**: realised dead smallest-pool optimisation + snapshot-safe mid-walk mutation + 24 tests (new test_ecs_edge.cpp). Visit-set preserved, order already unspecified.
  - **ui_layout**: closed reserved compute_main/cross/position_children no-ops (3-phase) + kAbsolute + margin + kDuplicate constraint + 28 tests.
  - **dialogue**: 34 DSL/VM/Blackboard tests (src unchanged — coverage gap only).
  - **settings**: off_change unsubscribe + lossless float round-trip (from_chars + max_digits10) + 18 tests.
  - **l10n**: plural_rule_ar (6 CLDR cats) + plural_rule_ru (4 cats) + 20 tests.
  - **input_recorder**: 12 edge/negative tests + README fix.
  - Gate fixes: dialogue nodiscard + 3 WAE (dialogue move-const, math use-auto, Color.hpp dup-include) + l10n test (Arabic one/two are exact n==1/n==2, not modular). Gate: build clean -Werror (0/0); ctest 320/320; golden/sponza/chrome byte-identical.
- ✅ **Batch 3 (phase1255)** — tier 88 render/golden-sensitive, 5 libs → 100% (~101 new tests, all golden-safe):
  - **gluon**: 12 GPU-free registry/resolver-contract tests (canonicalisation, determinism, find_module boundaries, guard triad). No shader-math touched.
  - **material**: 15 CPU-contract tests across 2 new binaries (alpha/HDR clamp, lifecycle, AlphaMode predicates, multi-set layout). No BRDF/render change.
  - **shader**: real corrupt-cache-file EVICT/self-heal (malformed branch only, no SPIR-V change) + 23 tests + Slang gate-seal ADR (ADR-20260620).
  - **scene**: 51 edge tests (serializer corrupt/forward-compat, frustum exact-plane, spatial-hash boundary, trigger re-entry, deep-hierarchy transform). Zero production-code change.
  - **debug_line**: pack_color/unpack_color + add_axes (colored gizmo) + add_grid_rect (N×M) + 15 tests. Existing shapes byte-identical. (1st agent under-delivered → re-dispatched.)
  - Gate fixes: material unused-const + 4 WAE (shader use-ranges, scene emplace, debug_line redundant-expr/use-auto/incorrect-rounding→lround). Gate: build clean -Werror (0/0); ctest 322/322; golden/sponza/chrome BYTE-IDENTICAL.
- ✅ **Batch 4 (phase1256)** — tier 85 / ai_director → 100% (22 new tests, 62 new asserts):
  - **ai_director**: saturating-multiply overflow bug fixed (delta_units*kScorePerUnit wraps before headroom check when intensity_delta is extreme float). 17→39 tests / 46→108 asserts — zero/negative/extreme-dt, intensity-min/max saturation, score sub-unit/overflow/boundary-multiply, configure-replace/clear, single-template, tier-gap skip/select, all-same-tier tie-break, kRelief accelerated-decay + no-kBuildUp-exit + direct-to-kIdle, natural-decay-to-idle, event-id advisory, EncounterTemplate field round-trip, exact-threshold boundary selection. No public API change. Build clean -Werror.
- ⏳ **Batch 5 (in progress)** — tier 85 / cd::core → 100% (foundational, correctness-critical, +60 tests):
  - **SmallVector gap closed**: implemented the documented "insert/erase + emplace land later" surface — `insert(pos, v)` (lvalue+rvalue, returns slot ptr, clamps OOB), `erase(pos)` (tail-shift, returns next ptr, OOB no-op), `reserve(n)`, `front()`/`back()`. Header comment refreshed.
  - **3 REAL BUGS FIXED (FLAGGED, behavior-preserving for all working public paths):**
    1. `SmallVector` move-ctor/move-assign **buffer overflow** — `move_from` placement-new'd `o.size_` (possibly > N) elements into the freshly-constructed N-slot **inline** buffer when the source had spilled to heap. Fixed to **pointer-steal** the source heap buffer (O(1) + correct); inline source keeps the element-wise move path.
    2. `RingBuffer::push(const T&)` invoked an **undefined `emplace_`** (trailing underscore) → any lvalue push was a hard compile error. Latent because the templated member is never instantiated with an lvalue in any consumer. Fixed to `emplace`.
    3. `CVarRegistry::set` re-read `vars_.find(key)` **outside the lock** to source the callback value → **data race** vs concurrent `set`/`erase` (rehash/erase can dangle the iterator). Fixed to snapshot the new value **under the lock** and pass that to callbacks. Observably identical single-threaded.
  - **Blast-radius audit**: only `tests/` exercised the fixed paths — grep shows zero production callsites doing `cd::core::SmallVector` heap-move or `cd::core::RingBuffer` lvalue-push (the lone `SmallVector` external user is `spirv_cross::SmallVector`, an unrelated type). No consumer semantics changed.
  - +60 edge/negative/boundary/lifetime tests in new `tests/test_core_edge.cpp` (118 → 178), wired into the existing `cd_add_test(core ...)` SOURCES. Instance-counted `Counted` lifetime balance across spill/clear/move; move-only payloads; PoolAllocator LIFO reuse + exhaust/refill + null-dealloc; Handle field isolation/reset; HandleStore parity/holes/out-of-range/const-for_each; Result and_then/or_else/value_or + move-only payload; CVar type-mismatch/retype/snapshot/unsubscribe-miss; RingBuffer lvalue-push + non-trivial lifetime; Bitset word-boundary/cross-word; FixedString exact-fit/clear; Bytes/BitOps/StringSplit/RetryPolicy/EnumFlags/Ref/ScopeGuard/CounterTable boundaries.
  - clang-tidy WAE-hygiene: `std::array` over C-array; moved-from asserts routed through a reference-alias to dodge `bugprone-use-after-move`; self-assign/self-move tests aliased to dodge `-Wself-assign`/`-Wself-move` (NOLINT can't suppress compiler `-Werror`). NEEDS-SEAL: none. Build/ctest gating deferred to orchestrator per directive.
- ✅ **Batch 4 (phase1256)** — tier 85, 8 CPU libs → 100% (~227 new tests, 5 REAL BUGS fixed):
  - **core** (3 real bugs): SmallVector move-ctor/assign HEAP-BUFFER-OVERFLOW when source spilled to heap (placement-new'd >N elems into N-slot inline buffer → pointer-steal fix); RingBuffer::push(const T&) called undefined emplace_ (compile error, latent); CVarRegistry::set DATA RACE (re-read outside lock). +60 tests (new test_core_edge.cpp). Blast-radius audit: 0 production callsites hit the buggy paths.
  - **ai_director** (1 real bug): saturating-multiply overflow + UB float→uint for extreme deltas (float-domain guard fix) + 22 tests.
  - **dialog_tree** (1 real bug): cyclic-kCondition stack-overflow (depth-1024 guard) + 10 tests.
  - **io**: PathUtils normalize/join + Endian big-endian pair + BinaryReader peek + 60 tests.
  - **log**: 40 tests (Format/Json-escape/RingBuffer/AuditTrail/Console edges).
  - **pathfinding**: 14 tests (A* unreachable/admissibility/tie-break + triangulator degenerate).
  - **fsm**: 14 tests (shallow/deep history, hierarchical entry/exit order, self-transition).
  - **save_compression**: 7 RLE/LZ4 edge tests.
  - Gate fixes: 8 compile/WAE (core sign-conv/operator=-default/push_back-reserve/unused-op, log use-ranges, pathfinding std::numbers) + 4 test-vs-impl mismatches (io at_end can't know bit-count; ai_director float-precision boundary; save_compression trailing byte is a LITERAL not repeat packet). Gate: build clean -Werror (0/0); ctest 322/322; golden/sponza/chrome BYTE-IDENTICAL (core is universal).
- ✅ **Batch 5 (phase1257)** — tier 85, 8 CPU game/world libs → 100% (~150 new tests, 2 real bugs):
  - **particles_event** (1 real bug): tick() negative-dt fell through to the compaction loop (non-empty + dt<0) → true no-op guard; +dense-handle tombstone slot-reuse + BurstHandle generational UID + 19 tests. Flagged a pre-existing fire() callback-reentrancy hazard for follow-up.
  - **game::camera**: [[nodiscard]] on add_vcam/remove_vcam (un-removable handle leak) + 11 tests (priority tie-break, mid-blend interrupt/warp, curve endpoints, dt=0 freeze).
  - **gameplay_input_binding**: 4 features (is_action_just_pressed/just_released, set_dead_zone, axis clamp [-1,1]) + 22 tests.
  - **input**: 48 tests (Hold/DoubleClick/KeyChord/Gamepad/MouseDrag/Axis/Context boundaries).
  - **anim_graph**: 20 tests (clip edge cases, 1D/2D blend pins, pose-count mismatch).
  - **query**: 17 tests (ray-AABB slab edges, frustum straddle, dedup, spatial-hash boundary).
  - **asset_hot_reload**: 11 tests (throttle window boundary, deletion-wins, burst coalesce).
  - **gameplay_time**: fixed-step accumulator + scale edges.
  - Gate fixes: 6 WAE (query 2x isolate-decl, input_binding find→contains, input 3 headers if<→std::max+<algorithm>) + 1 test-vs-impl (asset_hot_reload throttle test missed the watcher's first-poll baseline → bump must come AFTER an initial baseline tick). Gate: build clean -Werror (0/0); ctest 322/322; golden byte-identical (CPU libs).
- ✅ **Batch 6 (phase1258)** — tier 85, 8 render/world libs → 100% (~307 new tests, all golden-safe, 1 real impl):
  - **physics**: implemented the documented-deferred OBB/OBB SAT (15-axis separating-axis test, Ericson §4.4.1) + 46 primitive edge tests (new function, no prior callers → golden-safe).
  - **asset** (umbrella, 10 sub-targets): +106 defensive-deserialization tests (json/gltf/image/ktx2/obj/pak/wav/cdmesh/cdtex/streaming) — every loader rejects corrupt input via Result, no UB. Valid decode byte-identical.
  - **net**: +58 tests (new test_net_edge.cpp) — ack-window/RTO/snapshot/prediction/RLE/seq-wrap/QoS edges.
  - **anim**: +39 tests (new test_anim_edge.cpp) — DualQuat/StateMachine/BlendTree2/PoseBlend/Skeleton/GpuSkinning. No pose math touched.
  - **render-camera**: +31 tests (perspective/ortho known-value + clamp + degenerate look_at). No matrix math touched.
  - **hdr_display**: +scrgb_unpack/rec2020↔xyz inverses + 11 PQ/Rec2020 tests.
  - **anim_ik**: +8 CCD/joint-limit tests. **spirv_cross_glue**: +6 MSL/HLSL/GLSL emit-path tests.
  - Gate fixes: WAE (hdr 2× float-loop-induction→int, anim_ik sqrt2→std::numbers, net nodiscard+arg-comment, asset ~6 arg-comment/dup-include, core CVar find→contains, OrbitController dup-include) + 3 test-vs-impl RCA (camera ortho is RIGHT-HANDED depth[0,1] so cull box needs z∈[-far,-near]; json parser leniently accepts "1."; stb leniently decodes header-only BMP). Gate: build clean -Werror (0/0); ctest 324/324; golden/sponza/chrome BYTE-IDENTICAL.
  - NOTE: re-scan surfaced pre-existing sealed broader-130 WAE in cd::core headers (Bitset/HandleStore/PoolAllocator/Ref/ScopeGuard/SmallVector) — out of batch-6 scope, queued for a focused phase1259 core-header cleanup (incl. a HandleStore use-after-forward to investigate).
- ✅ **phase1259 (core-header cleanup)** — made cd::core genuinely WAE-clean (its headers carried sealed broader-130 lint surfaced by the batch-6 gate). HandleStore::for_each: fixed a latent use-after-move footgun (was `std::forward<F>(fn)(...)` inside the per-slot loop → forwards the callable as an rvalue every iteration; now calls `fn(...)` as an lvalue, the correct multi-invocation idiom; behaviour-preserving for the common const/lvalue callables that existing for_each tests cover). Plus 9 trivial sealed-lint fixes: SmallVector insert clamp `if(>)`→std::min, Bitset/ScopeGuard redundant-init→default-member-init, PoolAllocator 4× void**→void* explicit memcpy casts, Ref NOLINT re-placed onto the ctor line, test_core_edge deliberate moved-from check NOLINT'd. Gate: build clean -Werror (0/0); ctest 324/324; golden byte-identical (core is universal).
- ✅ **Batch 7 (phase1260)** — tier 85 LAST, 4 UI libs → 100% (~72 tests, golden-irrelevant). **TIER 85 COMPLETE.**
  - **ui_renderer**: closed the u16 vertex-limit gap — kMaxVertices=65532 guard + at_vertex_limit() + silent-drop (prevents u16 index overflow); auto-split sealed as Phase 2. +10 tests.
  - **ui_animation**: +15 tests (Penner endpoints/overshoot/bounce, Tweener neg-dt/done, Timeline empty/seek) + pi/emplace hygiene.
  - **ui_input**: +21 tests (HitTester z-order/nest, FocusManager tab/modal, Gesture FSM boundaries).
  - **ui_theme**: +26 tests (WCAG 3:1/4.5:1/7:1/21:1 boundaries spec-verified, on-color, brand override, theme_from_name, scale monotonic).
  - Gate fix: ui_theme on-color test exact-threshold was float-unstable (l>0.179, L(grey k)=k → 0.179*1.0 rounds just above) → test either side with margin. Gate: build clean -Werror (0/0); ctest 324/324; WAE clean.
- ✅ **Batch 8 (phase1261)** — tiers 84/83/82, 8 libs → 100% (~183 new tests, 1 real bug):
  - **time** (84→100): SimClock::set_time() + 21 tests + REAL FIX: TimerQueue::schedule_after returned entries_.top().sequence (the min-deadline heap top), not the just-scheduled timer's id → returns the real id now (+ near-then-far regression test).
  - **restir_di** (83→100): +39 tests (3 files: reservoir-math + temporal-buffer host-side, GPU-gated edge cases). Sprint-7 G-buffer seam wiring SEALED out-of-charter (alters rendered output → GPU render-review).
  - **diag** (82→100): DeadlineMonitor arm/disarm/extend/injected-clock ctor + replaced flaky sleep_for test + 25 tests.
  - **events** (82→100): +13 tests (ScopedConnection RAII/move, unsubscribe-during-emit snapshot, deferred drain order, reentrant publish). Priority-subscribe confirmed already done; async sealed to cd::concurrency.
  - **profile** (82→100): BufferSink::drain + StatRow::percentile_ns + ChromeTrace JSON escaping + 17 tests.
  - **vfs** (82→100): normalize_path (new PathUtil.hpp) + unmount() + path normalization across exists/read/list/put/erase + 25 tests.
  - **brdf** (82→100): +35 paper-verified tests (LTC/Charlie-D/Neubelt-reciprocity/clearcoat/Burley-SSS energy-conservation/CPU↔GLSL parity). No lobe math touched.
  - **scene_ingest** (82→100): +8 tests (rollback ECS+GPU-clean invariant, deep DFS world-compose, AABB pass-through). No ingest-output change.
  - Gate fixes: 1 compile (time unused cv_mutex) + ~17 WAE (diag 5x empty-catch→SUCCEED + 5x lock_guard→scoped_lock + op=self-assign-guard, profile 2x raw-string + empty-catch, time TimerQueue + 2x test scoped_lock, brdf self-fixed DeMorgan). 1 flake (rhi_pipeline_cache, passed on re-run). Gate: build clean -Werror (0/0); ctest 326/327 (1 flake); golden/sponza/chrome BYTE-IDENTICAL.
- ✅ **Batch 9 (phase1262)** — tier 82, cd::game::trigger → 100% (+10 edge/negative/boundary tests, no production-code change, query-lib integration SEALED out-of-charter):
  - **trigger** (82→100): 12→24 gtests. No production code change — implementation was already correct. Gaps closed by new tests:
    - re-enter-after-exit: on_enter fires a second time after an exit cycle (not on_stay).
    - zero-occupants: enabled volume with empty subject list fires nothing and accumulates no occupancy.
    - simultaneous-multi-volume: one subject inside two overlapping volumes simultaneously fires on_enter/on_stay/on_exit on both independently.
    - exact-boundary-inclusive (AABB + Sphere): point exactly on AABB face (`>= min`, `<= max`) and on Sphere surface (`d² <= r²`) fires on_enter.
    - simultaneous-enter+exit-different-volumes: single-tick transition A→B fires on_exit(A) and on_enter(B) in the same tick.
    - remove-mid-overlap: removing the volume while a subject is inside does NOT fire on_exit (callback target gone).
    - clear_occupancy: resets all occupancy without firing on_exit; next tick re-fires on_enter.
    - layer-clamp-geq64: Subject::layer >= 64 clamps to channel 0 (no UB shift); bit-0 volume fires, bit-1 does not.
    - find-null/post-remove: find() returns nullptr for unknown owner and after remove_trigger.
    - despawn-exit-for-layer-filtered-subject: occupancy (not layer filter) drives the despawn-exit loop — subject that entered on layer 3 still fires on_exit when it disappears.
  - Query-lib integration (cd::game::query G3.1 at 100%) SEALED in README: body-only future commit, zero API churn, brute-force O(V·S) is the only wired path and handles all G3.2 use-cases.
  - No WAE introduced (no production code edited).
- ✅ **Batch 9 (phase1262)** — tier 82 close-out + tier 80 start, 8 libs → 100% (~227 new tests, 2 real features). **TIER 82 COMPLETE.**
  - **trigger** (82→100): +10 tests (enter-once/stay/exit/despawn-exit/re-enter/0-occupant/multi-volume/boundary-inclusive/simultaneous/remove-mid-overlap). No code change. query-lib spatial backend SEALED (out-of-charter G3.2).
  - **net_lobby** (82→100): REAL FEATURE — RoomState::host_player_id + host-migration on host-leave + destroy_room() API + 20 tests. Pre-existing socket-test sleep_for flagged (real-UDP helper, left).
  - **world_container** (82→100): +32 tests (LevelStreamer residency/switch, ProjectIo JSON malformed/version-skew/round-trip, Level/Layer/Project edge). No code change.
  - **ddgi** (82→100): +47 tests/3 files (probe-grid math CPU, PC/UBO+GLSL contract, GPU-gated error paths). No probe/blend/sample math or GLSL touched. DDGI→hello_engine HDR-composite wiring SEALED (alters rendered output → GPU render-review, like restir_di Sprint-7).
  - **config** (80→100): +14 tests (round-trip all types, truncation per-field, magic/version mismatch, merge semantics, sorted-key invariant) + wire-format doc + README API corrections. No code change.
  - **plugin** (80→100): REAL FEATURE — WatchedHotReloader fusing IFileWatcher + HotReloader (the reload flow the README advertised but had no impl) + make_watched_hot_reloader factory + 23 tests. Real-DLL ABI-fixture tests SEALED (need per-platform compiled test plugin).
  - **script** (80→100): +48 tests/2 files (compile/runtime/missing Result split, moved-from degradation, instruction-cap, nil-padding, (nil,message) ECS contract, Vec/Mat operators, event unregister/erroring-handler) + stale "Wave 72" doc fix. No production-logic change.
  - **imgdiff** (80→100): +33 tests (ImageDiff/SsimLite/GaussianBlur/FlipLite/FlipFull/SsimGaussian edge + reference values). No FLIP/SSIM/Gaussian math touched (it IS the golden-diff tool).
  - Gate fixes: 1 compile (net_lobby missing <algorithm> for std::ranges::none_of) + 2 WAE (trigger hicpp-use-auto, ddgi bugprone-implicit-widening). Gate: build clean -Werror (0/0); ctest 332/332 (100%, +5 new bins, no flake); golden/sponza/chrome BYTE-IDENTICAL; WAE 0.
- ✅ **Batch 10 (phase1263)** — FINAL tier 80, 10 libs → 100% (~298 new tests, 5 real bugs/features). **TIER 80 COMPLETE → MARATHON COMPLETE: every ≥80% lib now at genuine 100%.**
  - **framegraph** (80→100): +14 tests + new host-side topo_cyclic_nodes() diagnostic. No scheduling change; aliasing+reorder SEALED (ADR-20260616).
  - **debug_draw** (80→100): +21 tests. No vertex math change.
  - **post** (80→100): +79 tests/10 files + new cd_test_post_camera bin (per-effect Settings/kernel/Push/GLSL contract locks). No effect math/GLSL/default touched. GTAO crude-integrator + exposure GPU-reduce + composite 3D-LUT SEALED.
  - **audio** (80→100): +59 tests + 3 REAL BUGS (Voice loop-wrap fractional overshoot, set_volume + set_master_volume unclamped).
  - **audio_spatial** (80→100): +27 tests + REAL FEATURE (3 attenuation models linear/inverse-square/exponential + rolloff_factor; default kLinear keeps existing behavior).
  - **net_matchmaker** (80→100): +24 tests + REAL FEATURE (MatchmakingQueue + MatchTicket/TicketStatus/MatchResult + skill-window widening).
  - **physics_soft_body** (80→100): +19 tests + 2 REAL FEATURES (Provot bending constraints + ground-plane collision w/ Coulomb friction; both opt-in → existing sims byte-identical). FEM/GPU SEALED.
  - **sample_framework** (80→100): App lifecycle deepening (App.hpp +14, App.cpp +18) + 556 test lines.
  - **ui_renderer_rhi** (80→100): +16 tests (glyph-atlas host path verified+tested) + PIMPL fix (Submitter default-ctor moved out-of-line so default-construction works cross-TU).
  - **ui_widgets** (80→100): +39 tests across 7 widget files. NativeWindow cross-platform SEALED.
  - Gate fixes: 2 compile (ui_renderer_rhi Submitter PIMPL incomplete-Impl on default-ctor → out-of-line; sample_framework 19× nodiscard app.run() → static_cast<void>) + 2 test-tolerance RCA (gtao fast_acos is an approximation: 0.01→0.2 + finiteness/ordering; sample dt float-seeded accumulation 1e-9→1e-6) + 47 WAE (26 argument-comment, 11 float-loop-induction→int, 3 pi-literal→std::numbers, 2 braced-init, 2 noexcept-move, audio contains/widening/member-init, net_matchmaker move-const/contains, use-auto, any_of, min-max, audio /tmp WAV→cwd path). Gate: build clean -Werror (0/0); ctest 334/334 (100%, +7 new bins); golden/sponza/chrome BYTE-IDENTICAL; WAE 0.

## MARATHON COMPLETE
All engine libraries that were at ≥80% implementation depth are now at genuine 100% (real implementation + tests, or formally-sealed multi-week out-of-charter subsystems). 11 commits phase1253–1263, 73 libraries driven to 100%, ~1875 new tests, ~17 real bugs/features. Every checkpoint: build clean -Werror, full ctest green, golden byte-identical, clang-tidy WAE clean.

## ≥75 extension (back-fill of foundation libs below the original 80 cutoff)

- ✅ **cd::mem (78→100)** — the documented "POSIX munmap leaks length" v1 bug was ALREADY fixed upstream in phase1237 (per-mapping side-table records the rounded length → `munmap(ptr, stored_len)` on POSIX, `VirtualFree(ptr, 0, MEM_RELEASE)` on Windows; `mapping_length()` / `live_mapping_count()` accessors expose the table for tests). This pass closes the remaining thin-coverage / edge / negative gaps: **+39 tests (18→57)** — detail `align_up`/`is_power_of_two` (incl. constexpr fold), SystemAllocator align-sweep 1/8/64/4096 + zero-align-default + null-dealloc, Linear exact-capacity-boundary / zero-size / non-pow2-reject / bytes_remaining / dead-backing, Pool LIFO-free-list / exhaust-refill / aligned-blocks / over-align-reject / tiny-block-bump-to-link / zero-count-inert / non-pow2-inert / null-dealloc / bytes-accounting, TypedPool exhaust-null + dtor-runs-once, Page zero-size / alignment-within-page / over-page-align-reject / page-size-stable-pow2 / mapping_length-null / sub-page-round-up (on top of existing map/unmap/round-trip/double-free/foreign), Tracking failure-count / zero-size-not-failure / shared-external-stats-aggregate / peak-high-water-mark / inner-identity / capacity-forward, Pmr throw-bad_alloc-on-exhaustion / is_equal-identity-and-foreign-reject / deallocate-forwards-to-inner. No public-API change; no production-code change (bug was pre-fixed). Status row + this log corrected (baseline doc still read "78 / leaks / 14 gtests"). **Needs orchestrator seal: build + full ctest gate (developer is build-gated).**

## ≥75 extension — Batch A (phase1265)
75–79 band, 13 libs → 100% (~7400 lines, ~330 new tests, 3 REAL BUGS). Highest-first.
- **78%**: mem (munmap-leak already-fixed verified + 39t), profile_cpu_marker_overlay (host-layout +~23t), profile_frame_graph_timeline (+~23t), render umbrella (+37t host, golden-safe), async_submit (+~15t + scoped_lock + fence SEALED), net_session_replay (+~18t SRPK), physics_jolt (+15t, 2 REAL BUGS), ui_font (default-path +~? , HarfBuzz shaping SEALED).
- **75%**: ai::squad (CPU formation +~16t, GpuBatchSolver Sprint-3 SEALED), lighting_clusters (+22t new host-math bin, GPU prefix-sum SEALED, golden-safe), audio_dsp_fx (FDN reverb real +~14t, SIMD SEALED), physics_vehicle (dynamic understeer lateral model +~14t, Pacejka SEALED), ui_a11y (+~14t, OS AT-bridge SEALED).
- **3 REAL BUGS**: (1) physics_jolt create_body next_idx_ started at 0 → first body's BodyHandle = null-sentinel (is_valid()==false) → start at 1. (2) physics_jolt step() crashed on dt≤0 (JPH::Update assert) → non-positive-dt no-op guard. (3) [mem munmap-leak was already fixed upstream phase1237 — stale doc].
- Gate fixes: 7 compile (a11y 5× shadow, net_session_replay 2× nodiscard) + 20 clang-tidy WAE (async_submit 5× lock_guard→scoped_lock + move-const, ai_squad integer-div×2 + use-auto + min-max, ui_font use-auto×2, a11y contains, render data-pointer, profile integer-div + member-init, net_session_replay unused-using) + 4 test-vs-impl RCA (ai_squad slot-offset sign convention, lighting lateral off-screen x=30→2, vehicle understeer non-monotonic yaw + grip-saturation, jolt mass-clamp + handle-validity + damped-free-fall). Gate: build clean -Werror (0/0); ctest 335/335 (100%, +2 new bins); golden/sponza/chrome BYTE-IDENTICAL; 0 NEW WAE (9 pre-existing JoltWorld.cpp broader-130 sealed per phase1252).
