---
name: context-radar
description: Performs high-signal Context Engineering and zero-bloat repository mapping. Extracts C++23 public symbols, header line numbers, and CMake target dependencies in a single compact summary so agents never waste tokens reading entire directories or large files blindly. Use at the start of any discovery, refactoring, or feature task.
---

# Context Radar — Context Engineering & Zero-Bloat Repo Mapping

## Why Context Engineering Matters

In 2026 Agentic Software Engineering, **context window hygiene** is the primary determinant of reasoning accuracy and token cost. Reading 15 whole files to locate 3 class definitions pollutes the attention mechanism ("context rot") and wastes thousands of tokens.

## Golden Rule: Radar First, Slice Second

1. **Radar Scan (1 command)**: Run `repo_map.py` on the target subsystem or symbol query to get exact `file:line` pointers, CMake target names, and public C++23 types (`class`, `struct`, `enum class`, `concept`).
2. **Surgical Slice (`view_file`)**: Read only the 20–60 line range returned by the radar using `StartLine` and `EndLine`.
3. **Delta Handoff**: When delegating to subagents (`developer`, `architect`, `tester`), paste the compact radar output instead of raw file contents.

## Usage

### 1. Map an Engine Domain or Module
```powershell
python "C:/Users/cemal/.gemini/config/plugins/argon-core/skills/context-radar/scripts/repo_map.py" --path engine/render --query "Light"
```

### 2. Map All CMake Targets & Public Headers in a Subsystem
```powershell
python "C:/Users/cemal/.gemini/config/plugins/argon-core/skills/context-radar/scripts/repo_map.py" --path engine/world
```

## Context Pruning Checklist
- **Never** dump full CMake build logs or >100 lines of source code into chat or subagent prompts.
- Prefer `repo_map.py --query <Symbol>` over broad unfiltered directory walks when exploring unfamiliar modules.
