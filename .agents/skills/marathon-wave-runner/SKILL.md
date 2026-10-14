---
name: marathon-wave-runner
description: >-
  Executes high-throughput, long-horizon autonomous implementation waves ("marathon" runs)
  with single-command CMake build/test verification, micro-checkpointing, and anti-premature-stop
  continuation. Use when running multi-phase coding sprints, wave-by-wave backlog execution,
  or verifying C++23 engine builds and tests rapidly.
---

# Marathon Wave Runner (High-Throughput Continuous Execution)

Use this skill to maximize continuous autonomous output duration and minimize per-wave latency during multi-step implementation runs.

## 1. Wave Execution Loop (No Premature Stopping)

When executing a multi-item backlog or marathon stretch:
1. **Batch by Wave (2–4 independent items per wave)**: Group non-conflicting libraries/modules into a single wave.
2. **Surgical Edit First, Build Once Per Wave**: Apply all header/implementation/test edits for the current wave before triggering the build, rather than running `cmake --build` after every single line edit.
3. **Single-Command Verification**: Run [verify_wave.ps1](./scripts/verify_wave.ps1) to execute incremental Ninja build + targeted CTest in a single shell invocation with compact tail output (saves 50–70% shell latency and prevents log flooding).
4. **Continuous Wave Progression**: When a wave passes verification (`BUILD_OK` + `TEST_OK`), immediately record the wave status and proceed to the next wave in the user's requested scope without unnecessary conversational pauses.

## 2. Fast Verification Script

Run the bundled PowerShell helper from the repository root:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File "C:\Users\cemal\.gemini\config\plugins\argon-core\skills\marathon-wave-runner\scripts\verify_wave.ps1" -Preset "ninja-debug" -TestRegex "cd_test_.*" -Target ""
```

### Parameters
- `-Preset`: CMake preset name (default: `ninja-debug`).
- `-Target`: Optional specific CMake target to build fast (e.g. `cd_test_foundation`). Leave empty for full preset build.
- `-TestRegex`: Regex passed to `ctest -R` (default: runs all tests if omitted).
- `-MaxLines`: Maximum output lines returned per stage (default: `40`, preventing context window pollution).
