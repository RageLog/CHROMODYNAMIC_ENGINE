# ADR-20260620: cd::shader Slang backend — gate-stub SEAL

## Status

**SEALED (out-of-charter, gate-stub retained)** — 2026-06-20.
ROADMAP_80_TO_100 batch (cd::shader 88% → 100%).

This ADR is the explicit one-paragraph seal that the 100% mandate
(`docs/ROADMAP_80_TO_100.md` §"Definition of 100%") requires for a
genuinely multi-week, out-of-charter subsystem left as a documented
gate rather than implemented.

## Context

`cd::shader` exposes a backend-agnostic `ICompiler` surface with a
`make_slang_compiler()` factory next to the production
`make_glslang_compiler()`. The Slang path ships as
`src/SlangCompilerStub.cpp` — the factory returns `nullptr` so every
consumer branches on availability with no code change. The build flag
`CD_ENABLE_SLANG` (default OFF) only defines `CD_SHADER_HAVE_SLANG`; it
does NOT swap in a real backend (CMake `message(WARNING ...)` makes
this explicit).

The architecture decision that Slang is a **Phase-3 candidate, not a
current adoption** was already taken in
`ADR-20260612-shader-library-architecture.md` (§1 "Slang … bugün
adopte etmek taze X4-A GLSL-master kararıyla çatışır → Phase-3 adayı";
§3 "Slang adopsiyonu (şimdi): GLSL korpusunun yeniden yazımı + taze
X4-A kararının revizyonu"). What that ADR did not carry is an explicit
*seal* tying the stub to the 100%-completion accounting. This ADR
supplies it.

## Decision

The Slang backend stays a **documented gate-stub** and is **sealed as
out-of-charter** for the cd::shader 100% milestone. cd::shader is
considered 100% at the GLSL/HLSL→SPIR-V production surface (glslang
backend + disk cache + include resolution + hot-reload watcher), with
Slang explicitly excluded by charter and recorded here.

Rationale (one paragraph): a real Slang backend requires FetchContent
of `github.com/shader-slang/slang` plus linking the generated
`slang` / `slang-rhi` static libraries (a deep vendor build tree), a
`SlangCompiler` class wrapping `createGlobalSession()` +
`IModule::loadModuleFromSource()` + `link()` + `getTargetCode()`, and —
to deliver any user value — a rewrite of the GLSL module corpus into
Slang modules, which directly contradicts the still-current X4-A
GLSL-master toolchain decision. That is a multi-week,
cross-subsystem workstream, not a gap closure; implementing it inside
the ≥80→100 sweep would change the engine's shader-authoring contract,
not merely finish cd::shader. The factory + interface already exist so
the future backend swaps in without touching any consumer.

## Consequences

- `make_slang_compiler()` returns `nullptr`; the contract is locked by
  `test_shader.cpp::SlangFactoryHonorsBuildToggle` and (new this batch)
  the explicit `kGlsl`/`kHlsl`-rejection contract is documented in the
  header so consumers know the future backend's GLSL stance.
- cd::shader's 100% claim covers the glslang production path only; the
  Slang line in `docs/PROJECT_COMPLETION_STATUS.md` §4 is updated to
  note this seal.
- No SPIR-V output changes (the stub emits nothing). Golden / sponza /
  chrome are unaffected by this seal.
- Phase-3 re-evaluation trigger is unchanged from
  ADR-20260612 §"Sonuçlar": adopt Slang only if complex
  specialization needs outgrow the GLSL-preprocessor composition path.

## Rejected alternatives

- **Implement the real Slang backend in this batch.** Rejected:
  multi-week vendor integration + GLSL-corpus rewrite, contradicts the
  current X4-A GLSL-master decision; out of the gap-closure charter.
- **Delete the Slang factory + stub entirely.** Rejected: the factory
  + interface are the seam that lets the Phase-3 backend land without
  touching consumers; removing them would be a regression of the
  ADR-20260612 design intent.

## References

- `engine/render/shader/src/SlangCompilerStub.cpp` — the gate-stub TU.
- `engine/render/shader/CMakeLists.txt` lines 76–87 — `CD_ENABLE_SLANG`
  forward-compat option + WARNING.
- `engine/render/shader/include/cd/shader/Compiler.hpp` —
  `make_slang_compiler()` factory contract.
- `docs/ADR/ADR-20260612-shader-library-architecture.md` — the
  Phase-3-candidate architecture decision this seal formalises.
- `docs/ROADMAP_80_TO_100.md` — the 100% mandate requiring this seal.
