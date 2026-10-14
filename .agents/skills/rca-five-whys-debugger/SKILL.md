---
name: rca-five-whys-debugger
description: Enforces deterministic Root-Cause Analysis (5-Whys) and automated compiler, linker, CTest, ASAN, UBSAN, and TSan failure log parsing. Prevents blind trial-and-error fix loops when builds or tests fail. Use whenever a build fails, a test fails, or a runtime crash/sanitizer error occurs.
---

# RCA Five-Whys Debugger — Deterministic Triage & Root-Cause Protocol

## Anti-Slop Debugging Rule

When a compiler error, linker error, GTest assertion, or ASAN/UBSAN report fires, **guessing fixes in a loop is strictly forbidden**. Blind trial-and-error wastes tokens and introduces secondary regressions.

## The 4-Step Deterministic RCA Loop

### Step 1: Extract the Exact Failure Signal
Pipe or pass the failure log/command output through `parse_failure_log.py` to strip noise and isolate the first primary failure (`file:line:col`, error code, or sanitizer frame):

```powershell
python "C:/Users/cemal/.gemini/config/plugins/argon-core/skills/rca-five-whys-debugger/scripts/parse_failure_log.py" --file <log_path>
```

### Step 2: Inspect the Exact Slice (`view_file`)
Read the 25 lines surrounding the reported `file:line` using `view_file` (`StartLine` / `EndLine`).

### Step 3: Formulate the 3-Line 5-Whys Hypothesis
Before touching any file, state:
1. **Symptom**: What exact compiler error / assertion / sanitizer trap fired?
2. **Root Cause**: Why did the invariant break (lifetime, signed/unsigned conversion, missing include, uninitialized member, race condition)?
3. **Permanent Fix**: Which exact line restores the invariant without suppressing warnings (`NOLINT`, `reinterpret_cast`, or deleting tests are forbidden)?

### Step 4: Escalate After 2 Failed Attempts
If 2 targeted fixes do not resolve the failure, stop editing and invoke the `troubleshooter` (`Model: "inherit"`) subagent with the extracted failure summary.
