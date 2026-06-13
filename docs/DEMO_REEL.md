# CHROMODYNAMIC — Demo Reel

> Phase 1–100 marathon output, presented as runnable demos.
> Branch: `dev` · Version: v0.99.33 · 49 samples · 100 phases

The marathon shipped 100 phases of header-only primitives + RHI integration
across the engine's foundation, render, world, ui, asset, runtime, and
script layers. This guide groups every sample into a category and tells
you exactly what command runs it and what to look for.

For Windows / Git-Bash. All paths assume you've configured + built
`llvm-win-debug` (or any other preset). The binary location used below
is the LLVM-Clang Windows preset:

```
build/llvm-win-base/bin/Debug/
```

Substitute `clangcl-win-base`, `msvc-base`, `vs2026-base` if you built
with another toolchain.

---

## 1. Visual demos (Vulkan, opens a window)

Each of these creates a 1280×720 window, runs a real Vulkan render loop,
and exits on ESC. They all accept `--headless N` to run N frames and
exit (CI / smoke). Add `--golden-capture <path.png>` to save the last
frame.

| Sample                          | What you see                                                  | Phase  |
|---------------------------------|---------------------------------------------------------------|--------|
| `hello_triangle`                | The canonical first Vulkan triangle.                         | 4      |
| `hello_mesh`                    | OBJ mesh loaded + rendered with proper normals.              | 6      |
| `hello_texture`                 | Textured quad, full UV sampling pipeline.                    | 6      |
| `hello_obj`                     | OBJ importer end-to-end with normals + UV.                   | 7      |
| `hello_gltf`                    | glTF 2.0 import → cdmesh → draw.                             | 7      |
| `hello_cooked`                  | Asset cooked into `.cdmesh`, loaded back, drawn.             | 8      |
| `hello_textured_cooked`         | Same, but with `.cdtex` (KTX2/BC7) sampling.                 | 8      |
<!-- hello_anim removed phase1172 — SAFE-DELETE: cd::anim coverage via test_anim.cpp 55 unit cases + test_dual_quat.cpp; capability superset HelloSkinnedAnim in hello_engine (real CesiumMan skinning, not keyframe-cube) -->
| `hello_scene_graph`             | Scene-graph traversal, parent→child transforms.              | 11     |
<!-- hello_hot_reload removed phase1157 — covered by hello_engine ShaderWatch panel -->
| `hello_imgui`                   | ImGui integration (Phase 17.A), draw + interact.             | 17     |
| `hello_inspector`               | Editor-style inspector window over a live scene.             | 17.B   |
| `hello_editor`                  | EditHistory + TransformCommands wired into ImGui inspector.  | 18     |

**Run examples:**
```bash
# Interactive (ESC to exit):
./build/llvm-win-base/bin/Debug/hello_triangle.exe

# Golden capture (deterministic):
./build/llvm-win-base/bin/Debug/hello_triangle.exe \
    --headless 3 --golden-capture tri.png
```

---

## 2. Headless console demos — engine primitives

These print results to stdout, do not open a window, and exit < 5 s.
Great for showing the marathon's library primitives at work.

### Foundation / Concurrency

| Sample              | What it demonstrates                                                 |
|---------------------|----------------------------------------------------------------------|
| `hello_core`        | `cd::core::Result<T,Error>` round-trip; ErrorCode lifecycle.         |
| `hello_handle`      | Generational-index handle pool (create → free → recycle).            |
| `hello_foundation`  | Foundation-layer compile-time + runtime assertions.                  |
| `hello_runtime`     | Subsystem boot + shutdown, IRuntimeService wiring.                   |
| `hello_scheduler`   | Job system fork-join, multi-thread task scheduling.                  |
| `hello_bench`       | Microbenchmark harness (statistical + per-op timing).                |

### ECS / Scene

| Sample              | What it demonstrates                                                 |
|---------------------|----------------------------------------------------------------------|
| `hello_ecs`         | Archetype ECS — create entities, attach components, query views.     |
| `hello_scene_graph` | Scene-graph world transforms, dirty propagation.                     |
| `hello_scene_save`  | Scene serialize → JSON → reload → compare round-trip.                |

### Asset

| Sample              | What it demonstrates                                                 |
|---------------------|----------------------------------------------------------------------|
| `hello_json`        | Streaming JSON parser, schema-free + schema-validated paths.         |
| `hello_asset_registry` | AssetId → AssetRecord registry, ref-count, hot-reload hook.       |
| `hello_audio_synth` | Sine → WAV → cd::asset_wav reload, bit-identical roundtrip check.    |

### Audio (NEW — Phase 100 reel)

| Sample              | What it demonstrates                                                 |
|---------------------|----------------------------------------------------------------------|
| `hello_audio_chain` | **Mixer → Compressor → Reverb → LowPass → Limiter chain.** Writes  |
|                     | `hello_audio_chain_out.wav` (1.5 s, 48 kHz mono). Prints comp gain  |
|                     | reduction in dB, limiter activity, peak amplitude.                   |
| `hello_audio_play`  | Plays a buffer through the Null backend (no speakers required).     |
| `hello_audio_wasapi`| WASAPI session open → device enum → format negotiation.              |

### Network (NEW — Phase 100 reel)

| Sample              | What it demonstrates                                                 |
|---------------------|----------------------------------------------------------------------|
| `hello_net_sim`     | **SnapshotBuffer + DeltaWriter + LatencyStats + Throttle.** 2 s of  |
|                     | simulated client-server replication through a lossy/jittery channel.|
|                     | Per-second table: pkts sent/recv/dropped, raw vs. delta bytes, RTT  |
|                     | EWMA + jitter, interpolation lookup count.                          |
| `hello_udp`         | Real UDP loopback (winsock / posix sockets).                         |

### Editor (NEW — Phase 100 reel)

| Sample                  | What it demonstrates                                             |
|-------------------------|------------------------------------------------------------------|
| `hello_command_palette` | **VS-Code-style fuzzy command palette.** Registers 34 commands, |
|                         | runs queries like `"save"`, `"trs"`, `"tog"`, `"rdc"`, prints   |
|                         | ranked top-5 results showing FZF subsequence scoring.            |

### Misc

| Sample              | What it demonstrates                                                 |
|---------------------|----------------------------------------------------------------------|
| `hello_events`      | EventBus pub/sub typed event dispatch.                               |
| `hello_watch`       | File-watcher firing on touched file.                                 |
| `hello_flip`        | FLIP image-difference scoring (perceptual diff).                     |
| `hello_script`      | Embedded scripting host bridging C++ ↔ script callbacks.             |
| `hello_replication` | Loopback replication of a tiny world.                                |
| `hello_rt_check`    | Vulkan ray-tracing extension probe (does NOT dispatch rays yet).     |
| `hello_rhi_features`| Per-backend capability probe (queue families, formats, extensions).  |

---

## 3. Recommended demo reel order (10 minutes)

For a quick "show me what the marathon did" walkthrough:

1. **`hello_command_palette`** — 1 s, prints registry + fuzzy queries.
2. **`hello_net_sim`** — 3 s, prints the per-second replication table.
3. **`hello_audio_chain`** — 2 s, writes a WAV you can play.
4. **`hello_editor`** — interactive, ImGui inspector with undo/redo.

```bash
DBG=./build/llvm-win-base/bin/Debug
$DBG/hello_command_palette.exe
$DBG/hello_net_sim.exe
$DBG/hello_audio_chain.exe
# Then the visual ones (close each window with ESC to advance):
$DBG/hello_editor.exe
```

---

## 4. Build everything from scratch

```bash
cmake --preset llvm-win-debug                                    # configure
cmake --build --preset llvm-win-debug --target samples -j2       # build all samples
ctest --preset llvm-win-debug --output-on-failure                # run engine tests
```

Substitute `clangcl-win-debug`, `msvc-debug`, `vs2026-debug` for other
toolchains. `-j2` keeps RAM pressure low on clang-cl — drop to `-j1`
if the linker reports "paging file too small".

---

## 5. Sample → marathon primitive cross-reference

A practical cheat-sheet for which sample exercises which subsystem.
"Primitive" here means the header-only class added during one of the
100 marathon phases.

| Subsystem  | Primary samples                              | Notable primitives exercised                  |
|------------|----------------------------------------------|-----------------------------------------------|
| core       | hello_core, hello_handle, hello_foundation   | Result, ErrorCode, Handle, RingBuffer, Hash   |
| concurrency| hello_scheduler                              | Job, Latch, Barrier, MPSC Channel, Flag       |
| math       | hello_engine (HelloMath probe)               | Mat4, Quat, AABB, Frustum, SphericalHarmonics |
| ecs        | hello_ecs, hello_scene_graph                 | Entity, Archetype, View, SystemGraph          |
| scene      | hello_scene_graph, hello_scene_save          | TransformGraph, TagBucket, SceneStats         |
| rhi        | hello_triangle, hello_rhi_features           | IDevice, ICommandBuffer, Pipeline, Barriers   |
| render     | (retired phase1144 → test_framegraph_vulkan) | Renderer, FrameGraph, SortKey, IBL, PBR mat   |
| asset      | hello_gltf, hello_obj, hello_cooked          | AssetRegistry, cdmesh, cdtex, AssetRefCount   |
| audio      | hello_audio_chain, hello_audio_synth         | Mixer, Compressor, Limiter, SimpleReverb, LowPass |
| net        | hello_net_sim, hello_udp                     | SnapshotBuffer, DeltaWriter, LatencyStats, Throttle, SequenceWindow |
| editor     | hello_command_palette, hello_editor          | CommandPalette, EditHistory, TransformCommands, HierarchyView |
| input      | hello_editor                                 | KeyChord, DoubleClick, MouseDragState         |

---

*End of demo reel. See `docs/MARATHON_SUMMARY.md` for the full phase-by-phase changelog.*
