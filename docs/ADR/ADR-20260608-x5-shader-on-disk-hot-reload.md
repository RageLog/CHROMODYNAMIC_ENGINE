# ADR-20260608 — X5 Shader On-Disk + Hot-Reload (Phase 2 lock-down)

- **Status**: Proposed (Phase 2 design lock-down; sign-off pending §5 open questions)
- **Date**: 2026-06-08
- **Branch**: dev
- **Deciders**: Cemal TATLI
- **Author**: architect subagent
- **Supersedes (in part)**: ADR-20260529-X5-shader-on-disk-hot-reload (§"Decision" still authoritative for `MaterialDesc::vertex_glsl_path` precedence and folder layout; this ADR LOCKS the release-mode toolchain, the deferred-release invariant, and the HotReloadBus integration that the May-29 ADR deferred).
- **Related**: ADR-20260530-metal-backend §1.5 (SPIRV-Cross MSL already in tree), ADR-001 (RHI surface), ADR-20260528-job-system-design (no concurrency hazard added — recreate runs on the frame-owning thread), ADR-016 (vendor matrix; affects vcpkg manifest), docs/ROADMAP_PHASE_2.md §3 + §6 Q1 + §6 Q4.
- **Scope**: This ADR is the FIRST Phase 2 milestone per ROADMAP §5.1. It does not gate X4 or X1-FU-F; it does fix the contract those milestones inherit when they author/edit shaders.

> **Scope guard**: header + ADR only. No `.cpp` lands from this ADR. The implementer (developer agent, X5-B..F per ROADMAP §3.3) is bound by the contracts below and must Edit, not rewrite, the headers named in §2.

---

## 1. Bağlam (Context)

### 1.1 What already shipped (Run 17, ADR-20260529-X5)

- `samples/engine/hello_engine/shaders/` exists; `prim.vert.glsl` + `prim.frag.glsl` are tracked text.
- `MaterialDesc::vertex_glsl_path` / `fragment_glsl_path` are live (verified at `engine/render/material/include/cd/material/Material.hpp:103-104`).
- `cd::shader::FileWatcher` (portable poll, no native event API) exists at `engine/render/shader/include/cd/shader/FileWatcher.hpp`.
- `cd::shader::ICompiler` accepts arbitrary source text; the glslang factory is the only live backend (`engine/render/shader/include/cd/shader/Compiler.hpp:158`). A Slang factory exists as a `nullptr`-returning stub.
- `cd::game::asset_hot_reload::HotReloadBus` (100 ms throttle, polling FileWatcher under the hood) exists at `engine/game/asset_hot_reload/include/cd/game/asset_hot_reload/HotReload.hpp` with the `AssetCategory::kShader` enum reserved.
- Sample-side toggle `HELLO_ENGINE_USE_ON_DISK_SHADERS=$<BOOL:..>` already gates the on-disk path against the embedded literal fallback at the call site (`samples/engine/hello_engine/HelloMaterials.hpp:198`, `:266`).

### 1.2 What is still missing (what THIS ADR closes)

The May-29 ADR shipped the on-disk **plumbing** but explicitly deferred four points the user is now asking us to lock so X5 can be called done:

1. **`Material::recreate(...)` is documented but not signed off.** The public surface still has only `create`; the May-29 ADR named `recreate` as the eventual pattern but did not lock its contract against the deferred-release invariant.
2. **Release-mode fallback toolchain is undecided.** §6 Q1 of ROADMAP_PHASE_2 is FLAGGED IRREVERSIBLE: which SPIR-V toolchain ships in the release-mode (`HELLO_ENGINE_USE_ON_DISK_SHADERS=OFF`) and engine-as-library binaries that consumers ship. Three live options: (a) keep glslang only, (b) add shaderc as the cached fast-path, (c) author HLSL via DXC and use glslang's HLSL frontend on Vulkan. The choice locks the vcpkg manifest and the cross-platform binary cost.
3. **HotReloadBus wiring vs. direct FileWatcher.** The May-29 ADR wired `HelloShaderWatch.hpp` directly to `cd::shader::FileWatcher`. Run 23 promoted `cd::game::asset_hot_reload::HotReloadBus` as the canonical multi-subscriber surface with 100 ms throttle. X5's V2 wiring must route through the bus so a future editor (L5) and a future material parameter live-edit (G5.x) share the same poll.
4. **Material include semantics.** Inline `vertex_glsl` is compile-time content — it must NOT be eligible for hot-reload, because the literal lives in C++ and the file watcher has no signal. The May-29 ADR left this implicit; this ADR makes it a typed invariant.

### 1.3 Dev-loop cost — current vs. target

Measured during the Run 17–20 marathon while iterating cloud / fog / tonemap parameters in `composite.frag.glsl`:

| Stage                                | Current (edit → embedded literal) | Current (edit → on-disk + restart) | Target (X5 V2 hot-reload)                  |
| ------------------------------------ | --------------------------------- | ---------------------------------- | ------------------------------------------ |
| Edit `.glsl`                         | 5 s                               | 5 s                                | 5 s                                        |
| Save + (`configure_file` regen)      | -                                 | -                                  | -                                          |
| Rebuild engine + sample              | 18–35 s (incremental)             | 0 s                                | 0 s                                        |
| Re-launch sample, navigate to view   | 8–12 s                            | 8–12 s                             | 0 s (PSO swaps in the live process)        |
| **Total iteration latency**          | **31–52 s**                       | **13–17 s**                        | **<1 s (next frame after 100 ms throttle)**|

Run 18 (cloud quality polish) and Run 19 (TAA history decay) burnt ~26 round-trip iterations each. Compressing the loop from ~40 s to <1 s is the single largest dev-experience win Phase 2 can deliver, which is why ROADMAP §5.1 puts X5 first.

### 1.4 Cross-platform alignment with Metal (ADR-20260530)

ADR-20260530-metal-backend §1.5 confirms `engine/render/spirv_cross_glue/include/cd/spirv_cross_glue/Translate.hpp` already exposes a SPIR-V → MSL pipeline. The X5 hot-reload story for macOS is therefore: GLSL → glslang → SPIR-V → SPIRV-Cross → MSL → Metal pipeline rebuild. No new toolchain dependency lands for the Apple platforms beyond what L1 already needs. This protects the `cross-API` claim in the engine charter.

### 1.5 Threading + GPU-lifetime hazard

`Material` holds `cd::rhi::GraphicsPipelineHandle` + `ShaderModuleHandle` × 2 + `DescriptorSetLayoutHandle` + `PipelineLayoutHandle`. On Vulkan these become `vkDestroyPipeline` calls that **must not** run while the GPU is still consuming the pipeline (TDR / device-lost otherwise; see Marathon Run 20 memory bullet on the bindless descriptor crash class). The job-system ADR (ADR-20260528) treats device handles as owner-only; the recreate path therefore runs synchronously on the frame-owning thread between `present` and `acquire`, NOT off-thread.

---

## 2. Karar (Decision)

Six coordinated changes (X5-A → X5-F), each with a typed contract and a done-criterion. Header surfaces below are normative — the implementer must Edit the existing files to match, not introduce parallel new ones.

### 2.1 LOAD-BEARING CHOICE — release-mode SPIR-V toolchain

**Decision**: **Keep glslang as the single in-process toolchain for both debug and release.** Do not vendor shaderc. Do not flip the engine source-of-truth to HLSL+DXC. Ship pre-compiled `.spv` blobs **next to** each `.glsl` source as the release-mode fast-path; runtime falls back to glslang compile only if the `.spv` is missing or stale.

Rationale (the §6 Q1 recommendation):

- **glslang is already in tree** (Run 17). Adding shaderc duplicates ~6 MB of binary and an entire third-party with no functional delta — shaderc is a thin wrapper around glslang's `SPIRV-Tools` path that we already get directly.
- **DXC-only (HLSL author) is rejected** as the release path because Vulkan SPIR-V emission from DXC's HLSL→SPIR-V backend is feature-incomplete for our ray-tracing + mesh-shader path (Run 18 + W8-BE shipped GL_EXT_ray_query + GL_EXT_mesh_shader explicit, both authored as GLSL).
- **Pre-compiled `.spv` next to `.glsl`** keeps the dev-loop unchanged (debug still hot-reloads from `.glsl` via glslang) AND makes the release binary independent of glslang at runtime — release binaries link `cd::shader` with `CD_ENABLE_GLSLANG=OFF` and call only `cd::shader::load_spirv_file` (already in `Compiler.hpp:171`).
- **Build-time SPV emission** runs via a CMake function (`cd_compile_glsl_to_spv(...)`) that shells out to `glslangValidator` from the build host's vcpkg tree. The output `.spv` lands next to its source under `<bindir>/shaders/`; the loader resolves `<source>.spv` before falling back to source compile.
- **vcpkg manifest impact**: zero new dependencies. Removes a future temptation to add shaderc. ADR-016 vendor matrix entry for glslang stays Tier-A.

Consequence: the engine ships ONE shader toolchain on the developer machine (glslang via vcpkg), and ZERO shader toolchains in a released library consumer's binary (only the `.spv` blob loader). This is the cheapest, most cross-platform, most consumer-friendly path.

### 2.2 X5-A — `MaterialDesc` precedence + `Material::recreate` contract

`engine/render/material/include/cd/material/Material.hpp` already documents the precedence (`*_spirv > *_glsl_path > *_glsl`). LOCK this contract by adding a typed sentinel and the recreate signature.

Required header additions (developer to Edit, not Write):

```cpp
namespace cd::material {

/// Source layer reported by Material::source_kind(). Diagnostic-only; the
/// engine never selects behaviour off this enum (precedence is decided by
/// the non-empty MaterialDesc field per §2.1 of ADR-20260608).
enum class MaterialShaderSource : std::uint8_t {
    kInlineGlsl   = 0,   ///< vertex_glsl / fragment_glsl (compile-time content; NOT hot-reloadable).
    kOnDiskGlsl   = 1,   ///< vertex_glsl_path / fragment_glsl_path (hot-reloadable).
    kPrecompiled  = 2,   ///< vertex_spirv / fragment_spirv (asset pipeline; NOT hot-reloadable).
};

class Material {
public:
    // existing surface stays untouched ...

    /// Reports which input pair was used at create() time. Hot-reload code
    /// uses this to gate registration with HotReloadBus: only kOnDiskGlsl
    /// is eligible. Inert Materials return kInlineGlsl as the documented
    /// default (the value is irrelevant when is_valid() == false).
    [[nodiscard]] MaterialShaderSource source_kind() const noexcept;

    /// Re-build the pipeline from `desc`. Old RHI handles are NOT released
    /// inside this call — the returned Material owns the new handles, and
    /// the caller MUST assign-over the old Material AFTER the device has
    /// reported the previous frame's submission complete. The Material
    /// move-assignment operator then calls release_() on the old handles,
    /// which in turn issues device->wait_idle() before destroy.
    ///
    /// Threading: must be called on the frame-owning thread. Not safe to
    /// call concurrently with apply() / bind() on any thread.
    ///
    /// Errors:
    ///   * material_errors::kShaderCompileFailed — front-end diagnostic in message.
    ///   * material_errors::kPipelineCreationFailed — RHI rejected the PSO.
    ///   * material_errors::kCompilerRequired — desc still asks for GLSL but compiler == nullptr.
    /// On error the existing Material is unchanged; the caller keeps rendering with the
    /// old pipeline (defensive: a broken edit does NOT brick the live session).
    [[nodiscard]] static cd::core::Result<Material>
    recreate(cd::rhi::IDevice& device,
             cd::shader::ICompiler* compiler,
             const MaterialDesc& desc);
};

}  // namespace cd::material
```

Done-criterion (X5-A): the header above compiles; `cd_test_material_recreate.cpp` (host-portable, see §4.4) exercises the precedence table and the inert-Material default.

### 2.3 X5-B — On-disk file resolution + release-mode `.spv` fast-path

A new free function in `cd/material/MaterialLoader.hpp` (developer to add as a sibling header — additive only, does not touch the existing `Material.hpp` surface):

```cpp
namespace cd::material {

struct LoadedShaderSource {
    std::string glsl_text {};                  ///< Owning copy of the .glsl file text.
    std::vector<std::uint32_t> spirv {};       ///< Non-empty when .spv fast-path hit.
    std::filesystem::path resolved_path {};    ///< Diagnostic; what we actually opened.
    bool used_spirv_fastpath { false };        ///< True when .spv was found AND fresher than .glsl.
};

/// Resolve `path` (relative to cwd OR an explicit search-root composed by the
/// caller), prefer a co-located `.spv` blob, and return both forms so the
/// caller can decide whether to compile or hand straight to the RHI.
///
/// Search order:
///   1. `<path>.spv` next to the .glsl — used iff present AND mtime >= the .glsl's mtime.
///   2. `<path>` (the .glsl source) — read into glsl_text for compile via ICompiler.
///
/// Returns kInvalidArgument when neither exists.
[[nodiscard]] cd::core::Result<LoadedShaderSource>
load_shader_source(std::string_view path);

}  // namespace cd::material
```

Done-criterion (X5-B): the loader picks `.spv` when fresh, falls back to `.glsl` when `.spv` is missing or stale, returns kInvalidArgument on neither-present. Three test cases in `cd_test_material_loader.cpp` (host-portable, see §4.4).

### 2.4 X5-C — Hot-reload via HotReloadBus, NOT direct FileWatcher

The May-29 ADR wired `HelloShaderWatch.hpp` directly to `cd::shader::FileWatcher`. Replace that with a thin adapter routing through `cd::game::asset_hot_reload::HotReloadBus` so debounce + multi-subscriber semantics are inherited rather than re-implemented.

Sample-side header (`samples/engine/hello_engine/HelloShaderWatch.hpp`, developer to Edit):

```cpp
namespace cd_sample {

class HelloShaderWatch {
public:
    HelloShaderWatch();   ///< Constructs the underlying HotReloadBus with 100 ms throttle.
    ~HelloShaderWatch();

    /// Register a Material* for hot-reload. The material must have been built
    /// from a vertex_glsl_path + fragment_glsl_path pair (source_kind() ==
    /// kOnDiskGlsl); otherwise registration is silently dropped with a log warning.
    ///
    /// The desc is captured by value so a future recreate() call has the
    /// full pipeline configuration without re-querying the Material.
    void register_material(cd::material::Material* mat,
                           cd::material::MaterialDesc desc);

    /// Pump the bus exactly once. Call AFTER present and BEFORE acquire of
    /// the next frame. On a dirty edit, recreate the affected material; the
    /// old Material is released via move-assignment which internally calls
    /// device->wait_idle() before destroy (X5-D).
    void poll_and_reload(cd::rhi::IDevice& device,
                         cd::shader::ICompiler* compiler);

private:
    std::unique_ptr<cd::game::asset_hot_reload::HotReloadBus> bus_;
    // path → (Material*, captured MaterialDesc) registry omitted for brevity.
};

}  // namespace cd_sample
```

Throttle window: 100 ms (HotReloadBus default). Rationale: editor saves typically coalesce 2-4 mtime ticks within ~30 ms; 100 ms is the smallest window that reliably collapses bursts without adding perceptible delay.

Done-criterion (X5-C): `cd_test_hello_shader_watch.cpp` (Tier B integration) registers a material, mutates `prim.frag.glsl`, asserts exactly ONE recreate within 250 ms.

### 2.5 X5-D — Deferred-release invariant (the load-bearing GPU-lifetime rule)

`Material::recreate()` returns a fresh Material; the OLD Material is destroyed by the caller's move-assignment. The move-assignment operator on `Material` MUST:

1. Capture the old `device_` pointer.
2. Call `device_->wait_idle()` (existing `cd::rhi::IDevice` member; see `engine/render/rhi/include/cd/rhi/IDevice.hpp`).
3. Then call `release_()` which destroys the pipeline / layouts / shader modules in LIFO order.

This is non-optional. Vulkan PSO destruction while the GPU is consuming the pipeline is a TDR-class bug (Run 18 BLAS-truncation-class lesson applies: silent failures cost ~30 commits each). The `wait_idle` cost is ~1 ms once per shader edit — acceptable for a hot-reload path, prohibitive for steady-state but X5 only touches steady-state when an edit fires.

A future ADR may upgrade this to "wait on the timeline semaphore value at the moment of `present` for the frame that last used the pipeline", which is non-blocking. That is OUT OF SCOPE for X5; `wait_idle` is the V1 invariant.

Done-criterion (X5-D): a TSan-clean test asserting that `recreate → assign → next frame draw` issues no validation error and no use-after-free on the destroyed shader modules. Test is gated on a live device, so it lives in `cd_test_hello_shader_watch.cpp` (integration tier), not the host-portable suite.

### 2.6 X5-E — Inline-GLSL is read-only (compile-time content)

`MaterialDesc::vertex_glsl` (inline literal) explicitly does NOT participate in hot-reload. The HotReloadBus registration path (§2.4) checks `Material::source_kind() == kOnDiskGlsl` and drops the registration otherwise. The May-29 ADR left this implicit; making it typed prevents a future contributor from "fixing" hot-reload to scan in-process literals (impossible — the literal IS the source; there is no second copy to diff).

Sample-tier guidance (already in `HelloMaterials.hpp` lines 193–200): when both `vertex_glsl` and `vertex_glsl_path` are present, `vertex_glsl_path` wins per the precedence rule. The embedded literal becomes a shipped-binary fallback, not a hot-reload-eligible source.

Done-criterion (X5-E): a unit test confirms a Material built from `vertex_glsl` returns `kInlineGlsl` and is silently dropped by `HelloShaderWatch::register_material`.

### 2.7 X5-F — Doc + ADR + sample README

- This ADR (the file you are reading) lands as the load-bearing record.
- `samples/engine/hello_engine/README.md` gains a "Shader hot-reload" section pointing at `HelloShaderWatch.hpp` + the toolchain-fallback table from §2.1.
- `engine/render/material/README.md` gains a §"Hot-reload" subsection cross-linking to this ADR and naming the three not-eligible cases (inline GLSL, precompiled SPIR-V, inert Material).

Done-criterion (X5-F): doc-writer agent ships both READMEs with a working `ctest -R cd_test_material_recreate` invocation block.

---

## 3. Reddedilen alternatifler (Rejected alternatives)

### 3.1 Use shaderc instead of glslang for the runtime path

Rejected. shaderc wraps glslang and adds a libshaderc_shared.so/dll layer (~6 MB) plus an extra vcpkg port to track. We already integrate glslang directly via the `cd::shader::make_glslang_compiler()` factory. No functional gain; pure binary-size regression. shaderc's only real advantage is the bundled `#include` preprocessor, which we explicitly defer to a future ADR (the V1 shader corpus has no includes).

### 3.2 DXC-only (flip the engine to HLSL source-of-truth)

Rejected. (a) Vulkan SPIR-V emission from DXC's HLSL→SPIR-V backend is feature-incomplete for `GL_EXT_ray_query` and `GL_EXT_mesh_shader` extensions that W8-BE / Run 18 / phase 866 already ship and depend on. (b) Forces every existing `.glsl` file in tree to be ported once, then maintained as a second source — Marathon Run 16's 25 commits touched 12 distinct `.glsl` files; doubling the corpus doubles the edit cost. (c) DXC remains the right tool for the D3D12-native shader path (X4-A); keep it scoped there.

### 3.3 Pre-compile to SPV at build time only, no runtime compile at all

Rejected. This kills the dev-loop benefit — without a runtime glslang the hot-reload path becomes "edit .glsl → invoke build-host glslangValidator from inside the running process → load .spv → recreate". That is functionally identical to today's "edit → rebuild → restart" pattern with one fewer step, not a sub-second iteration loop. The SPV-fast-path in §2.1 keeps the build-time SPV as the **release-mode** optimisation while preserving the **debug-mode** runtime compile path.

### 3.4 Native OS file-watch (ReadDirectoryChangesW / inotify / FSEvents)

Rejected, same reasoning as ADR-20260529-X5 §"Rejected alternatives". The polling watcher is portable across Win + Linux + macOS with zero platform ifdefs, costs sub-millisecond per poll at our scale (~tens of files), and is race-free by construction (the next poll catches any change the previous one missed). Native APIs add per-OS background threads and rename-edge-case complexity (rename → delete-then-create on some platforms, in-place on others) that polling sidesteps entirely. Revisit when the shader corpus exceeds ~500 files OR the build host's CPU shows polling in a profile.

### 3.5 Hot-swap in-place by patching the existing PSO

Rejected. Vulkan pipelines are immutable; D3D12 pipeline state objects are immutable; Metal `MTLRenderPipelineState` is immutable. The "create new → wait_idle → release old" pattern is the only correct cross-API path. Locking it as the X5 invariant (§2.5) prevents a future contributor from "optimising" away the deferred release and reintroducing a TDR.

### 3.6 Use `cd::vfs::IFileSource` instead of `std::filesystem::path` directly

Rejected for V1, same reasoning as the May-29 ADR. `cd::vfs` is built for pak / zip-mounted asset streams at runtime; it has no write-notification surface and adds an indirection that the on-disk hot-reload path doesn't want. When shipped-binary asset packing becomes a requirement (post-L5 editor binary), a follow-up ADR can introduce an `IShaderSource` interface that vfs + filesystem both implement; until then the raw path is the cheapest correct surface.

### 3.7 Bypass HotReloadBus, keep direct FileWatcher in HelloShaderWatch

Rejected at V2. The May-29 ADR wired FileWatcher directly because HotReloadBus did not exist yet. Run 23 shipped the bus with throttle + multi-subscriber semantics; reusing it lets the future editor (L5) and material parameter live-edit (G5.x) share one poll source and one debounce window. Direct FileWatcher use becomes a code-smell — anyone wiring a new reload path goes through the bus.

---

## 4. Sonuçlar (Consequences)

### 4.1 Positive

- **Sub-second shader iteration** in `hello_engine`. The 31–52 s round-trip from §1.3 collapses to <1 s. Run 18–20 style polish marathons become 10× cheaper.
- **Release binaries shed glslang**. Engine consumers that build with `CD_ENABLE_GLSLANG=OFF` (a flag added by X5-B) link only `cd::shader::load_spirv_file` plus the pre-compiled `.spv` blobs. Binary size drops by the glslang static lib footprint (~3 MB on Windows debug).
- **One shared reload bus**. Texture, mesh, audio, locale, AND shader reload all flow through `cd::game::asset_hot_reload::HotReloadBus`. Editor L5 inherits this surface for free.
- **GPU-lifetime safety codified**. The `wait_idle`-before-release invariant lives in `Material::operator=(Material&&)` and is exercised by every recreate test; future contributors cannot accidentally regress it.
- **Cross-platform from day 1**. The glslang → SPIR-V → SPIRV-Cross → MSL chain is already in tree (ADR-20260530 §1.5); macOS hot-reload works the moment L1 Metal lands without further toolchain decisions.

### 4.2 Negative / risk

- **`wait_idle` is a stall**. Each shader edit blocks the frame for ~1 ms (Vulkan idle drain). Acceptable while editing one file at a time; a "save all" across 10 shaders would stall ~10 ms. Mitigation: HotReloadBus 100 ms throttle coalesces a multi-file save into one batched recreate pass; the stall amortises across the batch.
- **`.spv` blob freshness depends on the build step**. If a developer edits `.glsl` and forgets to re-run `cmake --build`, the release-mode path serves a stale `.spv`. Mitigation: `load_shader_source` checks mtime — if `.glsl` is newer than `.spv` it falls back to the source compile path. Belt + braces.
- **HotReloadBus runs on the frame-owning thread**. A pathological "watcher polls 10000 files" would block the frame. Mitigation: the X5 V2 use-case watches ~10 shader files; the framework cap is a `static_assert` and an ADR clause if usage ever approaches the limit.
- **glslang infoLog text is ugly**. Front-end diagnostics surface verbatim via `material_errors::kShaderCompileFailed`. Mitigation: out of scope for X5; a future ADR may add a diagnostic re-formatter.

### 4.3 Replace-ready

This ADR's invariants survive every plausible toolchain swap. Three concrete swaps are already named:

- **glslang → Slang** (the `make_slang_compiler` factory is already a stub in `Compiler.hpp:165`). Recreate semantics unchanged.
- **Polling FileWatcher → native OS event source**. HotReloadBus surface unchanged; only the watcher implementation moves.
- **Per-frame `wait_idle` → timeline-semaphore deferred release**. `Material::operator=(Material&&)` body changes; the contract ("old handles are safe to destroy after this returns") does not.

### 4.4 Test gates

Three tests are MANDATORY before X5 closes. All three are **host-portable** at the ICompiler surface — they do NOT require a Vulkan device. The fourth is the integration smoke; it requires a device.

| Test                                       | Tier              | Surface                                                  | Done-criterion                                                                                          |
| ------------------------------------------ | ----------------- | -------------------------------------------------------- | ------------------------------------------------------------------------------------------------------- |
| `cd_test_material_loader.cpp`              | Host-portable     | `cd::material::load_shader_source` (§2.3)                | 3 cases: (a) `.spv` fresh, returns spirv non-empty + glsl_text empty; (b) `.glsl` newer, returns spirv empty + glsl_text non-empty; (c) neither present, returns kInvalidArgument. |
| `cd_test_material_source_kind.cpp`         | Host-portable     | `MaterialDesc` precedence → `Material::source_kind`      | 4 cases: (a) only inline glsl → kInlineGlsl; (b) only path → kOnDiskGlsl; (c) only spirv → kPrecompiled; (d) all three set → kPrecompiled (spirv wins per precedence rule). Compile is mocked via a stub ICompiler that returns a 1-word SPIR-V module. |
| `cd_test_shader_watcher_throttle.cpp`      | Host-portable     | `cd::game::asset_hot_reload::HotReloadBus` with the test-only `tick(TimePoint now)` injection seam (existing in `HotReload.hpp:219`) | 3 cases: (a) single edit fires once; (b) burst of 5 edits within 50 ms collapses to 1 callback at next tick after 100 ms; (c) edit + 100 ms gap + edit fires twice. No `sleep_for` — `tick(TimePoint)` drives the clock per CLAUDE.md §5 anti-flakiness rule. |
| `cd_test_hello_shader_watch.cpp`           | Integration (B)   | Full sample-side surface against a live `cd::rhi::IDevice` | (Gated on the dev box.) Boot hello_engine in headless mode, mutate `prim.frag.glsl`, assert one recreate within 250 ms, assert next frame draws without validation error.    |

Existing `cd_test_file_watcher_hotreload.cpp` (already PASS at 2/2) stays as the FileWatcher-only regression net. No test deletion.

CLAUDE.md §3 (evidence-based) discipline: all four tests must be GREEN at `ctest --preset ninja-debug --output-on-failure` filtered to `^cd_test_material_recreate|^cd_test_material_loader|^cd_test_material_source_kind|^cd_test_shader_watcher_throttle|^cd_test_hello_shader_watch$` before X5 is marked DONE in `STATUS_AND_PLAN_W8.md`.

---

## 5. Açık sorular — kullanıcı onayı gerekiyor (Open questions for user sign-off)

These three questions must be answered before X5 ships. The ADR's body assumes the recommendation in each; flip the recommendation in a comment on this ADR and the implementation re-routes.

### Q1 — Release-mode toolchain (§6 Q1 of ROADMAP_PHASE_2, the load-bearing decision)

**Recommendation in this ADR**: keep glslang in-process for debug; ship `.spv` blobs next to `.glsl` for release; no shaderc, no DXC, no engine-source-of-truth flip to HLSL.

**Alternatives that need an explicit veto**:
- (a) Vendor shaderc too, in case future preprocessor-include support needs it.
- (b) Skip the `.spv` build step; require glslang at runtime in release too.
- (c) Flip GLSL → HLSL across the engine and use DXC + glslang HLSL frontend.

The ADR is written assuming the recommendation; if the user picks (a) the vcpkg manifest gains shaderc; if (b) the release binary keeps the glslang link; if (c) the entire `samples/*/shaders/*.glsl` corpus needs porting and §1.4 Metal alignment via SPIRV-Cross has to be re-validated.

### Q2 — HotReloadBus throttle window

**Recommendation in this ADR**: 100 ms (HotReloadBus default).

**Alternatives**:
- (a) 50 ms — feels snappier but risks double-fire on slow editors (VSCode + a saving plugin).
- (b) 250 ms — safer for "save all" coalescing, perceptibly laggier on a single edit.
- (c) Make it configurable per-session via an ImGui slider in the hello_engine FX panel.

The ADR assumes 100 ms; (c) is the most user-friendly but adds 2-3 lines of UI to `HelloFxPanel.hpp` that the X5 scope was not budgeted for.

### Q3 — Max stale-PSO age before forced reload?

**Recommendation in this ADR**: no max; rely on edit-driven invalidation only.

**Alternatives**:
- (a) Force a recreate every N minutes regardless (defends against undetected disk churn).
- (b) Force a recreate when the user toggles a specific ImGui "rebuild shaders" button (manual).
- (c) Watch the build-host's `.spv` mtime too, so a `cmake --build` from a sibling terminal triggers reload without editing the `.glsl`.

The ADR assumes none; (c) is the most useful for a developer who edits via "rebuild + glslangValidator" rather than direct `.glsl` edit, but requires double-watching every shader (the `.glsl` AND the `.spv`). Defer to a future ADR if the workflow demands it.

---

## 6. Reference

- `engine/render/material/include/cd/material/Material.hpp` (extended by X5-A; precedence comment lines 96-104 already authoritative).
- `engine/render/shader/include/cd/shader/Compiler.hpp` (load_spirv_file at :171 is the release-mode loader entry; make_glslang_compiler at :158 is the debug-mode runtime path; make_slang_compiler at :165 is the future swap target).
- `engine/render/shader/include/cd/shader/FileWatcher.hpp` (poll watcher; consumed by HotReloadBus, not by HelloShaderWatch directly anymore).
- `engine/game/asset_hot_reload/include/cd/game/asset_hot_reload/HotReload.hpp` (HotReloadBus + AssetCategory::kShader + 100 ms throttle default + test-only `tick(TimePoint)` injection seam at :219).
- `samples/engine/hello_engine/HelloMaterials.hpp` (call-site for the `*_glsl_path` precedence, lines 193-200 and 264-268).
- `samples/engine/hello_engine/CMakeLists.txt` (HELLO_ENGINE_USE_ON_DISK_SHADERS option at :3-5; `target_compile_definitions` at :74-75; shaders copy step at :77+).
- `engine/render/shader/tests/test_file_watcher_hotreload.cpp` (existing FileWatcher regression net; not deleted, not modified).
- `engine/render/spirv_cross_glue/include/cd/spirv_cross_glue/Translate.hpp` (SPIRV-Cross MSL surface used by L1 Metal; cross-platform alignment per ADR-20260530 §1.5).
- `docs/ADR/ADR-20260529-X5-shader-on-disk-hot-reload.md` (predecessor; this ADR LOCKS the §"Decision" bullets it left as roadmap).
- `docs/ADR/ADR-20260528-job-system-design.md` (no concurrency hazard added — recreate runs on frame-owning thread only).
- `docs/ROADMAP_PHASE_2.md` §3 (X5 scope + sub-task DAG), §5.1 (X5 first), §6 Q1 + Q4 (this ADR closes both).

## 7. Demir Kural status

No academic citation. Shader hot-reload + deferred-release-after-wait_idle are industry-standard engineering practice (Filament `Engine::flushAndWait` pattern, Unreal `FRHIResource::AddRef/Release` deferred deletion queue, Unity SRP shader stripping pipeline). No peer-reviewed paper is needed and none is cited — per CLAUDE.md §2, citation without a verified PDF + bibliography entry is forbidden.
