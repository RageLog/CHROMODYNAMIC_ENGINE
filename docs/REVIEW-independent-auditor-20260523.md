# Independent review — CHROMODYNAMIC v0.25.0

- Reviewer: **Axis-D independent auditor** — fresh-eyes pass, no prior exposure to the marathon transcript; my context is 15+ years writing engine code (Vulkan/D3D12), CMake install/export pipelines, and library-API stewardship reviews.
- Date: 2026-05-23
- Commit reviewed: d4a9a57 on branch dev (tag under review: v0.25.0)
- Time invested: ~3.5 hours, headers + the seven mandatory docs + spot-checks (no local build).
- Read list completed: **yes** — Readme, ARCHITECTURE, LIBRARIES, CONSUMING, ADR-wave125/132/133, foundation headers (Result, ErrorCode, ErrorFormat, Handle, HandleStore, CVar, Version, Assert, ILogger, RingBufferSink, PanicDump), plus spot-checks on cd::imgdiff::ImageDiff, cd::asset_image::Image, and cd::rhi::ICommandBuffer.

---

## What I think is solid

- **The rollback itself is the strongest signal in the repo.** ADR-wave125 is unusually honest: a project that ships a v1.0, finds five visual bugs through manual smoke, and un-ships is doing something most teams cannot. The four-axis maturity gate as a replacement reads as the right framework, not theatre. The fact that I am being commissioned at all to be Axis D is consistent with the ADR commitments — that is rare. (docs/ADR/ADR-20260523-wave125-v1.0-rollback.md)
- **cd::core::ErrorCode owning-message redesign is correct.** The make_owning + rewrap pair (engine/foundation/core/include/cd/core/ErrorCode.hpp:73-109) is the right shape: zero-alloc constexpr literals when the caller has a string-literal, opt-in shared_ptr<const std::string> storage for runtime-built diagnostics, and rewrap preserves the owning back-store across domain translation without bumping the shared_ptr to a copy. The fix-history (bug #3 in the rollback) hooks directly to a real lifetime hazard, and the design that landed is durable.
- **cd::diag::panic not-noexcept is the right call.** The header comment at engine/foundation/diag/include/cd/diag/Assert.hpp:80-87 correctly identifies the conflict between the engine wanting to be honest about noexcept-ness and tests wanting to substitute a throwing handler. Many engines get this backwards and silently std::terminate when their test fixtures throw. The hybrid [[noreturn]] + non-noexcept here is honest.
- **Handle<Tag> layout is small, hashable, comparable, type-safe at compile-time.** 32-bit index + 16-bit generation + 16-bit type-id packs into 64 bits with static_assert. The SplitMix64 finalizer (Handle.hpp:155-158) for std::hash is overkill but harmless. Phantom-tag prevents TextureHandle vs BufferHandle mixups at compile-time.
- **Cross-vendor noise floor characterization in ARCHITECTURE.md section 6 is concrete.** Per-sample max-delta / RMSE / PSNR against the NVIDIA reference, with a hypothesis (more shader arithmetic produces more rounding sites) that matches the observed hello_skybox outlier. This is the kind of empirical work that converts gate-is-too-tight or gate-is-too-loose into a measurable question.
- **Lavapipe CI job is wired, not just promised.** .github/workflows/ci.yml:177-192 actually adds the Linux Vulkan-on-software-renderer job for Phase 11 Track B — many Track-B-is-queued repos turn out to have nothing in CI. This one does.
- **DAG enforcement is observable.** grep for cd::render / cd::world / cd::runtime inside engine/foundation and engine/asset returns no real upward dependencies — only two comment mentions in bench/Benchmark.hpp and platform/Window.hpp. The no-cycles claim is not lip-service.

## What I think is wrong (blocking for v1.0)

### B1. LIBRARIES.md is materially incorrect — a frozen-surface catalogue must not lie

- **Where:** docs/LIBRARIES.md:1 (header says ships 48 libraries) and the foundation tier table at docs/LIBRARIES.md:22-39.
- **Why blocking:** The doc lists cd_serialization / cd::serialization as a foundation library, with a public namespace and include root. **It does not exist on disk** — grep for cd_serialization or cd::serialization under engine/ returns zero results. README.md and ARCHITECTURE.md also list it in their tier summaries. v1.0 promises ABI stability for cd::serialization public headers; we cannot promise stability for a target that does not compile. Separately, the doc declares 48 libraries but the actual on-disk count is 55 (15 foundation + 10 asset + 16 render + 7 world + 3 ui + 1 runtime + 1 imgdiff + 1 imgui_backend + 1 script). The README says 55 and is correct. LIBRARIES.md silently undercounts by missing every render-tier stub (rhi_d3d12, rhi_metal, rhi_native, async_submit, cluster_gpu, cluster_pbr, volumetric, ibl) and both side libs.
- **Suggested fix:** Either author cd::serialization as a real library before v1.0 (it shows up in design docs as if it exists) **or** scrub it from README/ARCHITECTURE/LIBRARIES and from any frozen-surface list. Then regenerate LIBRARIES.md from the actual engine/*/CMakeLists.txt set so it matches the README 55-library count.

### B2. Case-sensitivity landmine: docs reference Engine/, on-disk is engine/

- **Where:** docs/ARCHITECTURE.md and docs/CONSUMING.md (e.g. docs/REVIEW_BRIEF.md:68 reads Engine/foundation/core/include/...). The on-disk directory is engine/ (lowercase) — confirmed by ls on the repo root.
- **Why blocking:** On Linux / case-sensitive ext4 / macOS APFS-case-sensitive, every cross-reference link in the docs is broken. More importantly, if any #include directive anywhere in the tree (or in CMake target_include_directories) used capitalized Engine/..., the Windows + macOS-default-insensitive builds would silently succeed and Linux would fail. This is precisely the class of bug that a v1.0 ABI promise cannot tolerate — different OSes seeing different headers. The CI matrix on Linux probably catches the build side but cannot catch the doc-link side.
- **Suggested fix:** Lowercase every Engine/ reference in docs/*.md and docs/REVIEW_BRIEF.md before tagging v1.0. Grep the tree for Engine/ and normalize.

### B3. Vulkan debug label uses a stack-buffer pointer that the driver may read after the call returns

- **Where:** engine/render/rhi_vulkan/src/VulkanCommandBuffer.cpp:634-651 (VulkanCommandBuffer::push_debug_group).
- **Why blocking (or strong should-not-be-in-v1.0-RHI-surface):** The implementation writes the label name into a 128-byte stack array char buf[128] and passes the address as VkDebugUtilsLabelEXT::pLabelName to vkCmdBeginDebugUtilsLabelEXT. The Vulkan spec is ambiguous about exactly when the driver consumes that pointer, but a safe reading is: the label must outlive command buffer execution. buf does not outlive even the function return. In practice most drivers copy on the call so it works; on strict validation layers and on some Intel/AMD drivers this has been seen to surface dangling-read warnings. The ICommandBuffer::push_debug_group(std::string_view) header (engine/render/rhi/include/cd/rhi/ICommandBuffer.hpp:158) does not document the lifetime contract either — so a future backend author has no way to know.
- **Suggested fix:** Either (a) document on ICommandBuffer::push_debug_group that callers must pass a string with lifetime >= command-buffer-execution and copy onto a per-command-buffer arena in the Vulkan backend, or (b) make push_debug_group take const char* (literal-only) and surface the contract in the type system. The current header signature lies to the consumer.

### B4. ABI-versioning policy is too loose for a pre-1.0 line

- **Where:** docs/CONSUMING.md:222-225 (find_package(CHROMODYNAMIC 0.25 REQUIRED) accepts any 0.25.x. The config writes a SameMajorVersion policy ...).
- **Why blocking:** CMake SameMajorVersion treats 0.25.0 and 0.26.0 and 0.99.0 as all major=0 and therefore mutually compatible. SemVer 2.0 is explicit that anything MAY change at any time in 0.x. The install tree shipped today therefore promises ABI compatibility that the project own version policy denies. The right policy for a 0.x line is SameMinorVersion (0.25.x only), or even ExactVersion. If the project ships a v0.26.0 that breaks a struct layout in cd::core::ErrorCode, downstream find_package(CHROMODYNAMIC 0.25 REQUIRED) will still succeed and the consumer will get an ODR violation at link time. This bites the moment the engine has more than one downstream.
- **Suggested fix:** Change the policy in the install Config to SameMinorVersion until v1.0 ships; on v1.0 switch to SameMajorVersion (which is then semantically correct). Document the change in CHANGELOG.

### B5. The no-catch-ellipsis rule (ARCHITECTURE.md section 3.1) is violated in the foundation tier

- **Where:** engine/foundation/log/include/cd/log/ILogger.hpp:62, engine/foundation/profile/include/cd/profile/CsvSink.hpp:77, engine/foundation/profile/include/cd/profile/ChromeTraceSink.hpp:90, engine/foundation/concurrency/include/cd/concurrency/JobGraph.hpp:100, engine/foundation/concurrency/include/cd/concurrency/WorkStealingThreadPool.hpp:291, engine/foundation/concurrency/include/cd/concurrency/ThreadPool.hpp:278.
- **Why blocking (for the convention, not the code):** ARCHITECTURE.md section 3.1 says verbatim that catch-ellipsis with an empty body is forbidden. The actual code uses catch-ellipsis in six foundation-tier headers — five of them inside the **frozen** v1.0 ABI surface. None of them are silently empty (each records a counter or routes to a log call), so the *intent* of the rule is honored. But a reviewer who reads the convention literally and grep-checks it (which is what the brief explicitly asks me to do) gets six hits. Two responses are possible: rewrite the rule to match the practice (catch-ellipsis is allowed only when it observably records the failure), or remove the catches and let exceptions propagate to a documented termination boundary. Whichever, the doc and the code must agree before v1.0 freezes them.
- **Suggested fix:** Edit ARCHITECTURE.md section 3.1 to: Empty catch-ellipsis handler is forbidden. catch-ellipsis is permitted only when it observably records or routes the failure (counter, log line, stat bump) and the file is documented in docs/EXCEPTION_BOUNDARIES.md. Then add that doc.

## What I think is unclear / smells (non-blocking)

- **README sample count is 39 in prose, 23 in the sample table** (Readme.md:25 says Sample count: 39, but the table at Readme.md:183-207 lists 23 rows). samples/ on disk has 41 entries, 39 of which are hello_*. The README table needs to be regenerated from disk; the prose is correct.
- **CVarRegistry::set fires callbacks *outside* the lock by walking vars_.find(key) *without* the lock held** (engine/foundation/core/include/cd/core/CVar.hpp:51-67). A concurrent erase(key) between releasing the unique_lock and the post-lock vars_.find would dereference a stale iterator. The intent (fire callbacks outside the lock to avoid re-entrancy deadlocks) is right; the implementation needs to capture the value-by-copy under the lock and not re-look-it-up afterwards. Low-likelihood but real.
- **Handle::generation_type is uint16_t (Handle.hpp:47)** and HandleStore rolls it at overflow by skipping back to 2 (HandleStore.hpp:155-159). 16-bit generation means a slot that churns insert/erase 32768 times wraps. For long-running game sessions with churning particle handles or transient render-target views this is plausibly hittable. The header notes extremely rare but does not document the wrap behavior — a downstream consumer using a stored stale handle right at the wrap point will get a *false positive* is_live match. Either widen to uint32_t (shrinking kIndexMask to 0x00FFFFFFu, still 16M slots) or document the wrap semantics and the maximum churn rate per slot.
- **cd::log::ILogger::log (ILogger.hpp:50-66) catches a format error and re-logs an Error at the same source location.** That is clever, but log_impl(LogLevel::Error, &loc, Log-format-error) could itself throw (the implementation is virtual and not noexcept), creating an infinite catch-relog loop until the stack runs out. The double-fault path needs a noexcept escape hatch.
- **CONSUMING.md status banner says 47 of the 55 libraries but counts 8 exclusions (47+8 != 55).** Re-reading, the parenthetical lists 7 cd::* libraries plus sample-only cd::sample_common, so the math is actually 47+8 = 55 only if sample_common counts toward the total. But sample_common is described as sample-only — i.e. probably not in the headline 55 the README boasts. Either the headline 55 is wrong or the install math is. Pick one.
- **cd::core::Version::tag is std::string_view{} always** — empty, never populated by the build. The tag semantics (alpha, rc1) never round-trip through the version macros. If the rationale is we-do-not-ship-release-tags-pre-1.0, say so; otherwise wire CMake PROJECT_VERSION_TWEAK or a custom macro.
- **examples/consuming/CMakeLists.txt:38-42 (the reference downstream)** validates that cd::core / cd::diag / cd::log / cd::imgdiff / cd::asset_image import. But docs/CONSUMING.md:69-79 shows a target_link_libraries with **cd::math, cd::asset, cd::ecs, cd::scene** in addition — a much wider link. The two documents are inconsistent about what the consumer should link. Either the docs example needs to broaden or the prose needs to narrow.
- **The 55 libraries headline aggregates real ABI-bearing libraries with explicitly experimental stubs.** cd::rhi_d3d12 and cd::rhi_metal are described in LIBRARIES.md section Render as Add a cd::rhi_d3d12 or cd::rhi_metal here later without touching consumers — i.e. they exist as targets but the backends are skeletons. v1.0 cannot promise ABI stability for skeletons. The frozen / experimental split needs to be authoritative in LIBRARIES.md, not just hinted at in ADR-wave133 section Out of scope.

## Answers to the section 4 questions

### 1. Names

Three picks from the foundation public surface:

- **Result<T> (Result.hpp:42)** — says exactly what it does once you have seen it; a thin alias for std::expected<T, ErrorCode>. Names match contents. The fail(...) helper (Result.hpp:45-54) is well-named and saves consumers from writing std::unexpected(core_errors::make(...)) six times a day.
- **ErrorCode::rewrap (ErrorCode.hpp:94-109)** — the name *almost* misleads. Rewrap sounds like RAII semantics (re-acquire ownership); what it actually does is *re-tag* (change domain+code, preserve message). I would rename to retag or with_tag. The doc comment is clear, but a reader scanning the header alone would guess wrong.
- **HandleStore::contains (HandleStore.hpp:137)** — perfect; returns true iff the handle still points to a live slot. Matches std::set::contains intuition.

### 2. Error story

The make_owning / rewrap split is **clear from the header alone after one reading** (ErrorCode.hpp:69-109). I did not need to read ADR-wave125 to understand it. The cliff was originally the non-owning string_view capture of a temporary — the migrated design solves it without making the common-path constexpr literal pay an allocation. The one thing the header does not say explicitly is the *invariant*: if owned != nullptr, then message == std::string_view{*owned}. The reader has to infer it from the factory. Worth a single sentence in the header noting that copying preserves the invariant via shared_ptr refcount.

### 3. Handle lifetime

The contract for stale-handle is Handle::is_valid != HandleStore::contains — a non-null Handle{} whose generation is one increment behind the current slot generation will return is_valid() == true but contains() == false. The header documents is_valid as value_ != 0 but does *not* document that a valid handle can still be stale. A downstream consumer is going to call is_valid on a saved handle, get true, deref via .get(), and get back nullptr. The right [[nodiscard]] UX is to use auto* p = store.get(h) and branch on p; the header is structured to encourage that. But the names are confusing: is_valid() should probably be is_non_null() to leave room for valid-AND-alive-in-store-X. Otherwise a reader without the ADR will use if (h.is_valid()) as an aliveness check, which it is not.

### 4. noexcept honesty

cd::diag::panic not-noexcept (Assert.hpp:82-87): **I buy it.** The reasoning — a throwing handler must propagate, declaring noexcept would force std::terminate — is correct. The [[noreturn]] attribute keeps the optimizer happy. The header is honest. The single follow-up is that cd::log::detail::panic_dump_handler is declared noexcept (PanicDump.hpp:37), but it calls std::fprintf — fprintf is not noexcept in libc++ in all configurations (it can throw on some PMR-allocator paths). Practically fine; pedantically the noexcept claim is slightly hopeful.

### 5. DAG sanity

I grepped each tier for imports from above:
- engine/foundation/ -> only doc-comment mentions of cd::rhi / cd::asset_json, no actual deps. **Clean.**
- engine/asset/ -> no cd::render / cd::world / cd::runtime / cd::ui references. **Clean.**
- engine/render/ -> no cd::world / cd::runtime / cd::ui / cd::asset references. **Clean.**

The DAG holds in practice. The configure-time check in cd_add_library (mentioned in ARCHITECTURE.md:75-76) means new violations would be caught immediately. The one risk: the check is in CMake, so a header-only consumer that includes cd/render/... from inside cd/foundation/ would compile without tripping the check (CMake only sees target_link_libraries, not #include). A pre-commit hook that greps for upward #include cd/<above-tier>/ would close that hole.

### 6. Frozen vs. experimental

Two libraries I would move out of the frozen v1.0 surface even if everything else lands:

- **cd::rhi_d3d12 and cd::rhi_metal** — explicitly skeleton backends per LIBRARIES.md, but they are in the install tree (per CONSUMING.md section 3) as find_package-able. A frozen ABI promise on a skeleton is a promise to never re-architect the skeleton. They should be in the EXCLUDE_FROM_INSTALL set until they actually render a frame.
- **cd::physics** (LIBRARIES.md World tier: Broad-phase / narrow-phase primitives (**Jolt integration deferred**)) — same story. The public surface is going to change when Jolt lands. Do not freeze it now.

Conversely, cd::shader is currently EXCLUDED but is the natural public companion for cd::material (also excluded) — those two should re-enter together once the vendored-dep wrappers land. They are a coherent pair.

### 7. Layer breaks

I picked the no-catch-ellipsis rule (ARCHITECTURE.md section 3.1) and grep-checked. Six hits in foundation tier (see B5 above). Whether you call them violations or implicit-exceptions-to-the-rule depends on whether you treat catch-ellipsis with a payload as the same as catch-ellipsis with an empty body. The doc is currently written to ban the empty-body construct verbatim; the code uses the construct with a payload. Pick one and align.

I also picked Result-on-every-fallible-boundary-no-exceptions-on-hot-paths — and confirmed it: engine/foundation/ shows throw only inside cd::mem::PmrAdapter (where the PMR contract requires it) and inside tests. The hot path is clean. This rule is honored.

### 8. Is the 4-axis gate the right gate?

Mostly yes, with one missing axis and one over-specified axis:

- **Missing axis E — Documentation / generated artifacts match the code surface.** The bugs I found in this review (B1: phantom cd::serialization, B2: case-mismatched paths, doc/code count disagreement, version-policy lax) are all in the docs, not the code. v1.0 promises ABI stability *and* documentation of that ABI. There is no axis in the current gate that says the-published-docs-match-the-install-tree-symbol-by-symbol. A cmake --install then scrape pass that diffs the install tree against LIBRARIES.md would close this — and would have caught B1 / the cd::serialization phantom.
- **Axis B (cross-vendor GPU) is the strongest axis** as-defined — the lavapipe CI job + the Intel iGPU validation is exactly the right shape. The second-real-vendor requirement is correct.
- **Axis C (downstream consumer) needs sharpening.** A-second-repository-builds-against-the-v0.25.0-install-tree is permissive enough that the marathon author could spin up a 50-line repo and call it shipped. Concretize: the downstream must be >= 500 LOC across at least 3 cd::* libraries beyond the foundation tier and must run a Vulkan frame, in a public repo, before v1.0 closes.

### 10. Track C — should v1.0 ship without cd::rhi_vulkan and cd::shader find_package-able?

**No, that is a v1.0 blocker for a game-engine framing.** A framework whose installer cannot give a downstream find_package-access to the only working RHI backend is not a v1.0 of a game engine; it is a v1.0 of a math + asset + ECS toolkit. The marketing in Readme.md (CHROMODYNAMIC is a cross-platform, cross-API hybrid 2D+3D game engine) commits the project to shipping a rendering pipeline that downstream can consume without add_subdirectory. The vendored-dep wrapper work (volk / VMA / glslang installable as transitive deps) needs to land before v1.0, or the v1.0 framing needs to soften to rendering-framework-toolkit-GPU-backend-via-submodule. The current CONSUMING.md build-from-source-via-add_subdirectory workaround for the excluded 7 libraries is fine for v0.x; it is not v1.0 quality for a framework that ships a Vulkan backend.

### 12. If I had to ship a real game on this engine tomorrow, what would I refuse without?

In order:

1. **cd::rhi_vulkan installable**, so my game CMakeLists.txt does not fork the engine build.
2. **cd::shader installable**, ditto, plus a documented runtime shader-compile cache location.
3. **A real cd::ui text-rendering API**, or a documented the-engine-has-no-text-yet-integrate-Dear-ImGui-directly. LIBRARIES.md is vague.
4. **cd::physics either real or marked deferred-to-v1.x.** A broad-phase-narrow-phase-primitives-Jolt-integration-deferred promise is unworkable for a game.
5. **Documented hot-reload story for assets**, not just shaders. hello_hot_reload is shaders only; gameplay needs scene + texture + audio hot reload to be usable in iteration.

The foundation + asset + math story is genuinely good. The render-to-pixels path is real (cross-vendor validation tables in ARCHITECTURE.md are not faked). The world tier is the part where I would balk.

## Overall

| Axis | Did v0.25.0 close it for you? |
|---|---|
| A — Visual correctness | **partial** — the brief admits this; 5 of 14 windowed samples pixel-checked + golden gate wired but not enforced. The remaining 9 windowed samples are exit-0 only. ADR-wave133 Track A is the right plan; v0.25.0 is honestly not the closure of Axis A. |
| B — Cross-vendor GPU | **partial** — NVIDIA + Intel iGPU validated, lavapipe CI job wired and live, AMD + Apple/MoltenVK both n/a per ARCHITECTURE.md section 6. A second-real-vendor (Intel iGPU counts as that) is met; third desktop vendor is open. |
| C — External downstream | **no** — examples/consuming/ is internal smoke. The Track C acceptance a-second-repository-in-a-separate-repository-ships is unmet. CONSUMING.md is well-written and would *enable* an external consumer; the consumer itself does not yet exist. |
| D — Independent review | yes — this document. Acted on or deferred-to-v1.x is the project owner call. |

**Would you sign off on a v1.0 tag today?** **No.**

The four-axis gate is the right gate. Axis A is partially closed (acceptable as a v0.25 baseline, not as v1.0). Axis B is partially closed (one second vendor is met; the gate language reasonably says >= 2 vendors, so met). **Axis C is not started** — the brief says a-real-downstream-project-in-a-separate-repo-ships. None exists. Without C closed, v1.0 cannot ship.

Beyond the 4-axis gate, the five blocking items in section What-I-think-is-wrong are not v1.0-compatible: docs that name libraries that do not exist (B1), case-sensitivity landmines in the doc tree (B2), a known-dangling pointer in the Vulkan debug-label path (B3), an over-loose CMake version policy that voids the ABI promise (B4), and a foundation-tier rule the foundation tier itself does not honor (B5).

**If no, the smallest concrete list that would change your mind:**

1. **Either ship cd::serialization or remove every mention of it from README / ARCHITECTURE / LIBRARIES.** Then regenerate LIBRARIES.md from engine/*/CMakeLists.txt so library count and tier sub-totals match the on-disk reality.
2. **Lowercase every Engine/ reference in docs/*.md (including REVIEW_BRIEF.md).** Verify by grepping the docs tree for capital-E Engine and getting nothing.
3. **Fix VulkanCommandBuffer::push_debug_group** to use a per-command-buffer label arena (or store the std::string in a per-CB vector) so the pLabelName pointer outlives the command-buffer-execution lifetime the Vulkan spec implies.
4. **Change the install tree version policy from SameMajorVersion to SameMinorVersion** for the duration of the 0.x line. Flip it to SameMajorVersion on the v1.0 tag (where it is then semantically correct).
5. **Rewrite the no-catch-ellipsis rule in ARCHITECTURE.md section 3.1** to match the practice (catch-ellipsis permitted only if observably routed; empty handler is forbidden), OR remove the six catches and let the documented exception boundary do the work.
6. **Close Axis C with a real external downstream** — concrete: a public-repo project, >= 500 LOC, linking >= 3 non-foundation cd::* libraries, running a Vulkan frame end-to-end against find_package(CHROMODYNAMIC 0.25). This is the actual gate from ADR-wave133; without it Axis C is not closed.
7. **Close Axis A** by capturing golden references for the remaining 9 windowed samples (per ADR-wave133 Track A sequencing) and enabling the FLIP/SSIM CI gate. The framework is wired; the references are missing.
8. **Add a sixth axis E (documentation parity)** to the gate, with a CI check that diffs the install tree exported targets against LIBRARIES.md. This is the gate that would have caught B1 on the first commit and saved this review the embarrassment of finding a fictional library.

When items 1-5 land and items 6-7 produce real artifacts, I will gladly re-review. The bones are good — the marathon really did build a coherent library-oriented engine — and the rollback discipline is genuinely admirable. The remaining gap is the difference between we-built-it and we-can-stand-behind-a-v1.0-promise-about-it. That gap is closeable in weeks, not months.
