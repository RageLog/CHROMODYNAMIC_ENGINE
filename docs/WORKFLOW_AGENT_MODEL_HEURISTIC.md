# Workflow agent — model selection heuristic

Last updated: 2026-05-31 (post-marathon token-cost analysis).

This document is the **policy file** for choosing which Claude model
each subagent should run on inside a Workflow dispatch. Applies to all
future `Workflow({ script: ... })` invocations.

## Why this exists

Empirical session measurements:

* `wf4fqi6kp` — 15 agents, all default-tier (Opus 4.7), used
  **1,553,747 subagent tokens** → ~103K tokens / agent.
* `wf_33c10f28-3cb` running 31 agents at default tier projects to
  ~3.2 M tokens.
* A second parallel workflow (`wnds9wfjz`) was dispatched in the same
  conversation, doubling token burn — stopped mid-flight per the
  budget-aware rule (see §4).

Default-tier Opus is appropriate for **hard reasoning** but wasteful
for **mechanical** work (README writing, ADR-from-spec drafting,
single-file scaffolds, audit reports). The Workflow tool exposes
`opts.model: 'haiku' | 'sonnet' | 'opus'` per `agent()` call; the
orchestrator script SHOULD classify each task by difficulty and pass
the model explicitly.

When omitted, agents inherit the main-loop model (Opus tier) —
expensive default for trivial work.

## 1. Difficulty tiers + recommended model

| Tier | Model | Use for | Typical token cost |
|------|-------|---------|---------------------|
| **Trivial** | `haiku` | README writing, ADR-from-spec drafting, doc-only changes, audit reports, simple test stubs, mechanical file renames, CMakeLists.txt edits | ~10-20K |
| **Easy** | `haiku` or `sonnet` | small library impl (<200 LOC) with clear spec + tests, single-file refactor, locale file authoring | ~25-40K |
| **Medium** | `sonnet` | library impl 200-500 LOC, cross-file refactor 3-5 files, sample integration with existing libs, ADR drafting from first principles | ~60-90K |
| **Hard** | `opus` | root-cause investigation, complex architectural decisions, multi-system integration (5+ files across tiers), shader / pipeline design | ~100-200K |
| **Critical** | `opus` + adversarial verify | user-flagged "must be right first time" items, security-sensitive changes, breaking-change PRs | ~200-400K |

## 2. Quick classifier — pick a model in 30 seconds

Answer these questions; the FIRST `yes` decides:

1. **Pure documentation** (no code edits, only markdown / ADR / README)?
   → `haiku`
2. **Audit report only** (read code, write findings; no code edits)?
   → `haiku`
3. **Single file new** with a spec that says exactly what goes in it
   (locale .kv, CMakeLists target, stub source from a template)?
   → `haiku`
4. **Library impl** with public header AND impl AND tests, but the
   algorithm is documented in spec (e.g. "Reinhard log-avg") and
   total new LOC < ~300?
   → `sonnet`
5. **Library impl** where the algorithm or layout is non-obvious
   (e.g. dock-space, curve editor, Cassowary solver) OR multi-file
   refactor (>5 files) OR sample integration with existing scene
   render path?
   → `sonnet`
6. **Root-cause investigation** of a bug whose mechanism is unknown,
   OR **architectural design** of a new tier / pattern, OR
   **multi-system integration** that touches RHI + render + sample?
   → `opus`
7. **User has explicitly asked for highest-quality output** OR the
   task is a security review / breaking-change audit / a Q1
   investigator role in adversarial diagnose?
   → `opus` + adversarial verify

## 3. Cost-budget heuristics

* Estimate token budget per phase up-front:
  * `n_agents × avg_tokens_for_their_model`
  * Haiku ~15K, Sonnet ~70K, Opus ~150K (observed averages).
* A phase that mixes tiers (e.g. 1 Opus root-cause + 3 Haiku audits)
  is cheaper than 4 Opus auditors AND produces better-targeted
  output (the Opus agent inspects the Haiku findings).
* If the total estimated budget for the workflow exceeds the
  user's stated budget (`budget.total`), use `budget.remaining()`
  to gate later phases — fail fast rather than mid-workflow.

## 4. Hard rules

* **NEVER dispatch two workflows in parallel** in the same session.
  Token cost doubles, file conflicts multiply. If a second workflow
  is needed, wait for the first to complete OR use `TaskStop` on
  the lower-priority one.
* **NEVER mix `agentType: 'Explore'` with `opts.model: 'opus'`** —
  Explore is already a cheap read agent; Opus overrides it back to
  expensive. Use `'Explore'` alone (no model override) for read-only
  investigation.
* **Avoid `parallel()` for genuinely independent batches > 5 agents
  in one wave** — context size at the orchestrator grows linearly
  with parallel-agent count, even at Opus tier. Prefer `pipeline()`
  or split into smaller waves.
* **Single ctest sweep at the end** — not per-agent. Each agent
  runs `cmake --build` on their target only; final agent runs
  full `ctest`. Saves ~25s × n_agents.

## 5. Template — minimum-friction Workflow scaffold

```js
export const meta = {
  name: '...',
  description: '...',
  phases: [{ title: '...' }, ...],
}

// Cheap audit / doc agent — Haiku
agent('write docs/AUDIT/x.md ...', { model: 'haiku' })

// Standard library impl — Sonnet
agent('create cd::game::foo lib with ... tests', { model: 'sonnet' })

// Read-only investigation — Explore (no model override)
agent('READ-ONLY: find all callers of foo', { agentType: 'Explore' })

// Root-cause investigation — Opus
agent('investigate Sponza shadow regression ...', { model: 'opus' })

// Adversarial verify of a hard claim — Opus, multiple voters
parallel(Array.from({length: 3}, () => () =>
  agent('Try to refute: ...', { model: 'opus', schema: VERDICT })))
```

## 6. Runtime adjustability

Workflow scripts can RECEIVE difficulty hints via `args`:

```js
// args = { difficulty_overrides: { 'phase475': 'opus' } }
const m = args?.difficulty_overrides?.['phase475'] ?? 'sonnet'
agent('phase475 ...', { model: m })
```

This lets the dispatcher (the orchestrator main loop) bump up an
agent's model at dispatch time if the previous wave revealed
unexpected complexity — without re-authoring the script.

## 7. When in doubt

**Default to `sonnet`**, NOT to omitting the option. Omitting
inherits the orchestrator's Opus tier — the most expensive default.
Sonnet covers 80% of real workflow tasks at ~30% of the cost.
