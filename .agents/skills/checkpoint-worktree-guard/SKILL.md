---
name: checkpoint-worktree-guard
description: Manages non-destructive git checkpoints, blast-radius audits, and isolated subagent workspace safety during multi-step refactors and autonomous marathon waves. Automatically triggered before risky refactors, before launching parallel write subagents, or before committing/completing a wave.
---

# Checkpoint & Worktree Guard — Safe State Isolation & Rollback Harness

## Core Principle

High-throughput agentic engineering and parallel multi-agent refactoring require an **instant, zero-risk rollback harness**. Agents must never lose a working state or accidentally introduce unintended file deletions, `.clang-tidy` scalar corruptions, or cross-layer spills.

## Capabilities & Commands

### 1. Blast-Radius & Hygiene Audit (`status`)
Summarizes modified/untracked files, insertions/deletions, touched engine layers, and checks for forbidden patterns (e.g., `#` comments inside `.clang-tidy` `Checks:` scalar):

```powershell
python "C:/Users/cemal/.gemini/config/plugins/argon-core/skills/checkpoint-worktree-guard/scripts/checkpoint_guard.py" status
```

### 2. Non-Destructive Micro-Checkpoint (`snapshot`)
Creates an instant git snapshot commit object (`git stash create`) and stores a lightweight ref under `refs/checkpoints/<label>` **without modifying or resetting your working tree**:

```powershell
python "C:/Users/cemal/.gemini/config/plugins/argon-core/skills/checkpoint-worktree-guard/scripts/checkpoint_guard.py" snapshot --label "pre-wave-refactor"
```

### 3. Parallel Subagent Worktree Protocol
- When launching **multiple concurrent `developer` or `ui-developer` subagents** that edit overlapping subsystems, set `Workspace: "share"` (git worktree semantics) in `invoke_subagent`.
- When launching subagents that edit **disjoint module directories** (`engine/render/<modA>` vs `engine/world/<modB>`), `Workspace: "inherit"` is safe and avoids worktree overhead.
