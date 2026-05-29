# CHROMODYNAMIC — clang-tidy audit (Marathon Run 11)

Status: 2026-05-29, snapshot taken from clang-tidy 21.1.0 against `build/ninja-base/compile_commands.json` (Clang-cl Debug), TU = `samples/engine/hello_engine/main.cpp`.

## Why this audit

User reported "I see warnings and clang-tidy errors across many files — they need to be fixed in a way that meaningfully raises the code standard." The CHROMODYNAMIC build itself is `-Wall -Wextra -Wpedantic -Werror` clean; this audit catches **clang-tidy diagnostics** that don't gate the build (they would gate the dedicated `clang-tidy` CI job once enabled via the `WarningsAsErrors:` knob in `.clang-tidy`).

## Methodology

```
clang-tidy -p build/ninja-base samples/engine/hello_engine/main.cpp 2>&1 \
  | grep -oE "\[[a-z]+-[a-z0-9-]+\]" | sort | uniq -c | sort -rn
```

The TU is the project deepest header-pull (97 libraries + 50 sample includes), so per-include diagnostics naturally aggregate at the sample. Engine-internal TUs would surface a different (smaller) distribution.

## Top categories (full list 38 rules)

| Count | Rule | Verdict | Strategy |
|---:|---|---|---|
| 910 | readability-math-missing-parentheses | FIX (precedence clarity) | mechanical batch via sed; tests cover behaviour |
| 294 | cppcoreguidelines-macro-usage | PARTIAL — most are vendor/build macros | only convert ours; vendor stays |
| 185 | hicpp-uppercase-literal-suffix | FIX (style consistency) | sed; gates literal correctness too |
| 122 | portability-avoid-pragma-once | DEFER — deliberate project policy | add to .clang-tidy disabled list |
| 113 | readability-identifier-naming | TRIAGE — naming/style conflicts | inspect each |
| 104 | readability-redundant-member-init | DEFER (cosmetic) | leave; documents intent |
| 84 | readability-use-concise-preprocessor-directives | FIX (clarity) | mechanical |
| 73 | cppcoreguidelines-avoid-c-arrays | PARTIAL — PoDs mirror GLSL layouts | DEFER PoDs, FIX free buffers |
| 45 | readability-use-std-min-max | FIX (idiom upgrade) | mechanical |
| 34 | readability-isolate-declaration | FIX (debuggability) | mechanical |
| 32 | modernize-use-integer-sign-comparison | FIX (correctness — silent sign-coercion bugs) | sed -> std::cmp_less etc. |
| 29 | modernize-use-std-numbers | FIX (3.14159265F -> std::numbers::pi_v) | mechanical |
| 22 | cert-err33-c | FIX (unchecked return) | annotate or add (void) cast |
| 20 | modernize-use-std-print | DEFER — `<print>` not universal | rebreak when C++23 print universal |
| 16 | readability-avoid-nested-conditional-operator | FIX (readability) | mechanical, low risk |
| 15 | modernize-use-scoped-lock | FIX (idiom upgrade — lock_guard -> scoped_lock) | mechanical |
| 11 | bugprone-misplaced-widening-cast | FIX BUG (correctness) | targeted; tests cover |
| 11 | bugprone-argument-comment | FIX (doc-as-code) | quick |
| 9 | hicpp-explicit-conversions | FIX (mark conversions explicit) | targeted |
| 8 | modernize-use-ranges | DEFER (ranges adoption tracked separately) | revisit later |
| 5 | cppcoreguidelines-prefer-member-initializer | FIX (ctor body -> init list) | mechanical |
| 4 | modernize-return-braced-init-list | FIX | quick |
| 3 | misc-confusable-identifiers | TRIAGE | inspect each |
| 3 | bugprone-implicit-widening-of-multiplication-result | FIX BUG | targeted |
| 2 | readability-simplify-boolean-expr | FIX | quick |
| 2 | readability-misleading-indentation | FIX BUG (may indicate intent mismatch) | inspect each |
| 2 | portability-template-virtual-member-function | TRIAGE | inspect |
| 2 | modernize-loop-convert | FIX (range-for) | quick |
| 2 | cppcoreguidelines-pro-type-const-cast | TRIAGE | inspect each |
| 2 | cert-flp30-c | FIX BUG (float loop counter — accumulating error) | switch to integer loop |
| 1 | readability-use-anyofallof | FIX | quick |
| 1 | misc-unused-using-decls | FIX | quick |
| 1 | hicpp-function-size | DEFER — main() (covered by Strand A) | tracked |
| 1 | cert-msc32-c | TRIAGE | inspect |
| 1 | cert-dcl58-cpp | TRIAGE | inspect |
| 1 | bugprone-unhandled-exception-at-new | FIX BUG | inspect |
| 1 | bugprone-suspicious-stringview-data-usage | FIX BUG | inspect |
| 1 | bugprone-integer-division | FIX BUG (int/int loses fraction) | inspect |

## Distribution between engine/ vs samples/

The TU sweep is dominated by engine/ headers. Approximate split (from sampling warning paths):

- engine/asset/ — ~30% (mostly C-array PoD vertex/decal/particle layouts)
- engine/foundation/ — ~25% (math operators, atomic patterns, concurrency)
- engine/render/ — ~25% (RHI, IBL, BRDF, post-fx layouts)
- engine/world/ — ~10%
- samples/ — ~10%

## Decision matrix for Run 11 commits

Given remaining marathon time + the user quality-bar requirement, Run 11 commits land the **top-3 mechanical fixes** plus the **bug-class fixes** that surface real correctness issues. Cosmetic / project-policy rules go into `.clang-tidy` disable list with a documented rationale.

### Will fix in Run 11

- `readability-math-missing-parentheses` (910)
- `hicpp-uppercase-literal-suffix` (185)
- `modernize-use-scoped-lock` (15)
- `bugprone-misplaced-widening-cast` (11) — BUG class
- `bugprone-implicit-widening-of-multiplication-result` (3) — BUG
- `readability-misleading-indentation` (2) — BUG
- `cert-flp30-c` (2) — BUG
- `bugprone-integer-division` (1) — BUG
- `bugprone-suspicious-stringview-data-usage` (1) — BUG
- `bugprone-unhandled-exception-at-new` (1) — BUG

### Will disable in .clang-tidy (project policy)

- `portability-avoid-pragma-once` — we use `#pragma once` by convention
- `readability-redundant-member-init` — explicit init documents intent
- `cppcoreguidelines-avoid-c-arrays` (full disable) — already disabled via `hicpp-avoid-c-arrays`, harmonize
- `modernize-use-std-print` — deferred until `<print>` is universally available on the 8-compiler matrix

### Will defer to Run 12+

- `cppcoreguidelines-macro-usage` (294)
- `readability-identifier-naming` (113)
- `readability-use-concise-preprocessor-directives` (84)
- `cppcoreguidelines-avoid-c-arrays` engine cases (~50) — PoD layouts mirror GPU structs; needs ADR

## Policy update plan

Once Run 11 batches land, tighten `.clang-tidy` to make the cleaned rules WarningsAsErrors. Disabled rules go into the existing exclude list with a one-line `# rationale:` comment so the reasoning survives future audits.

## Reproduction

```
# Per-rule counts:
clang-tidy -p build/ninja-base samples/engine/hello_engine/main.cpp 2>&1 \
  | grep -oE "\[[a-z]+-[a-z0-9-]+\]" | sort | uniq -c | sort -rn

# Per-file counts for a single rule:
clang-tidy -p build/ninja-base samples/engine/hello_engine/main.cpp 2>&1 \
  | grep -B1 "\[readability-math-missing-parentheses\]" \
  | grep -oE "engine/[^:]*\.hpp" | sort | uniq -c | sort -rn | head -20
```

