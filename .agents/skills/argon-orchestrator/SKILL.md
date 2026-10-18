---
name: argon-orchestrator
description: >-
  Orchestrates Gemini 4 Argon (High) multi-agent workflows with parallel fan-out,
  4-tier model routing (inherit/pro/flash/flash_lite), delta-only context handoffs,
  and dual-auditor reconciliation. Use when coordinating multi-step engineering,
  subsystem design, or Q1 academic pipelines across multiple subagents.
---

# Argon Multi-Agent Orchestrator

Use this skill to decompose complex tasks into a low-latency, token-efficient Directed Acyclic Graph (DAG) of specialized subagents.

## 1. Parallel Fan-Out Protocol

Always launch independent nodes concurrently in a **single `invoke_subagent` tool call** containing multiple entries in `Subagents`.

### Pipeline A — Engine Subsystem Design & ADR Wave
- **Wave 1 (Parallel Discovery)**:
  - `researcher` (`Model: "pro"`): Engineering SOTA (Filament, bgfx, Sokol, The Forge, Diligent, EnTT, Bevy, Jolt).
  - `academic-researcher` (`Model: "pro"`): Peer-reviewed papers (local PDF download + `MANIFEST.csv` + `bibliography.bib`).
  - `analyst` (`Model: "pro"`): Blast-radius and `#include` layer impact map.
- **Wave 2 (Architecture & Planning)**:
  - `architect` (`Model: "inherit"`): `.hpp` public interface + `docs/ADR/ADR-YYYYMMDD-<topic>.md`.
  - `planner` (`Model: "pro"`): Atomic 3–7 node WBS with `[P]` parallel markers.
- **Wave 3 (Parallel Gate & Verification)**:
  - `safety-integration` (`Model: "inherit"`): Concurrency, render-thread, job-system, lifetime & UB audit (BLOCKING).
  - `ethics-integrity-reviewer` (`Model: "pro"`): Third-party license compatibility (MIT/BSD/Apache-2.0 vs LGPL/GPL).

### Pipeline B — Engine Surgical Implementation & Marathon Wave
- **Wave 1 (Test-First / Spec)**:
  - `tester` (`Model: "pro"`): Failing unit/regression test or golden-image test harness.
- **Wave 2 (Parallel Module Implementation)**:
  - `developer` (`Model: "inherit"`, `Workspace: "inherit"` or `"share"`): Surgical C++23 implementation.
  - `build-devops` (`Model: "pro"`): CMake target/preset/vcpkg wiring if build files change.
- **Wave 3 (Parallel Verification & Consistency)**:
  - `code-consistency` (`Model: "flash"`): Mechanical `clang-tidy` / `clang-format` & C++23 attribute checks.
  - `safety-integration` (`Model: "inherit"`): Multithreading/lifetime verification (BLOCKING).
  - `tester` (`Model: "pro"`): Full affected CTest verification via `verify_wave.ps1`.

### Pipeline C — Q1 Academic / Benchmark Whitepaper Wave
- **Wave 1**: `academic-researcher` (`pro`) + `methodology-auditor` (`inherit`, BLOCKING) + `reproducibility-engineer` (`pro`).
- **Wave 2**: `statistical-analyst` (`pro`) + `figure-table-curator` (`flash`) + `latex-writer` (`pro`).
- **Wave 3 (Blind Dual-Verification — Parallel)**:
  - `citation-verifier` (`Model: "flash"`, BLOCKING): Exact local PDF + `MANIFEST.csv` check.
  - `independent-auditor` (`Model: "inherit"`, BLOCKING): Blind second verification using 3 extractors + 3 metadata APIs without reading `citation-verifier`'s report.
- **Wave 4 (Reconciliation & Peer Review)**:
  - Reconcile both audit reports (`PASS + PASS -> ACCEPT`; any `BLOCK -> BLOCK`; conflicting claim verdict -> `CRITICAL DISAGREEMENT` escalate to user), then run `peer-review-simulator` (`inherit`).

## 2. Delta-Only Brief Contract (Token Optimization)

Never paste full source files or long terminal outputs into subagent prompts. Structure every `Prompt` passed to `invoke_subagent` in ≤ 20 lines:

```markdown
- **Hedef**: <1 measurable sentence>
- **Kapsam Dışı**: <explicit exclusions + Iron Rule reminder>
- **Girdi Pointers**: <file:///path#L10-L50 + max 5 bullet summary of prior findings>
- **Done Kriteri**: <exact command or file path + expected verdict>
- **Çıktı Sınırı**: Max 25 satır (Yapıldı | Dosyalar | Kanıt | Verdict | Varsayımlar | Sonraki)
```

## 3. End-to-End Deterministic Pipeline Gate (`tools/argon_pipeline_runner.py`)

Before closing any multi-step engineering or UI/rendering wave, execute the timeout-protected Argon Orchestrator verifier:

```powershell
python tools/argon_pipeline_runner.py --build-dir build/ide-base
```

This single command deterministically verifies:
1. **Layer DAG (`layer_dag`)**: Zero `#include` cycle or upward dependency violations across `engine/`.
2. **Academic Iron-Rule Manifest (`academic_manifest`)**: 100% SHA-256 and `MANIFEST.csv` / `bibliography.bib` integrity.
3. **CMake Build (`cmake_build`)**: Clean compilation of `cd_app_editor` and affected test targets.
4. **CTest Suite (`ctest_suite`)**: Zero test failures across `cd_test_ui_widgets`, `cd_test_submitter_host_state`, `cd_test_world_container`, and `cd_test_brdf_multiscatter`.
5. **Golden Fixture #5 Parity (`golden_fixture_5`)**: Byte-identical `DIFF 0 / 921600 pixels (0.0000%) -- IDENTICAL` ray-traced reflection & PBR parity.
6. **Full 14-Level GPU Visual Capture (`capture_level_*`)**: GPU frame captures across all 14 levels (`Templates 0..13` across all 6 Worlds) with zlib `IDAT` viewport pixel variance verification, writing structured JSON evidence to `build/ide-base/argon_pipeline_report.json`.

