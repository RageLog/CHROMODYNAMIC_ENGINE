# Frame Graph SOTA Comparison

**Accessed:** 2026-05-22
**Compiled by:** researcher subagent (auto-pulled from primary sources)

## Engines Surveyed

| Engine / System | Year | License | Lang | Backend |
|---|---|---|---|---|
| Frostbite FrameGraph | 2017 | Proprietary | C++ | DX11/12, PS4, Xbox One |
| Granite (Themaister) | 2017–present | MIT | C++ | Vulkan |
| Unreal RDG | 2018–present | Custom EULA | C++ | DX11/12, Vulkan, Metal |
| AMD RPS SDK | 2022–present | MIT | C++ + RPSL | DX12, Vulkan |
| skaarj1989/FrameGraph | 2021–present | MIT | C++ | Renderer-agnostic |
| bgfx | 2012–present | BSD-2 | C++ | Many |
| The Forge | 2018–present | Apache-2.0 | C++ | DX12, Vulkan, Metal, Console |
| Sokol (sokol_gfx.h) | 2017–present | zlib | C | Many |

---

## Pass Declaration Patterns

### Frostbite FrameGraph (O'Donnell, GDC 2017)

Source: [SlideShare — FrameGraph: Extensible Rendering Architecture in Frostbite](https://www.slideshare.net/slideshow/framegraph-extensible-rendering-architecture-in-frostbite/72795495)

Three-phase model: **Setup → Compile → Execute**. Each pass is registered with two lambdas — a setup callback that declares resource accesses through a `RenderPassBuilder`, and an execute callback that receives an `IRenderContext*` for immediate-mode draw calls. The graph is rebuilt from scratch every frame. No base class to implement.

```cpp
// Pseudocode reconstructed from GDC 2017 slides
frameGraph.addCallbackPass<GBufferData>("GBuffer",
  [&](RenderPassBuilder& builder, GBufferData& data) {
    data.albedo = builder.write(builder.create<Texture2D>("Albedo", {fmt::RGBA8}));
    data.depth  = builder.write(builder.create<Texture2D>("Depth", {fmt::D32}));
  },
  [=](const GBufferData& data, RenderResources& res, IRenderContext* ctx) {
    // draw calls here
  });
```

Modules communicate through a `Blackboard` — a typed hash table. A WorldRenderer that previously required 15,000 SLOC was reduced to 5,000 SLOC by delegating to self-contained modules communicating via the blackboard.

### Granite — Themaister (Arntzen, 2017)

Source: [themaister.net — Render graphs and Vulkan, a deep dive](https://themaister.net/blog/2017/08/15/render-graphs-and-vulkan-a-deep-dive/) and [github.com/Themaister/Granite](https://github.com/Themaister/Granite)

`graph.add_pass(name, VK_PIPELINE_STAGE_*)` returns a `RenderPass&`. Resource inputs and outputs are declared via typed methods: `add_color_output`, `add_texture_input`, `add_storage_output`, `set_depth_stencil_output`. Resources are identified by **string names** at declaration; handles are returned. Pass authors see Vulkan pipeline stage flags directly.

```cpp
auto& pass = graph.add_pass("gbuffer", VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT);
auto albedo = pass.add_color_output("albedo", att_info);
pass.set_depth_stencil_output("depth", att_info);
pass.set_build_render_pass([=](Vulkan::CommandBuffer& cmd) {
    render_scene(cmd);
});
```

### Unreal RDG

Source: [dev.epicgames.com — Render Dependency Graph in Unreal Engine](https://dev.epicgames.com/documentation/en-us/unreal-engine/render-dependency-graph-in-unreal-engine)

`FRDGBuilder::AddPass(name, parameters, flags, lambda)`. Resources created with typed descriptor structs (`FRDGTextureDesc::Create2D`). Dependency edges are **inferred automatically** from the parameter struct via compile-time macro reflection (`SHADER_PARAMETER_RDG_TEXTURE`, etc.) — no explicit `builder.read()` call. Pass flags: `ERDGPassFlags::Raster`, `::Compute`, `::AsyncCompute`, `::Copy`.

### AMD RPS SDK — RPSL DSL

Source: [gpuopen.com/learn/rps-tutorial/rps-tutorial-part2](https://gpuopen.com/learn/rps-tutorial/rps-tutorial-part2/) and [gpuopen.com/rps](https://gpuopen.com/rps/)

RPSL is an HLSL superset. Resources are function parameters with access attributes (`[readwrite(rendertarget)]`, `[readonly(ps)]`). The C++ side calls `rpsProgramCreate()` and the runtime compiles RPSL to a DAG. Latest release: v1.1.1 (May 2024).

### skaarj1989/FrameGraph

Source: [github.com/skaarj1989/FrameGraph](https://github.com/skaarj1989/FrameGraph)

`fg.addCallbackPass<PassData>(name, setup_lambda, exec_lambda)`. Resources declared via `builder.create<T>()` returning typed `FrameGraphResource` handles. A `FrameGraphBlackboard` typed dictionary enables cross-module resource passing without string lookup at execution time. Renderer-agnostic — no API coupling.

### bgfx — NOT a frame graph

Source: [bkaradzic.github.io/bgfx/internals.html](https://bkaradzic.github.io/bgfx/internals.html)

bgfx uses **view IDs** (integer buckets) and 64-bit sort keys. No pass declaration, no dependency tracking, no barrier insertion, no resource aliasing. Draw call ordering is sort-based, not dependency-based. Instructive contrast: everything a frame graph does, bgfx leaves to the programmer.

### The Forge — Explicit barriers, no graph

Source: [github.com/ConfettiFX/The-Forge](https://github.com/ConfettiFX/The-Forge), [Issue #171](https://github.com/ConfettiFX/The-Forge/issues/171)

`cmdResourceBarrier(cmd, ...)` is called manually before every state transition. A GitHub issue requesting frame graph support was filed and received no maintainer response. The deliberate choice of explicit barriers gives maximum control at the cost of significant programmer burden.

---

## Resource and Lifetime Representation

| System | Token Type | Lifetime Scope | Import Mechanism |
|---|---|---|---|
| Frostbite | Typed handle from `builder.create<>()` | Frame-scoped | `RegisterExternalTexture()` |
| Granite | String at declare; `RenderResource*` handle | Frame-scoped default; `persistent=true` opt-in | Physical resource binding |
| Unreal RDG | `FRDGTexture*` / `FRDGBuffer*` pointer | Frame-scoped | `FRDGBuilder::RegisterExternal*` |
| AMD RPS | RPSL typed params / `create_tex2d()` | Transient / persistent / external | Entry function parameters |
| skaarj1989 | `FrameGraphResource` (opaque 32-bit) | Frame-scoped | `fg.import()` |

Both Frostbite and Unreal RDG explicitly decline to alias imported resources even when their lifetimes are fully knowable — a documented correctness-over-optimization choice.

---

## Compilation Cost and Frequency

### Per-frame full rebuild (Frostbite, UE RDG, skaarj1989)

Graph declared and compiled every frame. Compilation steps: backward reachability culling (from output handles), lifetime interval computation, resource aliasing pass, barrier generation. UE5 RDG parallelizes execute-lambda recording with one `FRHICommandList` per pass group joined before queue submission. Per-frame rebuild enables fully dynamic pass sets with no invalidation signals.

### Bake-once with per-frame reset (Granite)

`graph.bake()` runs once per topology change; `graph.reset()` each frame re-records into the cached structure. Bake steps: validation, dependency traversal, pass reordering (three scoring criteria: GPU overlap, tile-based subpass merge, compute/graphics interleave), resource aliasing, barrier generation, subpass merging. Arntzen acknowledges the reordering algorithm is "probably very suboptimal in CPU time, but it gets the job done" (blog post).

---

## Transient Aliasing

### Frostbite — platform-specific strategy (GDC 2017 slides)

| Platform | Strategy | Result |
|---|---|---|
| PS4 | Virtual memory aliasing | — |
| Xbox One | Physical page aliasing (ESRAM+DRAM) | 570 MB savings at 4K (1042→472 MB) |
| DX12 PC | Virtual memory + object pools | — |
| DX11 PC | Atomic linear allocator, no aliasing | — |

Aliasing barrier required between users; `DiscardResource` and metadata reinitialization (FMASK/CMASK/DCC on AMD hardware) is documented as a critical footgun.

### Granite

Aliasing determined by `physical_index` mapping. `build_physical_resources()` assigns multiple logical resources to the same physical slot when: dimensions + format + sample count match AND their first/last-use intervals do not overlap. Color input/output pairs within a single pass auto-alias. Storage images and history-tracked resources are excluded.

### Unreal RDG

Power-of-two bucketing for texture dimensions. `r.RDG.TransientAllocator` cvar gates the feature. Imports never aliased. Epic documents approximately 50% memory savings in typical use.

### Honest assessment

The Xbox One ESRAM figure (570 MB) is an extreme console-specific case. On PC Vulkan with a single DEVICE_LOCAL heap, realistic savings depend on pass count and resource sizes. All production systems apply conservative restrictions: imports excluded everywhere, storage images often excluded.

---

## Multi-Queue / Async Compute

| System | Async Compute | Mechanism | Genuine? |
|---|---|---|---|
| Frostbite | Yes | `builder.asyncComputeEnable(true)`; compiler inserts semaphore | Yes |
| Granite | Yes | Per-pass queue flag; separate semaphore tracking; external lock interface | Yes (most complete OSS) |
| Unreal RDG | Yes | `ERDGPassFlags::AsyncCompute`; compiler finds last producer, inserts fence | Yes (automatic fence) |
| AMD RPS | Declared | Architecture supports; details not verified beyond API flags | Partial confidence |
| skaarj1989 | No | Single-queue only | No |
| bgfx, Sokol | No | Single-queue abstraction | No |
| The Forge | Manual | Multiple queues; explicit barrier placement only | Programmer-managed |

---

## Debug and Introspection

| System | Primary Tool | Graph View | Lifetime View | DOT Export |
|---|---|---|---|---|
| Frostbite | Internal timeline viewer (GDC only) | Yes | Yes | Unknown |
| Granite | None built-in | No | No | No |
| Unreal RDG | RDG Insights | Yes | Yes (heap allocation overlap) | No (proprietary) |
| AMD RPS | RPSL Explorer + DAGPrintPhase | Yes (interactive) | Yes (heap timeline) | Yes (DOT) |
| skaarj1989 | None built-in | No | No | No |

Granite's lack of a visualization tool is the clearest weakness of an otherwise strong implementation. AMD's RPSL Explorer + DOT output is the current best-in-class for debug tooling.

---

## Critical Design Tensions

### Tension 1: Named String Handles vs. Typed C++ Tokens

Granite uses strings at declaration time; Frostbite returns typed builder handles; UE RDG uses `FRDGTexture*` pointer identity; skaarj1989 uses opaque 32-bit typed handles plus a typed Blackboard. The consensus hybrid: string at declaration for debug naming, typed handle (integer) for all runtime tracking.

### Tension 2: Fully Auto-Inferred Barriers vs. Explicit Stage Specification

UE RDG and RPSL infer barriers from parameter struct annotations. Granite requires explicit `VK_PIPELINE_STAGE_*` flags. Auto-inference is ergonomic; explicit flags allow precise multi-stage optimization.

### Tension 3: Per-Frame Full Rebuild vs. Baked Persistent Graph

Frostbite and UE RDG rebuild every frame. Granite bakes once and resets. For stable per-scene topology, baking wins on CPU cost. For dynamic feature toggles, per-frame wins on correctness.

### Tension 4: Aggressive Memory Aliasing vs. Conservative Correctness

All production systems restrict aliasing — only graph-owned transient resources qualify. UE RDG explicitly excludes imports even when lifetimes are knowable.

---

## Recommendations for CHROMODYNAMIC cd::framegraph

Engine constraints: Vulkan 1.3 dynamic rendering, handle-based RHI (`IBuffer`, `ITexture`), C++23, library-oriented (`chroma::framegraph`), DX12/Metal portable later.

**Pass declaration:** Lambda pair (setup + execute) following Frostbite pattern. A `PassBuilder` struct with `read()`, `write()`, `write_color()`, `write_depth()`, `dispatch_write()` methods returning typed handles. No virtual base class. Use `std::move_only_function` (C++23) for execute lambdas.

**Resource tokens:** Typed opaque handles (`FrameGraphTextureHandle`, `FrameGraphBufferHandle` — newtype wrappers over `uint32_t`). String names stored in a debug side-table indexed by handle ID.

**Compilation frequency:** Bake-once with per-frame reset (Granite model). Expose `invalidate()` for topology changes. Each frame calls `record(ICommandBuffer&)` into the cached structure.

**Barrier inference:** Semi-explicit — pass authors declare access type + resource type; engine infers `VkPipelineStageFlags2`/`VkAccessFlags2`. Use Vulkan 1.3 `VkMemoryBarrier2` throughout.

**Aliasing:** Conservative initial scope — transient only, no imports, no storage images. Greedy interval bin-packing against 256 MB DEVICE_LOCAL heap blocks.

**Dead-code elimination:** Backward reachability BFS from the swapchain output handle. Unmarked passes and transient resources dropped before allocation.

**Async compute:** Opt-in flag per pass (`PassFlags::AsyncCompute`). Compiler finds last graphics producer, inserts `VkSemaphore` signal/wait. Start conservative with `ALL_COMMANDS_BIT`.

**Debug:** `graph.export_dot(std::ostream&)` outputting Graphviz DOT with resource/pass node labels and edge annotations. Integrate with `cd::diag`.

**API portability:** Internal barrier representation uses engine `BarrierDesc` structs. The frame graph emits these; the RHI translates to Vulkan sync2 / DX12 / Metal as appropriate.

---

## Sources

- [GDC Vault — Frostbite FrameGraph](https://www.gdcvault.com/play/1024612/FrameGraph-Extensible-Rendering-Architecture-in)
- [SlideShare — Frostbite FrameGraph slides](https://www.slideshare.net/slideshow/framegraph-extensible-rendering-architecture-in-frostbite/72795495)
- [Themaister — Render graphs and Vulkan, a deep dive](https://themaister.net/blog/2017/08/15/render-graphs-and-vulkan-a-deep-dive/)
- [GitHub — Themaister/Granite](https://github.com/Themaister/Granite) — render_graph.{hpp,cpp}
- [Epic Dev — Render Dependency Graph](https://dev.epicgames.com/documentation/en-us/unreal-engine/render-dependency-graph-in-unreal-engine)
- [Epic Dev — AsyncCompute in UE](https://dev.epicgames.com/documentation/en-us/unreal-engine/asynccompute-in-unreal-engine)
- [AMD GPUOpen — RPS SDK](https://gpuopen.com/rps/)
- [GitHub — GPUOpen-LibrariesAndSDKs/RenderPipelineShaders](https://github.com/GPUOpen-LibrariesAndSDKs/RenderPipelineShaders)
- [GitHub — skaarj1989/FrameGraph](https://github.com/skaarj1989/FrameGraph)
- [bgfx internals](https://bkaradzic.github.io/bgfx/internals.html)
- [GitHub — ConfettiFX/The-Forge Issue #171](https://github.com/ConfettiFX/The-Forge/issues/171)
- [Riccardo Loggini — Render Graphs](https://logins.github.io/graphics/2021/05/31/RenderGraphs.html)
- [Stolecki — Frame Graph in production engines](https://stoleckipawel.dev/posts/frame-graph-production/)
- [Pavel Smejkal — Aliasing transient textures in DX12](https://pavelsmejkal.net/Posts/TransientResourceManagement)
- [Ponies & Light — Rendergraph implementation](https://poniesandlight.co.uk/reflect/island_rendergraph_1/)
