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
| 100 | rhi ✅ (already done) · game::quest ✅ · ecs ✅ |
| **92** | frame_timing · game::save |
| **90** | bench · ai_bt · cutscene_player · gluon |
| **88** | concurrency · math · dialogue · input_recorder · l10n · settings · material · shader · debug_line · scene · ui_layout |
| **85** | core · io · log · ai_director · pathfinding · anim_graph · asset_hot_reload · game::camera · dialog_tree · fsm · particles_event · game::query · save_compression · camera(render) · spirv_cross_glue · hdr_display · asset(umbrella) · anim · anim_ik · gameplay_input_binding · gameplay_time · input · net · physics · ui_animation · ui_input · ui_theme · ui_renderer |
| **84** | time |
| **83** | restir_di |
| **82** | diag · events · profile · vfs · brdf · scene_ingest · trigger · net_lobby · world_container · ddgi |
| **80** | config · plugin · script · imgdiff · framegraph · debug_draw · post · audio · audio_spatial · net_matchmaker · physics_soft_body · sample_framework · ui_renderer_rhi · ui_widgets |

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
