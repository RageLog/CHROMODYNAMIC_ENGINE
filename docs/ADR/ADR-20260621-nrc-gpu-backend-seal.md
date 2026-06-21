# ADR-20260621 — cd::nrc GPU-backend (CUDA-NN / OneAPI / SPIR-V) Kapsam Mührü (focused seal)

- **Status**: Accepted
- **Date**: 2026-06-21
- **Branch**: dev
- **Deciders**: Cemal TATLI
- **Author**: Developer (cd::nrc 70 → 100 close-out: complete the CPU-tractable
  slice of the Müller NRC recipe + comprehensively test it, then FORMALLY SEAL
  the fully-fused tensor-core GPU backends with Iglberger-format rationale —
  same seal class as cd::restir_gi's deferred-trace seal)
- **Related**:
  - `docs/ADR/ADR-20260616-band6-render-misc-scope.md` §4 (the original honest
    seal of cd::nrc: research-skeleton-v1 SEALED + test hardened). This ADR is
    the **focused, single-topic** restatement of that seal at charter-complete
    (100%), promoted out of the shared five-library band document so the GPU
    backend seal has its own findable, Iglberger-format home — mirroring how
    `docs/ADR/ADR-20260621-restir-gi-deferred-trace-seal.md` promotes the
    cd::restir_gi GI-trace seal out of its shared band document.
  - `docs/PROJECT_COMPLETION_STATUS.md` §5 (cd::nrc row).
  - `engine/render/nrc/include/cd/nrc/Nrc.hpp` (the sealed header — the
    `CpuReferenceMlp` forward/SGD-backward core + the additive CPU-charter
    helpers `query_into` / `encode_input` / `train_batch`).
  - `engine/render/nrc/tests/test_nrc.cpp` (the pinning tests).
- **Scope guard**: This ADR is a scope-decision document only. The sealed item
  is "deferred-by-design" — the GPU NN backends no longer count as an open gap;
  they carry a precise "promote-on-need" exit gate. The close-out pass that
  accompanies this ADR adds ONE additive CPU method (`train_batch`) that the
  header banner already documented as shipped, plus ADD-ONLY tests, and changes
  NO existing `query` / `query_into` / `train_step` / `encode_input` math →
  public API/ABI for those is fixed, the hello_engine NRC live-demo render path
  (which only calls `query` / `train_step`) is untouched → golden
  byte-identical. No out-of-scope library is touched (NOT rhi/, NOT samples/,
  NOT hello_*).

---

## 1. Bağlam (Context)

`cd::nrc` is a header-only (INTERFACE) library — the CPU reference of the
**Neural Radiance Cache** (Müller, Rousselle, Novák, Keller — "Real-time Neural
Radiance Caching for Path Tracing", SIGGRAPH 2021). Its CPU core is REAL and
correct: a single-hidden-layer perceptron

```
h   = ReLU(W_in · x + b_in)      (hidden_width units)
out = W_out · h + b_out          (kOutputDim = 3 RGB radiance, linear)
```

trained by plain SGD on the half-squared-error loss `L = ½·Σ(out − target)²`.
The forward pass and the SGD backward pass (output-layer gradient + ReLU-gated
hidden gradient + chain rule into the input weights) are VERIFIED against the
Müller model and pinned exactly by `tests/test_nrc.cpp`: forward agreement vs an
independent reference forward (asymmetric input, zero-input bias-through-ReLU,
proven ReLU clamp, five widths 1..128), determinism across instances, and a
single-sample overfit-to-~0-loss convergence proof (the strongest evidence the
gradient direction + chain rule are right).

Beyond the per-sample `query` / `train_step` contract the header ships the
CPU-tractable, golden-safe slices of the Müller NRC training recipe:

- `query_into()` — a scratch-reusing inference overload, byte-identical to
  `query`, with zero per-call heap traffic for a hot per-pixel inference loop;
- `encode_input()` — the stateless Müller §3.2 frequency (positional/sinusoidal)
  encoding that lifts a raw 5-D sample (pos.xyz + 2 packed dir/material scalars)
  into the `kInputDim`-wide feature vector the network consumes;
- `train_batch()` — a mini-batch SGD update that accumulates the per-sample
  half-squared-error gradients against a FROZEN weight set and applies the
  MEAN once — exactly what an SGD optimiser sees for a batch, and the shape the
  GPU fully-fused backward will mirror.

**The gap at the close-out baseline.** Two issues sat behind the 70% row.
(a) The header banner already *described* `train_batch()` as a shipped
CPU-charter feature "independently pinned by tests", but the method **did not
exist** in `CpuReferenceMlp` and the three additive helpers
(`query_into` / `encode_input` / `train_batch`) had **no test coverage** — the
documentation over-claimed the implemented surface. (b) The production GPU
accelerators (Tiny CUDA NN / OneAPI MLP / a custom SPIR-V fully-fused compute
MLP) behind the notional `CD_NRC_BACKEND` option were sealed only inside the
shared five-library band-6 document; the multi-month GPU NN deserves its own
findable, Iglberger-format decision (parity with the cd::restir_gi GI-trace
seal promoted the same day).

## 2. Karar (Decision)

**cpu-mlp-v1 is the charter — IMPLEMENT the documented-but-missing
`train_batch` + COMPREHENSIVELY test the additive helpers; the GPU NN backends
are promote-on-need; this ADR is their focused seal.**

- **cpu-mlp-v1 is the charter.** The forward pass + SGD backward + the three
  additive helpers (`query_into`, `encode_input`, `train_batch`) are the
  genuine, reusable, load-bearing CPU core of the Müller NRC. `train_batch` is
  now IMPLEMENTED (it was already documented as shipped) as the mean-gradient
  mini-batch update — a single-element batch is BYTE-IDENTICAL to one
  `train_step`, an empty batch is a no-op, and it reduces the mean L1 error over
  a distinct (feature → target) dataset. All of it is now COMPREHENSIVELY tested
  as fail-on-revert: `query_into` byte-identity + reused-scratch correctness;
  `encode_input` determinism + finiteness + zero-pad + lowest-octave sin/cos
  contract + large-bank clamp + feeds-the-network; `train_batch`
  single-element==`train_step` + empty-no-op + mean-error descent + determinism.
  All deterministic (fixed RNG seed only, no `sleep_for`), CPU-only (no Vulkan
  ICD, runs everywhere on CI).

- **The GPU NN backends are SEALED (promote-on-need).** A usable on-GPU NRC is a
  multi-month subsystem: 16-wide fully-fused tensor-core layers (the Müller
  performance recipe — the whole MLP resident in registers/shared memory across
  a thread-block, no inter-layer global-memory round-trips), the Adam optimiser
  (vs the reference SGD), GPU-side frequency encoding inlined ahead of the first
  layer, per-frame ONLINE training co-scheduled with the path tracer (self-
  training: the network learns from its own longer-path continuations), plus a
  CUDA or SPIR-V toolchain and render-loop integration this INTERFACE header
  cannot host. Tiny CUDA NN is the production-fastest backend for exactly this
  MLP; an OneAPI or a hand-written SPIR-V fully-fused compute MLP are the
  cross-vendor alternatives. NONE of these is tractable to write, test, or run
  on CI hardware today, and there is **no path-tracer consumer** that integrates
  indirect-bounce radiance caching for shipping. Building a partial GPU NN now
  would be untestable dead code that alters no rendered output (no consumer) —
  over-engineering, not progress.

- **This is the focused seal.** Per the project's honest 100% rule (a charter-
  complete library is 100% when its functional-v1 is IMPLEMENTED + tested OR
  formally SEALED by a one-decision ADR with a precise promote-on-need gate),
  `cd::nrc` is charter-complete at 100%: cpu-mlp-v1 implemented (forward + SGD
  backward + batch + scratch-inference + frequency encoding) + comprehensively
  tested, GPU NN backends sealed here with their exit gate. The CPU MLP stays as
  the reference ORACLE against which a promoted GPU backend's tests assert.

## 3. Reddedilen alternatifler (Rejected alternatives)

- **Stand up a partial `CD_NRC_BACKEND=tinycudann` translation unit now.** Needs
  a CUDA toolchain, fully-fused tensor-core kernels, an Adam implementation,
  GPU frequency encoding, and online-training co-scheduling with a path tracer —
  a multi-month subsystem, untestable on the CI matrix, with no consumer. The
  result would be dead code that changes no rendered output. REJECTED —
  cpu-mlp-v1 SEALED, GPU NN promote-on-need.
- **Synthesise a "good enough" GPU MLP** (e.g. a naïve per-layer compute pass
  with global-memory round-trips, no tensor cores, no fusion). That is a
  different, far-slower algorithm that defeats the entire point of NRC (the
  fully-fused resident MLP is what makes per-pixel inference affordable at frame
  rate); it would masquerade as the Müller backend while being unusable.
  REJECTED — the honest seal is correct; a half-backend re-introduces the
  misleading "feature" banner the band-6 pass removed.
- **Leave `train_batch` documented-but-unimplemented.** The header banner
  already claimed it shipped and was tested; leaving it absent keeps a real
  documentation-vs-code lie at the charter surface. REJECTED — implement the
  small, golden-safe, additive CPU method the banner promised and pin it.
- **Leave the seal only as a row footnote / shared band-6 ADR.** The GPU NN is a
  named, multi-month subsystem and a recurring "why isn't NRC on the GPU?"
  question; it deserves a focused, findable Iglberger-format decision (parity
  with the cd::restir_gi GI-trace seal). REJECTED — this ADR is that focused
  home.
- **Change any existing `query` / `train_step` / `query_into` / `encode_input`
  math in this pass.** The point is golden byte-identical for the existing
  contract. REJECTED — this pass is ADD-ONLY (`train_batch` + tests) + the row %
  + this ADR.

## 4. Sonuçlar (Consequences)

- (+) `cd::nrc` reaches the honest terminal 100%: cpu-mlp-v1 implemented
  (forward + SGD backward + mini-batch SGD + scratch-reusing inference +
  frequency encoding) + COMPREHENSIVELY host-tested, GPU NN backends formally
  sealed here with a precise promote-on-need gate.
- (+) The documentation-vs-code gap is closed: `train_batch()` is now a real
  method matching the header banner, and the three additive CPU-charter helpers
  the banner claimed were "independently pinned by tests" actually are.
- (+) The GPU-backend seal now has a focused, single-topic, Iglberger-format
  home (Context / Decision / Rejected-alternatives / Consequences) — the same
  seal CLASS as cd::restir_gi's deferred-trace seal — rather than only a
  status-row footnote inside a shared five-library band document.
- (+) New ADD-ONLY tests pin the real ACTUAL CPU boundaries (query_into
  byte-identity + scratch reuse; encode_input determinism/zero-pad/octave
  contract/clamp/feeds-network; train_batch single-element==train_step +
  empty-no-op + mean-error descent + determinism) as fail-on-revert. They assert
  NOTHING about a GPU backend — only the CPU math + the seal.
- (+) The existing `query` / `train_step` / `query_into` / `encode_input` math
  is BYTE-IDENTICAL → public API/ABI for the existing surface fixed; the
  hello_engine NRC live-demo (which only calls `query` / `train_step`) is
  untouched → golden byte-identical. No out-of-scope library touched.
- (−) This pass does NOT produce the real GPU NRC subsystem; it arrives with a
  real path-tracer indirect-bounce consumer + a separate promote ADR. Accepted:
  the GPU NN is genuine greenfield (multi-month fully-fused tensor-core MLP +
  Adam + GPU encoding + online co-scheduling), and the honest terminal state for
  a charter-complete INTERFACE header is "implement the CPU-tractable charter +
  deepen the real core + seal the multi-month GPU backend with a precise gate."

## 5. Promote-on-need (exit gate)

A real GPU NRC backend promotes when a path tracer actually integrates
indirect-bounce radiance caching for shipping, via a separate ADR. It stands up
a `CD_NRC_BACKEND=tinycudann` (or a custom SPIR-V fully-fused compute MLP)
translation unit providing:

- 16-wide (Müller width) **fully-fused** tensor-core layers — the whole MLP
  resident across a thread-block, no inter-layer global-memory round-trips;
- the **Adam** optimiser (replacing the reference SGD), with the same
  half-squared-error loss;
- GPU-side **frequency encoding** inlined ahead of the first layer (the CPU
  `encode_input` is the reference oracle for its output);
- per-frame **online training** co-scheduled with the path tracer (self-training
  from longer-path continuations) — a `train_batch`-shaped update mirroring the
  CPU mean-gradient mini-batch path;
- a render-graph consumer that queries the cache for the indirect-bounce
  continuation and demodulates the result before queue insert.

The `Config` / `CpuReferenceMlp` `query` / `query_into` / `train_step` /
`train_batch` / `encode_input` API and the `kInputDim` / `kOutputDim` /
`kRawSampleDim` contract stay FIXED so the CPU MLP remains the byte-for-byte
reference oracle the promoted backend's tests assert against.

---

## Varsayımlar

- "100% per lib" interpretation: a charter-complete library is 100% when its
  functional-v1 is IMPLEMENTED + tested OR formally SEALED by a one-decision ADR
  with a precise promote-on-need gate (the project honest rule, ADR-20260616
  §"What 100% MEANS"). cd::nrc's functional charter is the CPU reference MLP
  (forward + SGD backward + batch + scratch-inference + frequency encoding,
  implemented + now comprehensively tested); the real GPU NN is genuine
  multi-month greenfield with no consumer → SEAL is the correct terminal call,
  not padding code.
- The close-out IMPLEMENTS one additive CPU method, `train_batch`, which the
  header banner already documented as shipped and is golden-safe (it touches no
  existing method's math; `query` / `train_step` / `query_into` / `encode_input`
  are byte-identical). The accompanying tests are ADD-ONLY in
  `engine/render/nrc/tests/test_nrc.cpp`. Target `cd_test_nrc` (INTERFACE lib,
  CPU-only, runs everywhere without a GPU/CUDA toolchain).
- Golden byte-identical: this pass changes no existing render-affecting code —
  only the additive `train_batch` method + ADD-ONLY tests + the status row % +
  this ADR. The hello_engine NRC live demo calls only `query` / `train_step` →
  render path unaffected.
- Scope exclusion honoured: all changes under `engine/render/nrc/` (header +
  tests) + `docs/PROJECT_COMPLETION_STATUS.md` (one row) + this `docs/ADR/`
  file. No other library is touched.

## Sonraki

- Promote-on-need only: the real GPU NRC backend (`CD_NRC_BACKEND=tinycudann` or
  a custom SPIR-V fully-fused MLP + Adam + GPU encoding + online co-scheduling +
  a render-graph consumer) arrives with a real path-tracer indirect-bounce
  consumer + a separate promote ADR.
