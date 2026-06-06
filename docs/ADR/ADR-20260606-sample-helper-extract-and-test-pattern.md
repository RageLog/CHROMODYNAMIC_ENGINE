# ADR-20260606-sample-helper-extract-and-test-pattern

Date: 2026-06-06

## Context

Marathon Run 16 closed by adding **3 chrome-Sponza-fix-surface
helpers** (`compute_texture_average_alpha_weighted`,
`fold_texture_avg_into_factor`, `parse_args`) and **1 lock test
without extraction** (`build_pbr_demo_grid`) across phases 822-827.
The pattern that emerged is reusable and worth codifying:

When a non-trivial pure computation lives inside a sample's
implementation TU (typically `samples/engine/<sample>/main.cpp` or a
heavy-include header like `HelloGltf.hpp`), three problems compound:

1. **No coverage net.** Bugs in the math live entirely behind the
   visual outcome — the agent must launch the full sample, capture
   a PNG, and read it back to know whether the function is right.
   This was the original Run 16 cost: 5+ iteration cycles to find
   3 root causes.
2. **No isolated reproduction.** When the outcome IS wrong, the
   debug surface area is "the entire sample" — Vulkan device, asset
   load, frame loop, plus the math. Cannot test the math by itself.
3. **Slow refactor.** Renaming a parameter or tightening a contract
   requires re-running the full sample to confirm no regression.

The cure is **a stdlib-only header per pure helper** + **a matching
gtest binary**. We have shipped this pattern 4 times this session
without an ADR to anchor the convention; future contributors will
copy from whichever existing instance is closest to their task, and
the convention will drift unless documented here.

## Decision

**Every non-trivial pure helper inside a sample's heavy-include TU
SHOULD be extracted into a dedicated stdlib-only header AND covered
by a matching gtest binary registered through
`samples/engine/<sample>/tests/CMakeLists.txt`.**

The pattern has five components.

### 1. Stdlib-only helper header

Filename convention: `samples/engine/<sample>/Hello<Topic>.hpp`.

* `<Topic>` is a short noun phrase (`TextureAverage`, `GoldenCli`,
  `PbrGrid`). Not a sentence; not a verb.
* The header includes **only stdlib + cd::core** primitives — no
  `cd::rhi`, no glTF, no Vulkan, no platform. If the helper needs
  one of those, that is a signal it is **not pure** and should stay
  in the heavy-include TU.
* Functions are `[[nodiscard]] inline` + `noexcept` where the math
  permits.
* The header lives **next to the heavy-include header that
  delegates to it** (not in a separate `inc/` tree) so the
  call-site read pulls them together.

### 2. Pure-vs-singleton split

When the helper needs to integrate with the sample's existing
singleton lifecycle (e.g. `golden::options()`), split:

* `parse_args(argc, argv) -> CliOptions` — **pure**, returns by
  value, zero side effects. Tests use this.
* `options()` + `parse(argc, argv)` — **app-side wrappers** that
  defer to the pure function and write the result into the
  singleton. Behaviour at the call sites is byte-identical to the
  pre-extract path.

This is the canonical CLI-parser flavour of the pattern; the same
split applies to any helper that today reads/writes a process-wide
static.

### 3. Heavy-include TU delegation

The originating heavy-include TU (`HelloGltf.hpp`, `main.cpp`, …)
**includes the new helper header and delegates**:

```cpp
// before (inline math in heavy-include TU):
double sum_r = 0.0; ... double w = ... / 255.0; ...

// after (one line):
range.base_color_factor = fold_texture_avg_into_factor(
    mat.base_color_factor, avg_texture_color);
```

The inline body **vanishes from the call site**. Comments at the
call site point at the helper header and its test:

```cpp
// phase822: delegate to the extracted free function
// (testable via test_hello_engine_tex_avg.cpp).
```

### 4. Matching gtest binary

Filename convention:
`samples/engine/<sample>/tests/test_<sample>_<topic>.cpp`.

CMake registration via `cd_add_test` in
`samples/engine/<sample>/tests/CMakeLists.txt`:

```cmake
# phase822 — texture-average colour helper that drives the chrome-Sponza
# reflection per-prim albedo flow. Helper lives in
# samples/engine/hello_engine/HelloTextureAverage.hpp (stdlib-only) so
# this test can exercise it without the rhi / glTF heavy include graph.
cd_add_test(hello_engine_tex_avg
  SOURCES test_hello_engine_tex_avg.cpp
  DEPS    cd::core
)
```

The DEPS list **stays tiny**. `cd::core` is the floor; `cd::math` if
the helper uses `Vec3f` etc. Any other dep is a red flag — the
helper is no longer pure and the extract did not buy isolation.

### 5. Test taxonomy

Each test binary covers FOUR categories:

* **Edge guards.** Empty input, null input, zero dimensions, zero
  stride, all-zero-alpha. These catch the "main() called us with
  weird state during sanitiser pass" failure mode.
* **Happy path on canonical inputs.** For Sponza-fix helpers this
  is the three curtain colours (red 0.85/0.12/0.08, green, blue) +
  identity factor.
* **Algebraic / contract properties.** Symmetry, monotonicity,
  channel-wise multiplication, alpha pass-through, idempotence.
  These catch refactors that re-implement the math wrong (e.g. swap
  sum and product, drop alpha-weighting).
* **Regression markers.** Phase-tagged tests that lock the fix
  outcome (`kPbrGridZ == 0.0F`, `kMaxGeomsPerInst >= 103`,
  fixture-5 slug is `chrome_probe`). These catch "the developer
  un-fixed the bug while refactoring something else".

## Consequences

### Positive

* **Tight iteration cycles.** A 0-ms gtest binary catches the math
  error before the agent loop is invoked. The Run 16 pattern of
  "5+ iteration cycles + multimodal PNG read" collapses to one
  compile-test cycle for any future bug in the covered surface.
* **Documented behaviour.** Each test case is a sentence:
  `GoldenFixtureNonNumericIsRejected`. New contributors read the
  test file and learn the contract without grepping the
  implementation.
* **Refactor safety.** The 45 chrome-fix-surface tests we ship
  across 4 binaries (tex_avg ×14, golden_cli ×16, pbr_grid ×13, +
  the existing static_assert + 2 fixture-lock tests in
  test_sponza_golden) flatly forbid any refactor that breaks the
  fix outcome.
* **Forward-compatible.** Future visual bugs in the chrome surface
  follow the same template: extract → test → integrate.

### Negative

* **Per-helper line cost.** A 30-line `compute_texture_average_*`
  helper grew to ~70 lines in its own header (license header,
  documentation, namespace boilerplate). Acceptable when the
  helper is non-trivial; for one-line helpers the inline path is
  still right.
* **Two files to keep in sync.** The helper's signature lives in
  the header; the integration call lives in the heavy-include TU.
  A signature change requires editing both. Mitigation: the
  matching test catches the signature drift at compile time.
* **Initial cost.** Extracting a helper today costs ~30 minutes of
  refactor + test authoring. The pattern only pays for itself when
  the helper would otherwise need 2+ debug cycles in its lifetime.
  Trivial helpers (`max(a, b)`) do not benefit.

### Neutral / informational

* The same pattern applies to **any sample** under `samples/`, not
  just `hello_engine`. Future `hello_d3d12_pbr`, `hello_metal`,
  `hello_web` etc. should adopt the same convention so test files
  read consistently across samples.
* `cd_add_test` is the project's standard test registration helper
  (defined in `CMakeModules/`). No new CMake machinery is needed.

## Rejected alternatives

1. **Use a single `samples/engine/test_helpers.hpp` umbrella.**
   Would let multiple samples share helpers. Rejected: most helpers
   are sample-specific (the chrome-probe constants are
   hello_engine's; future samples will not need them). The
   per-helper-header pattern leaves room for a future shared
   umbrella when one is actually warranted.
2. **Keep helpers inline and add Vulkan-headless test infrastructure.**
   Would let tests cover the original heavy-include site. Rejected:
   the infrastructure cost (headless Vulkan device, dummy swapchain,
   asset stubs) is orders of magnitude greater than per-helper
   extraction, and the resulting tests are still slow.
3. **Use clang-tidy `readability-function-cognitive-complexity` to
   force extracts.** Would automate the decision. Rejected: the
   right time to extract is when the helper picks up its second use
   or its first debug cycle; complexity isn't a perfect proxy.
   Convention + ADR > automation here.

## Verification

* Phase 822 (tex_avg) — 10 tests + 1 helper integrated, build clean.
* Phase 823 (fold) — 4 tests + 1 helper integrated, build clean.
* Phase 825-826 (golden_cli) — 16 tests + pure/singleton split
  integrated, build clean.
* Phase 827 (pbr_grid) — 13 tests covering an existing pure helper
  (no extraction needed; the helper was already in `HelloPbrGrid.hpp`).
* All 4 binaries run in < 5 ms each at every `ctest --preset
  ninja-debug`. Tests 245 → 257 across Run 16 + Run 17 strand A.

## References

* [ADR W8-BD](ADR-20260606-W8-BD-per-geom-albedo-SSBO-and-curtain-reflections.md)
  — the chrome-fix chain that motivated the pattern.
* [ADR agent-iteration-loop](ADR-20260606-golden-fixture-agent-iteration-loop.md)
  — the visual-debugging loop these tests complement.
* `samples/engine/hello_engine/HelloTextureAverage.hpp` + matching
  `tests/test_hello_engine_tex_avg.cpp`.
* `samples/engine/hello_engine/HelloGoldenCli.hpp` + matching
  `tests/test_hello_engine_golden_cli.cpp`.
* `samples/engine/hello_engine/HelloPbrGrid.hpp` (no extract; helper
  was already pure) + matching `tests/test_hello_engine_pbr_grid.cpp`.
