---
name: spec-driven-dev
description: Converts high-level feature requests or engineering prompts into structured, executable micro-specifications (Spec-Driven Development / SDD) with C++23 API contracts, layer ranks, and acceptance test matrices. Automatically triggered when implementing new features, modules, or subsystems.
---

# Spec-Driven Development (SDD) — Executable Micro-Specifications

## Core Principle

Code is a downstream artifact of the Specification. Before implementing a new module, feature, or subsystem, define a compact, unambiguous **Micro-Spec** (`docs/specs/SPEC-<slug>.md`) that establishes the layer boundary, C++23 API contract, and acceptance test matrix.

## The 3-Step SDD Execution Loop

### Step 1: Classify Execution Scope
1. **Vertical Slice (Prototype / Single-Component)**:
   - Scope: Single header/source pair + 1 unit test target.
2. **Production Subsystem (Full Engine Standard)**:
   - Scope: Full CHROMODYNAMIC C++23 contract (`[[nodiscard]]`, `std::expected`, `noexcept`, zero warnings, edge-case + negative GTests, DAG compliance).

### Step 2: Scaffold the Micro-Spec
Run `scaffold_spec.py` to generate a structured specification in `docs/specs/`:

```powershell
python "C:/Users/cemal/.gemini/config/plugins/argon-core/skills/spec-driven-dev/scripts/scaffold_spec.py" --title "<Short Title>" --layer <L0..L6> --module <cd_module_name>
```

Every Micro-Spec contains 5 compact sections (≤ 35 lines total):
1. **Goal & Functional Scope**: 1-sentence measurable engineering outcome.
2. **Layer & Blast Radius**: Target directory (`engine/<domain>/<module>`), DAG layer (`L0`–`L6`), and allowed dependencies.
3. **C++23 API Sketch**: Core types, `std::expected<T, Error>` signatures, and memory ownership (`unique_ptr` / generational handle).
4. **Acceptance Test Matrix**:
   - `[Happy]` Core functional behavior
   - `[Edge]` Boundary / zero / max-capacity input
   - `[Negative]` Invalid handle / malformed input -> deterministic error return
5. **Verification Command**: Exact single-command build + test target.

### Step 3: Autonomous Implementation Against the Spec
- Immediately transition into **Red-Green-Refactor (`tdd-red-green-loop`)** or **Parallel Subagent Fan-Out (`argon-orchestrator`)** to implement and verify the spec in the same turn.
