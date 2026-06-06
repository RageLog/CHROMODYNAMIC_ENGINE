# CHROMODYNAMIC — Architecture Decision Records

Bu dizin CHROMODYNAMIC Engine için **kalıcı mimari kararları** Iglberger formatında (Bağlam / Karar / Reddedilen / Sonuçlar) saklar.

## Phase 1 — Tasarım ADR'ları (2026-05)

| # | Başlık | Sprint | Status |
| --- | --- | --- | --- |
| **001** | [RHI Architecture](ADR-001-rhi-architecture.md) | S1 | ✅ Accepted |
| **002** | [Renderer Architecture](ADR-002-renderer-architecture.md) | S2 | ✅ Accepted |
| **003** | [Shader & Material Pipeline](ADR-003-shader-material-pipeline.md) | S3 | ✅ Accepted |
| **004** | [ECS + Scene Architecture](ADR-004-ecs-scene-architecture.md) | S4 | ✅ Accepted |
| **005** | [Foundation Policy Bundle](ADR-005-foundation-policy-bundle.md) | S5 | ✅ Accepted |
| **006** | [Asset Pipeline](ADR-006-asset-pipeline.md) | S6 | ✅ Accepted |
| **007** | [Audio Architecture](ADR-007-audio-architecture.md) | S7 | ✅ Accepted |
| **008** | [Physics Architecture](ADR-008-physics-architecture.md) | S8 | ✅ Accepted |
| **009** | [UI Architecture (Dual System)](ADR-009-ui-architecture.md) | S9 | ✅ Accepted |
| **010** | [Animation System](ADR-010-animation-system.md) | S10 | ✅ Accepted |
| **011** | [Networking Architecture](ADR-011-networking-architecture.md) | S11 | ✅ Accepted |
| **012** | [Editor Mechanics](ADR-012-editor-mechanics.md) | S12 | ✅ Accepted |
| **013** | [Localization, Telemetry & Crash](ADR-013-i18n-telemetry-crash.md) | S13 | ✅ Accepted |
| **014** | [CI/CD, Build Cache & Distribution](ADR-014-ci-cd-distribution.md) | S14 | ✅ Accepted |
| **015** | [Concurrency, Job System & SIMD](ADR-015-concurrency-job-system.md) | S5b | ✅ Accepted |
| **016** | [Vendor Matrix & Replace-Ready Policy](ADR-016-vendor-matrix-replace-policy.md) | normative | ✅ Accepted |
| **017** | [DtForHil Pattern Salvage](ADR-017-dtforhil-pattern-salvage.md) | normative | ✅ Accepted |

## Phase 2+ ADR'ları (gelecek)

Aşağıdaki konular henüz ADR olarak yazılmadı; Phase 1 sonrası beklemede:

- **VR/XR** (T25.Q1=B iskelet hazır) — Phase 2-3
- **AI/ML inference** (T25.Q2 araştır) — Phase 3+
- **Game AI** (T25.Q3=D, BT+UAI+GOAP+FSM) — Phase 2-3
- **Procedural content** (T25.Q4=A+B, noise+spline+L-system+WFC) — Phase 2-3
- **Modding pipeline** (T25.Q5=A first-class) — Phase 2-3
- **Scripting host** (T23, Lua→Python→C#) — Phase 2
- **Visual scripting / Blueprint** (T23.Q5 sadece tasarım Phase 1) — Phase 3-4
- **Cheat mitigation** (Bernier-style lag compensation) — Phase 4

## ADR Yazım Standartları

- **Format**: Iglberger (Bağlam / Karar / Reddedilen alternatifler / Sonuçlar).
- **Demir Kural**: Akademik atıfta `research/library/MANIFEST.csv` doğrulanmış PDF + `bibliography.bib` BibTeX olmadan atıf yok. STUB ile işaretlenenler `academic-researcher` ajan tarafından PDF indirimi sonrası finalize edilir.
- **Replace-Ready disiplin**: Her vendor ADR'da adapter pattern + namespace ayrımı (`cd::<lib>::vendor::<name>` vs `cd::<lib>::native::<our>`) zorunlu.
- **Cross-cutting**: ADR'lar birbirine referans verir; bağımlılık DAG ile takip edilir (bkz. `docs/DESIGN.md`).

## Bağlantı

- **Master Design Document**: [docs/DESIGN.md](../DESIGN.md) — Tüm ADR'ların entegre engine vizyonu
- **Implementation Plan**: [docs/PLAN.md](../PLAN.md) — Phase 2+ adım adım WBS + milestone
- **CLAUDE.md** (proje kuralı): [/CLAUDE.md](../../CLAUDE.md)
- **Subagent Suite**: [.claude/agents/](../../.claude/agents/) — 33 ajan

## Implementation-status ADRs

| Date | Subject | Trigger |
| --- | --- | --- |
| 2026-05-28 | [Job System Implementation Status & Header-Inline Pattern](ADR-20260528-job-system-design.md) | W8/phase283 - close STATUS_AND_PLAN_W8.md X1 BLOCKER with audit + test gap closure |
| 2026-05-29 | [W8-AJ — LTC Corner Winding](ADR-20260529-W8-AJ-LTC-corner-winding.md) | LTC area light corner-case fix audit |
| 2026-05-29 | [W8-AN — Karis MRP](ADR-20260529-W8-AN-Karis-MRP.md) | Microfacet roughness pre-multiply (Karis 2013) |
| 2026-05-29 | [W8-AR — ECS-attribute PBR](ADR-20260529-W8-AR-ECS-attribute-PBR.md) | per-entity material override path |
| 2026-05-29 | [W8-AY — Bake-budget revert](ADR-20260529-W8-AY-bake-budget-revert.md) | IBL bake-rules policy rollback rationale |
| 2026-05-29 | [W8-AZ — Env-spec sun gate](ADR-20260529-W8-AZ-env-spec-sun-gate.md) | sun-direction gate on IBL specular |
| 2026-05-29 | [W8-BA — RT reflection occlusion](ADR-20260529-W8-BA-RT-reflection-occlusion.md) | chrome silhouette-against-sky shadow path |
| 2026-05-29 | [W8-BC — Per-instance albedo SSBO](ADR-20260529-W8-BC-per-instance-albedo-SSBO.md) | per-instance hit-colour SSBO (Option B from W8-BA) |
| 2026-06-06 | [W8-BD — Per-(instance, geom) SSBO + curtain reflections](ADR-20260606-W8-BD-per-geom-albedo-SSBO-and-curtain-reflections.md) | per-prim 2D SSBO + `kMaxGeomsPerInst 32 -> 128` + texture-avg colour flow + grid relocation (Run 16 chrome-Sponza fix chain) |
| 2026-06-06 | [Golden-fixture agent iteration loop](ADR-20260606-golden-fixture-agent-iteration-loop.md) | methodology ADR for closed-loop agent-driven visual debugging (Run 16 retrospective) |
| 2026-06-06 | [Sample helper extract-and-test pattern](ADR-20260606-sample-helper-extract-and-test-pattern.md) | methodology ADR codifying the helper-extraction + matching gtest pattern shipped 4× across phases 822-827 |
| 2026-06-06 | [W8-BE — RT texture sampling + bindless](ADR-20260606-W8-BE-rt-texture-sampling-bindless.md) | engine-wide bindless texture infrastructure + ray-side UV interp + per-prim texture sampling so chrome reflections show real Sponza textures (Run 18 strand, multi-day) |
