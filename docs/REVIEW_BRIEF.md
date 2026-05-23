# Independent reviewer briefing — CHROMODYNAMIC v0.25.0

> **For the reviewer:** you are reading this because the project owner
> asked you to be Axis D of the v1.0 maturity gate (see
> [ADR-20260523-wave125](ADR/ADR-20260523-wave125-v1.0-rollback.md)
> and [ADR-20260523-wave133](ADR/ADR-20260523-wave133-phase11-v1.0-maturity.md)).
> The marathon's author wrote the entire surface — including these
> docs — and self-certified everything. Your job is to be the eye that
> wasn't in the room.
>
> Time commitment: ~3-5 hours for a useful pass, ~1-2 days if you want
> to compile + run the samples yourself.
>
> Deliverable: one markdown document (template at the end of this file)
> committed as `docs/REVIEW-<your-name>-YYYYMMDD.md`. PR or issue,
> either is fine.

---

## 1. What you are reviewing

A C++23 cross-platform game engine, library-oriented (55 separately
consumable cd::* libraries), Vulkan-first rendering, with a working
install + `find_package` surface.

You are NOT reviewing the entire codebase. You are reviewing the
**public surface that v1.0 will promise ABI stability for** and the
**engineering invariants** that promise leans on. That's a tractable
subset.

### Concrete tag under review

- Branch: `dev` (or whatever HEAD is when you start)
- Latest SemVer: `v0.25.0` — the honest baseline after a v1.0 tag
  was cut prematurely and rolled back.

## 2. Mandatory read list (~1 hour)

Read these in order. Take notes as you go.

| # | File | Why |
|---|---|---|
| 1 | [Readme.md](../Readme.md) | top-level pitch, status, latest tag, supported toolchains |
| 2 | [ARCHITECTURE.md](ARCHITECTURE.md) | stack, dependency DAG, cross-cutting conventions |
| 3 | [LIBRARIES.md](LIBRARIES.md) | per-library catalogue (target / namespace / responsibility) |
| 4 | [ADR/ADR-20260523-wave125-v1.0-rollback.md](ADR/ADR-20260523-wave125-v1.0-rollback.md) | what the v1.0 maturity gate IS and why |
| 5 | [ADR/ADR-20260523-wave132-v0.25.0-baseline.md](ADR/ADR-20260523-wave132-v0.25.0-baseline.md) | what v0.25.0 contains + which axes it meets |
| 6 | [ADR/ADR-20260523-wave133-phase11-v1.0-maturity.md](ADR/ADR-20260523-wave133-phase11-v1.0-maturity.md) | the 4-track Phase 11 plan |
| 7 | [CONSUMING.md](CONSUMING.md) | how downstream projects link this engine |

The above is the prose-level pitch. Next is the actual code surface.

## 3. Public header read list (~1-2 hours)

These are the headers the v1.0 ABI promise binds. Read them top-to-
bottom — don't just skim — and form an opinion on:

- Do the names mean what they say?
- Does the doc comment promise what the code does?
- Is the lifetime / ownership story clear from the header alone?
- Are there public-but-experimental APIs that should NOT be in the
  frozen list?

### Foundation tier

| Library | Header | Pages |
|---|---|---|
| `cd::core` | [Engine/foundation/core/include/cd/core/ErrorCode.hpp](../Engine/foundation/core/include/cd/core/ErrorCode.hpp) | typed error code + owning-message back-store |
| | [Engine/foundation/core/include/cd/core/ErrorFormat.hpp](../Engine/foundation/core/include/cd/core/ErrorFormat.hpp) | pretty-print registry |
| | [Engine/foundation/core/include/cd/core/Result.hpp](../Engine/foundation/core/include/cd/core/Result.hpp) | `std::expected<T, ErrorCode>` alias + fail() helpers |
| | [Engine/foundation/core/include/cd/core/Handle.hpp](../Engine/foundation/core/include/cd/core/Handle.hpp) | generational handle pattern |
| | [Engine/foundation/core/include/cd/core/HandleStore.hpp](../Engine/foundation/core/include/cd/core/HandleStore.hpp) | sparse-set slot map backing the handle |
| | [Engine/foundation/core/include/cd/core/CVar.hpp](../Engine/foundation/core/include/cd/core/CVar.hpp) | typed config-var registry |
| | [Engine/foundation/core/include/cd/core/Version.hpp](../Engine/foundation/core/include/cd/core/Version.hpp) | `kEngineVersion` (stamped from PROJECT_VERSION) |
| `cd::diag` | [Engine/foundation/diag/include/cd/diag/Assert.hpp](../Engine/foundation/diag/include/cd/diag/Assert.hpp) | CD_ASSERT / CD_VERIFY / CD_PANIC + override hook |
| `cd::log` | [Engine/foundation/log/include/cd/log/ILogger.hpp](../Engine/foundation/log/include/cd/log/ILogger.hpp) | logger interface |
| | [Engine/foundation/log/include/cd/log/RingBufferSink.hpp](../Engine/foundation/log/include/cd/log/RingBufferSink.hpp) | bounded triage mirror |
| | [Engine/foundation/log/include/cd/log/PanicDump.hpp](../Engine/foundation/log/include/cd/log/PanicDump.hpp) | diag → log bridge |

That's ~10 headers, ~700 lines of declarations. Read them.

### Side libraries to spot-check (skim, don't deep-dive)

| Library | Why spot-check |
|---|---|
| `cd::imgdiff` | exposed by golden gate — what does the consumer have to know? |
| `cd::asset_image` | only stable image API; recently extended (`write_png_rgba`) |
| `cd::rhi` ICommandBuffer | the just-extended `copy_image_to_buffer` (Wave 134) |

## 4. Questions we'd like you to answer

You don't have to answer all of these. Aim for the 5-10 that
generate the strongest opinions — those are the most useful signal.
We labeled each with the axis it informs.

### About the public surface (Axis D)

1. **Names.** Pick three names from the foundation public surface.
   For each: would you guess what it does from the name alone, or
   does the doc comment surprise you?
2. **Error story.** `ErrorCode` carries domain + code + (optionally
   owning) message. Is the `make_owning` / `rewrap` distinction
   clear? Would you have arrived at it without reading
   [ADR-wave125](ADR/ADR-20260523-wave125-v1.0-rollback.md)?
3. **Handle lifetime.** `cd::core::Handle<Tag>` is generational. Is
   the contract for "stale handle" obvious from the header? Would a
   downstream consumer who doesn't read the ADRs get tripped up?
4. **noexcept honesty.** Look at `cd::diag::panic` — it's
   intentionally NOT noexcept. The rationale is in the header. Do
   you buy it?

### About the architecture (Axis D)

5. **DAG sanity.** Read the dependency diagram in
   [ARCHITECTURE.md §2](ARCHITECTURE.md). Does any edge look wrong
   (a tier depending on something above it)? Spot-check with
   `grep -rln "cd::<above-tier>" Engine/<below-tier>/`.
6. **Frozen vs. experimental.** ADR-wave125 lists which libraries
   the v1.0 promise binds. Read that list against
   [LIBRARIES.md](LIBRARIES.md). Anything you'd want to *move*
   between frozen and experimental?
7. **Layer breaks.** ARCHITECTURE.md §3 lists the cross-cutting
   conventions. Pick one (e.g. "Result on every fallible boundary,
   no exceptions"). Find a place the codebase actually breaks it
   (search is fair game). Is that a documented exception or a real
   violation?

### About the v1.0 gate itself (Axis D)

8. **Is the 4-axis gate (A: visual, B: cross-GPU, C: downstream,
   D: you) the right gate?** Anything missing? Anything pointless?
9. **Track A specifically:** the golden-image gate is per-driver-
   tied. Is "per-vendor golden sets" a reasonable v1.0 commitment,
   or does it block a tag indefinitely?
10. **Track C specifically:** the install tree excludes 7 libraries.
    Should v1.0 ship without `cd::rhi_vulkan` and `cd::shader`
    `find_package`-able, or is that a v1.0 blocker?

### Adversarial prompts (optional, highest signal)

11. Build something against the v0.25.0 install tree using just the
    `cd::core` + `cd::diag` + `cd::log` surface. Did you hit
    anything not covered by [CONSUMING.md](CONSUMING.md)? If yes,
    that's a docs gap worth flagging.
12. If you had to ship a real game on this engine tomorrow, what's
    the first thing you'd refuse without?

## 5. What you do NOT need to do

To keep the review tractable, you can skip:

- Reading individual .cpp implementation files (read headers; trust
  the tests).
- Reviewing the marathon-period ADRs (waves 1-122). The relevant
  ones for this review are 125, 132, 133.
- Performance / benchmark validation. `cd::bench` exists; Phase 11
  Track A locks the visual gate, not perf gates.
- Running the samples yourself unless question 11 calls for it.
  Visual signal-of-correctness is the human reviewer's job (project
  owner) on the NVIDIA box; a third visual review wouldn't add
  cross-coverage on Axis A.

## 6. Output template

Save as `docs/REVIEW-<your-handle>-YYYYMMDD.md`:

```markdown
# Independent review — CHROMODYNAMIC v0.25.0

- Reviewer: <your name + a one-line context: "10y C++ engine dev",
  "first-time CMake user", "graphics PhD", etc.>
- Date: YYYY-MM-DD
- Time invested: <N hours>
- Read list completed: <yes / partial — which items skipped>

## What I think is solid

- <thing 1, with file:line reference>
- <thing 2>
- ...

## What I think is wrong (blocking for v1.0)

- <issue 1>
  - **Where:** <file:line or area>
  - **Why blocking:** <one sentence>
  - **Suggested fix:** <one sentence — optional>
- ...

## What I think is unclear / smells (non-blocking)

- <observation 1>
- ...

## Answers to the §4 questions

> Only answer the ones you have opinions on. Skipping is fine.

### 1. Names
…

### 2. Error story
…

### 3. Handle lifetime
…

(etc.)

## Overall

| Axis | Did v0.25.0 close it for you? |
|---|---|
| A — Visual correctness | <yes / partial / no> |
| B — Cross-vendor GPU | <yes / partial / no> |
| C — External downstream | <yes / partial / no> |
| D — Independent review | (you're answering this question) |

**Would you sign off on a v1.0 tag today?** <yes / no / with these
N changes>

**If no, the smallest concrete list that would change your mind:**
1. ...
2. ...
```

That's it. Don't agonize over completeness — the value is in your
actual opinions, not the volume of words.

## 7. How to start the review tactically

If you want a 10-minute first slice instead of committing to a full
pass:

```bash
git clone https://github.com/RageLog/CHROMODYNAMIC_ENGINE.git
cd CHROMODYNAMIC_ENGINE
git checkout v0.25.0   # the tag under review
# Read in this order, ~10 minutes:
less Readme.md
less docs/ARCHITECTURE.md
less Engine/foundation/core/include/cd/core/Result.hpp
less Engine/foundation/diag/include/cd/diag/Assert.hpp
```

That puts you in the right headspace to know whether you want to
commit to the full read list.

## 8. Contact

Open an issue or DM the project owner. Async is fine. Don't worry
about being too critical — adversarial review is the entire point of
Axis D. The marathon's author is going to thank you for finding
sharp edges, not push back.
