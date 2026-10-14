---
name: tdd-red-green-loop
description: Executes rapid Red-Green-Refactor TDD micro-cycles for agentic software engineering. Builds and runs only the targeted CMake test binary or GTest filter in 1-3 seconds instead of running the full suite on every edit. Automatically triggered whenever implementing a new class, fixing a bug, or iterating on feature logic.
---

# TDD Red-Green-Loop — Agentic Test-Driven Development Harness

## Core Principle

In Agentic Software Engineering, **tests are the executable specification**. Writing or updating a focused GTest *before* or *alongside* the implementation gives the agent an instant, deterministic 2-second feedback signal (`RED -> GREEN -> REFACTOR`) and prevents silent regressions.

## The 3-Step Micro-Cycle

1. **RED (Specify via Test)**:
   - Write or extend the unit test in `engine/<domain>/<module>/tests/test_<module>.cpp` covering:
     - Happy path
     - Edge/boundary case (`0`, `empty`, `max`)
     - Negative case (invalid handle/input -> `std::unexpected` / safe error)
2. **GREEN (Minimal Surgical Implementation)**:
   - Implement the minimal C++23 logic in `include/` and `src/` to make the targeted test compile and pass.
   - Run **only** the targeted test binary via `run_tdd_cycle.ps1` (takes ~1–3 seconds, zero log bloat):
     ```powershell
     powershell -ExecutionPolicy Bypass -File "C:/Users/cemal/.gemini/config/plugins/argon-core/skills/tdd-red-green-loop/scripts/run_tdd_cycle.ps1" -Target <test_target_name> -Filter "<GTestFilter>"
     ```
3. **REFACTOR (Zero-Warning Polish)**:
   - Enforce `[[nodiscard]]`, `const`, `noexcept`, `override`, and zero magic numbers while keeping the test green.
   - Once the module is complete, run full wave verification (`verify_wave.ps1`) before closing the wave.

## Hard Rules
- **No `std::this_thread::sleep_for`** in tests — always use deterministic ticks, latches, barriers, or condition variables.
- **Never weaken or delete an existing assertion** just to make a test pass; fix the underlying implementation.
