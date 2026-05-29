# CHROMODYNAMIC — docs audit (Marathon Run 11 Strand C1)

Status: 2026-05-29, snapshot taken after Run 11 phase 326 (clang-tidy audit) lands.

## Why this audit

User reported docs-side quality drift alongside the code-side warnings. Per CLAUDE.md, ADRs are project memory; missing ADRs = lost decisions. Per Run 10 close-out + Phase 1 mandate, every alt-system decision should have an ADR. This audit catalogues:

1. Outdated phase / commit refs
2. Inconsistent terminology (cd:: vs chroma:: drift)
3. Missing ADRs for shipped W7/W8 decisions
4. Missing per-library README under engine/
5. Inconsistent ADR format (Iglberger compliance check)

## Findings

### Outdated phase refs

- `docs/STATUS_AND_PLAN_W8.md` Section 2 (subsystem audit) was written at end-of-W8 (commit 42e8c32) and reflects W8-BB state. Run 9 (phase 296+ N1 wave), Run 10 (phase 304+ N3..N8), and Run 11 (phase 321+ N9-N12) have shipped extractions. STATUS Section 2 needs the "hello_engine: 7793 satir" line refreshed to the new total.
- `docs/MARATHON_*_SUMMARY.md` (Runs 3 / 4 / 5 / 6 / 7 / 8 / 9 / 10 close-outs) live in `~/.claude/projects/.../memory/project_marathon_run{N}.md` rather than `docs/`; Run 11 should follow that convention (per user MEMORY.md index).

### Terminology drift

- 0 occurrences of `chroma::` in actual code (engine/ + samples/); the namespace is uniformly `cd::` (alias for chromodynamic). CLAUDE.md §1 says "Each library cd::<lib>:: namespace; explicit CHROMA_<LIB>_API macro export" — the namespace pattern is consistent (cd::) but the macro pattern still references "CHROMA". Cross-check.
- README.md (top-level) cross-referenced.

### Missing ADRs (W7 / W8 wave decisions)

The W7/W8 wave (commits ~phase 226 through ~phase 290, Runs 6 / 7) shipped several rendering correctness decisions but did NOT write ADRs as they landed. Backfill candidates (high priority):

1. **W8-AJ LTC corner winding** — area-light Heitz LTC integral broke for the rect-corner case until the projection winding got transposed. Cited in code, no ADR.
2. **W8-AN Karis 2013 representative point** — area-light specular got the Karis MRP fix; the math + cited Karis 2013 paper need an ADR + the paper MANIFEST'd.
3. **W8-BA RT reflection occlusion** — chrome row reflections gained per-instance colour via the W8-BC SSBO; the design (binding 10, instance-index lookup) is wire-format adjacent to ADR-001 (RHI) and ADR-003 (shader pipeline).
4. **W8-BC instance-material SSBO + RT colored reflections** — same family as W8-BA; the W8-BC SSBO is now a permanent fixture of the prim_inst descriptor set.
5. **W8-AR ECS-attribute PBR** — entities flip via SceneEntity.is_pbr to the PBR shader branch. The pattern (one render loop, one shader, one shadow pass; PBR is a per-entity attribute) deserves a small ADR so future libs (decal, particle) follow the same shape.
6. **W8-AY / AZ env-spec gating** — the chrome sphere env-spec gate (mip 0 sample vs prefiltered-spec mip-chain blend) closed the smear vs blur tradeoff. Worth recording the chosen blend weights + tuning history.

### Missing READMEs

`find engine -name README.md` returns ZERO results across 188 CMakeLists.txt. Every cd_<lib> library currently has no per-library doc. The minimum per-library README should carry: purpose, namespace, headers exposed, primary types, usage example, test command. With 97 libraries this is a big lift; prioritise:

- Foundation tier: cd::core, cd::math, cd::log, cd::time, cd::diag, cd::concurrency, cd::frame_timing (7)
- RHI tier: cd::rhi, cd::rhi_vulkan (2)
- Render tier: cd::material, cd::ibl, cd::brdf_ltc, cd::post_composite, cd::render (5)
- Asset tier: cd::asset, cd::asset_gltf, cd::asset_json, cd::asset_ktx2 (4)
- World tier: cd::ecs, cd::scene, cd::audio, cd::net (4)

Total Tier-1 = 22 READMEs. Run 11 will ship a representative subset + the README template + the index README.md under engine/ that points at them.

### ADR format consistency

ADRs land in two date conventions:
- early ones: `ADR-001-rhi-architecture.md` through `ADR-018-phase2-foundation-closure.md` (sequential numbering)
- post-2026-05-22: `ADR-YYYYMMDD-topic.md` (date-stamp)

The mixed scheme is unproblematic but not documented. CLAUDE.md §2 lists "docs/ADR/ADR-YYYYMMDD-konu.md — Iglberger formatI" as the canonical path; sequential ADR-001..018 are legacy.

Iglberger format compliance: spot-check shows most ADRs follow it, but some have inconsistent section names (Context vs Background, Decision vs Choice, Consequences vs Impacts). Not blocking.

## Run 11 actions

Given remaining marathon time:

- Write 3 highest-priority W7/W8 ADRs (W8-AJ LTC corner, W8-AN Karis MRP, W8-AR ECS-attribute PBR).
- Write 5 representative per-library READMEs covering the foundation tier (cd::core, cd::math, cd::concurrency, cd::frame_timing, cd::log).
- Refresh STATUS_AND_PLAN_W8.md Section 2 + Section 6 with Run 9/10/11 state.
- Update CLAUDE.md if Run 11 surfaced patterns worth codifying.

W8-BA / W8-BC / W8-AY / AZ ADRs queued for Run 12.
Remaining 17 Tier-1 READMEs + 75 Tier-2/3 READMEs queued for Run 12.

