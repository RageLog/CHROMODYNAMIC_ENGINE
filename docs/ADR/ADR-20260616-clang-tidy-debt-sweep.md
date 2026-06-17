# ADR-20260616 — clang-tidy WarningsAsErrors test-suite debt sweep (phase1252)

> Iglberger format. Closes the final item of the ALL-MODULES-TO-100 marathon
> (bands 0–7 = phase1228–1251). Sibling of the per-band scope ADRs.

## Bağlam (Context)

The 122-library all-modules-to-100 marathon (bands 0–7) drove every engine library to
terminal-100% (real implementation or promote-on-need seal). The marathon's queued final
item was a **batched clang-tidy WarningsAsErrors (WAE) sweep** of the accumulated
test-suite lint debt — the debt that the prior `build/tidy_scan.sh` tracked across **14
rules**:

```
google-build-using-namespace, readability-isolate-declaration, misc-unused-using-decls,
readability-static-definition-in-anonymous-namespace, modernize-use-ranges,
hicpp-use-auto, modernize-use-auto, performance-inefficient-vector-operation,
cppcoreguidelines-prefer-member-initializer, modernize-use-emplace,
bugprone-implicit-widening-of-multiplication-result, modernize-raw-string-literal,
llvm-namespace-comment
```

A parallel clang-tidy scan over all 321 engine/app test TUs (header-filter = engine
headers) measured **245 diagnostics across 103 files** for those 14 rules — 153 in test
`.cpp`, 92 in shipping headers.

Two important environmental facts shaped execution:

1. **clang-tidy is a SEPARATE pass, not wired into `ninja-debug`.** The build is `-Werror`
   clean and the full test suite passes *with this debt present* — it is cosmetic/idiomatic
   lint, not a correctness or build defect.
2. **`clang-tidy --fix` is gated in this tree.** Diagnostics promoted by `.clang-tidy`
   `WarningsAsErrors` are treated as errors, and `--fix` (even `--fix-errors`) refuses to
   apply fixits to them under LLVM 21 + the MSVC-STL compile flags. So the sweep was done
   by **hand** (surgical Edit), never by blind `--fix` (which also carries the documented
   `std::upper_bound`/ranges corruption risk).

## Karar (Decision)

**Within the defined 14-rule sweep scope: drive to terminal-0** — every one of the 245
diagnostics is either FIXED by a behaviour-preserving hand edit or formally NOLINT-SEALED.
**Beyond that scope: SEAL the broader full-config WAE debt as promote-on-need.**

### 1. 14-rule sweep — DONE (0 remaining)

- **149 FIXED.** A parallel auto-fix pass cleared the trivially-safe rules on 39 test
  `.cpp` files (header-filter = none → zero header writes, golden-safe by construction);
  the residual 149 were hand-fixed by 7 parallel `code-consistency` agents partitioned by
  library group (foundation/asset/render/world/ui/imgdiff+imgui/game+samples), each
  hand-edit-only per a shared `RULES.md` recipe. Breakdown of the fixed set:
  use-ranges (homogeneous `find`/`any_of`/`sort`/`fill`/`remove`/`for_each` → `std::ranges::*`),
  use-auto on cast-init, isolate-declaration splits, implicit-widening `static_cast` of the
  first operand, unused-using removals, `using namespace` → explicit using-declarations,
  use-emplace, namespace-comment, raw-string, prefer-member-init.
- **9 NOLINT-SEALED** (heterogeneous-comparator / braced-init — no clean idiomatic form):
  `modernize-use-ranges` on `std::lower_bound` with a heterogeneous comparator at
  `render/camera/.../CameraPath.hpp:46,63`, `world/anim/.../CurveTrack.hpp:40,56`,
  `world/anim/.../EventTrack.hpp:37`, `ui/editor/.../SelectionSet.hpp:36,45,57`; and
  `modernize-use-emplace` on a braced `std::pair` `push_back` at
  `world/audio/.../Surround.hpp:186`. Each carries a one-line `// NOLINTNEXTLINE(<rule>)`
  rationale at the site.

### 2. Broader full-config WAE debt — SEALED (promote-on-need)

A full-config scan (the real `.clang-tidy`, all ~209 WAE rules) over the same 321 TUs
surfaced **130 additional WAE across 24 rules never in the sweep's scope** (59 in shipping
headers, 71 in test `.cpp`; 59 files). These were never part of the tracked 14-rule debt
and are sealed here as a tracked follow-up:

| Rule | Count | | Rule | Count |
|---|---|---|---|---|
| bugprone-misplaced-widening-cast | 24 | | modernize-use-std-numbers | 4 |
| readability-duplicate-include | 16 | | bugprone-multi-level-implicit-pointer-conversion | 4 |
| modernize-use-scoped-lock | 15 | | modernize-return-braced-init-list | 2 |
| hicpp/performance-move-const-arg | 13 | | (cppcore/modernize)-use-default-member-init | 2 |
| performance-unnecessary-copy-initialization | 9 | | noexcept-move-operations | 2 |
| cert-flp30-c | 9 | | 12 further rules | 1 each |
| readability-use-std-min-max | 7 | | | |
| cppcoreguidelines/hicpp-member-init | 7 | | | |
| readability-container-contains | 6 | | | |

Plus **621 non-WAE advisory warnings** (NOT errors — absent from `WarningsAsErrors`):
`readability-identifier-naming` (372) and `bugprone-unchecked-optional-access` (245)
dominate. These are advisory only and out of scope for any WAE gate.

## Reddedilen alternatifler (Rejected alternatives)

- **Blind `clang-tidy --fix` across the suite.** Rejected: `--fix` is WAE-gated here (no-op
  on the very diagnostics we target) and the ranges auto-fixer is corruption-prone
  (`std::upper_bound` heterogeneous-comparator history; `_Iota_fn` incident below).
- **Hand-fix the broader 130 WAE too, this session.** Rejected: out of the sweep's defined
  scope, pre-existing, behaviour-non-critical (build `-Werror` clean, tests green), and 24
  rules × 59 files of careful per-site work (scoped-lock, member-init, move-const-arg) is
  unwarranted scope-creep with real breakage risk during an unattended run.
- **Revert the whole sweep and seal everything.** Rejected: the 14-rule fixes are real,
  verified-green progress; reverting good work to seal it is strictly worse.

## Sonuçlar (Consequences)

- **14-rule test-suite WAE debt = 0** (149 fixed + 9 sealed). Verified green gate:
  `cmake --build --preset ninja-debug` clean `-Werror` (0 errors / 0 warnings),
  `ctest --preset ninja-debug -E rhi_vulkan` = **317/317 PASS**, and the
  golden/sponza/chrome byte-identical tests pass after the seal rebuild → **no rendering
  regression**. All header edits were behaviour-preserving (use-auto / decl-split /
  homogeneous-ranges-wrap / comment-only NOLINT).
- **Broader 130 WAE (24 rules) + 621 advisory warnings: tracked, promote-on-need.**

### Promote-on-need trigger

When clang-tidy is wired into CI as a **hard gate** (lavapipe/RTX-3080 lane), drive the
remaining 130 WAE to 0 the same way (per-rule, hand-edit, group-partitioned agents), then
decide per-rule whether `readability-identifier-naming` / `bugprone-unchecked-optional-access`
should be promoted into `WarningsAsErrors` or left advisory.

### Lessons (folded into the fix recipe; see `build/wl/RULES.md`)

1. **`std::ranges::iota` needs a `std::weakly_incrementable` seed** — `float` is not one, so
   `std::iota(first,last,0.0F)` must stay classic (NOLINT). Caused the `_Iota_fn` no-match
   compile error in `test_dsp_fx.cpp` (reverted).
2. **`using namespace X;` → using-declarations must enumerate EVERY referenced symbol** —
   a missed one is a hard compile error (`CompressedTexture` in `test_texture_compress.cpp`,
   fixed). Read the whole TU before converting.
3. **`clang-tidy --fix` is WAE/compile-flag-gated here** — hand-edit; never trust blind
   `--fix` for ranges rules.
