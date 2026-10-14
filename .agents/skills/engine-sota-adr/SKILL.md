---
name: engine-sota-adr
description: >-
  Validates CHROMODYNAMIC C++23 modular game engine architecture, library boundary DAG
  invariants, and Iglberger-format Architecture Decision Records (docs/ADR/). Use when
  designing engine headers (.hpp), auditing #include layer violations, or authoring ADRs.
---

# CHROMODYNAMIC Engine Architecture & ADR Skill

## 1. Layered Dependency DAG Invariant

Every engine subsystem resides in `engine/<domain>/<lib>/` and exports headers under `chroma::<lib>::` (or `cd::<lib>::`). Circular dependencies and upward layer includes are strictly forbidden:

```text
Level 0: foundation (core, math, memory, platform)
Level 1: concurrency, io
Level 2: rhi, asset, audio, physics, net, script, texture_synth, imgdiff
Level 3: render, world, ui, imgui_backend
Level 4: runtime, game
Level 5: editor / apps / samples
```

## 2. Fast Layer DAG Audit Script

Run the bundled Python script to verify that no lower-tier engine directory includes headers from a higher-tier directory:

```powershell
python "C:\Users\cemal\.gemini\config\plugins\argon-core\skills\engine-sota-adr\scripts\check_layer_dag.py" --engine-root "engine"
```

## 3. ADR Template (Iglberger Format — `docs/ADR/ADR-YYYYMMDD-<topic>.md`)

```markdown
# ADR-YYYYMMDD-<topic>: <Title>

- **Status**: Proposed | Accepted | Superseded
- **Date**: YYYY-MM-DD
- **Subsystems**: `engine/<domain>/<lib>`

## 1. Bağlam (Context & Problem Statement)
- Engineering SOTA comparison (Filament, bgfx, Sokol, The Forge, Diligent, EnTT, Bevy, Flecs, Jolt) + verified academic references (`\cite{bibkey}` from `MANIFEST.csv`).

## 2. Karar (Decision & Public C++23 Contract)
- Memory ownership (`std::unique_ptr`, handle/opaque ID on hot-path), error boundary (`std::expected`), thread-safety/job-system contract, `[[nodiscard]]` / `noexcept` API shape.

## 3. Reddedilen Alternatifler (Rejected Alternatives)
- Alternative A — why rejected (latency / memory / coupling / license).
- Alternative B — why rejected.

## 4. Sonuçlar (Consequences & Verification Plan)
- Affected CMake targets, unit tests (`cd_test_*`), and golden-image / sanitizer invariants.
```
