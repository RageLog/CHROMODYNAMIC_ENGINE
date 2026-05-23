# ADR — Wave 156 — Phase 14 plan

- Date: 2026-05-23
- Status: Accepted
- Wave: 156
- Predecessor: ADR-20260523-wave151 (Phase 13 master, closed at
  v0.33.0 / Wave 155)

## Context

Phase 13 shipped four sub-phases (v0.30 → v0.33) that closed every
reviewer blocker and added two new working capabilities (D3D12
render path, editor ImGui driver). The wave151 plan-ADR closed
with a Phase-14 candidate list:

- Asset cooker improvements (BC7 quality, KTX2 mip gen)
- Networking — actual reliable channel + replication
- Audio — proper DSP graph + 3D positional via WASAPI
- Mobile platforms (Android Vulkan / iOS Metal)
- Real RT render path (BLAS/TLAS through cd::rhi)
- D3D12 PSO + draw surface
- Editor 3D viewport + dock-layout preset
- Scene serialization

User's marathon directive (memory: feedback_long_autonomous_marathon):
work all of them, ordering general → specific, easy → hard, fast →
slow.

## Decision

Phase 14 opens with the eight sub-phases below, sequenced by
AI-actionability (easy / fast / no-human-pixel-review-needed first,
risky-and-driver-dependent last).

### 14.A — Scene serialization → v0.34.0

**Why first:** pure code, headless-testable, uses already-existing
infrastructure (cd::asset_json + Scene API + ECS World). No visual
review needed.

- `cd::scene::save_to_json(const Scene&, const World&) → Result<std::string>`
- `cd::scene::load_from_json(std::string_view, Scene&, World&) → Result<>`
- A test fixture that round-trips a 3-entity scene with a known
  hierarchy + transforms and asserts byte-equal JSON re-emission.
- hello_scene_save sample.

### 14.B — Asset cooker improvements → v0.35.0

**Why second:** unit-testable cooker logic, headless, well-bounded
scope.

- BC7 cooker quality knob — currently the BC7 path uses a fast
  encoder; add a `--quality {fast,balanced,best}` flag and
  validate via PSNR vs. the reference uncooked PNG.
- KTX2 mip-chain generation in cd::asset_cdtex's cook path so
  texture assets ship with mips baked in instead of relying on
  GPU-side `vkCmdGenerateMipmap` at load time.

### 14.C — D3D12 PSO + draw surface → v0.36.0

**Why third:** the natural follow-up to Phase 13.C; the
buffer/texture/swapchain are already in place, the missing
piece is the PSO + draw + DXIL compile.

- HLSL → DXIL compile via `D3DCompile2` (or the newer DXC if
  vendored).
- `D3D12Device::create_graphics_pipeline` real impl.
- `D3D12Device::create_shader_module` reading DXIL bytecode.
- Trivial root signature builder for the MVP pipeline layout.
- `D3D12CommandBuffer::bind_graphics_pipeline` /
  `set_viewport` / `set_scissor` / `draw` /
  `bind_vertex_buffer` / `bind_index_buffer`.
- `samples/hello_d3d12_triangle` — same triangle hello_triangle
  draws via Vulkan, now via D3D12.

### 14.D — Networking reliable channel + replication → v0.37.0

**Why fourth:** loopback-testable; the wire format is the design,
the actual transport is already abstracted behind `ITransport`.

- A reliable-ordered channel built on top of the existing
  unreliable UDP transport (sequence numbers + ACK timer +
  retransmit window).
- A toy replication test: spawn two World instances, replicate
  3 LocalTransform mutations from one to the other, assert
  byte-equal post-state.
- hello_replication sample (two ECS Worlds in the same process
  for the marathon-shippable cut; cross-process belongs in a
  later iteration).

### 14.E — Editor 3D viewport + dock-layout preset → v0.38.0

**Why fifth:** brings a real Renderer-driven mesh draw into
hello_editor; visual but the bar is "is anything drawn?" which is
golden-image testable.

- Draw the three entities (Cube/Sphere/Cone) as primitive meshes
  inside the editor's main panel via the existing Renderer +
  cd::rhi pipeline.
- Free-fly camera (WASD + mouse-look) so the user can see the
  effect of inspector edits.
- A default ImGui dock-layout preset that positions Scene /
  Inspector / Toolbar / History around the viewport.
- A golden-image baseline for the editor's default-camera view
  of the three identity-transformed primitives.

### 14.F — Audio DSP graph + 3D positional polish → v0.39.0

**Why sixth:** audio gates need human-ear validation, which the
marathon cannot provide. The wave149 audio path (WASAPI) is real;
the DSP graph is the SOTA-quality direction.

- Replace the per-voice flat mixer with a directed-acyclic-graph
  of DSP nodes (gain, biquad LP/HP, distance attenuation,
  HRTF stub).
- 3D positional uses inverse-square + cone attenuation as
  documented in ADR-007.
- Headless unit test: build a 3-node graph, push N samples
  through, assert output buffer matches a fixture.
- Marathon honesty: ship the DSP graph without
  per-listener-equipment HRTF calibration — that needs human
  iteration.

### 14.G — Real RT render path → v0.40.0

**Why seventh:** Phase 12.D shipped the *detection* path
(`features().ray_tracing`); the actual pipeline is its own large
engineering effort and gates need cross-driver validation.

- BLAS/TLAS construction through `cd::rhi`
- `dispatch_rays` surface on ICommandBuffer
- Minimal raygen shader sample (hit-color-by-instance-id).
- Driver-validation table per ARCHITECTURE.md §6 protocol —
  NVIDIA RTX 3080 minimum; iGPU/lavapipe rejection is expected
  and documented.

### 14.H — Mobile platforms → v0.41.0

**Why last:** requires actual device or emulator; CI runs are
GitHub-hosted Linux/Windows/macOS. Mobile is the
infrastructure-heaviest item on the list.

- Android Vulkan: cd::platform Android window surface, gradle
  build wrapper, ARM64 cross-compile.
- iOS Metal: cd::platform UIView surface, Xcode project
  generation, ARM64 cross-compile.
- A `hello_triangle_mobile` sample for each platform.

## Rejected alternatives

- *Skip the easy items, jump to RT.* Rejected: the wave151 plan
  ordering rule (general → specific, easy → hard) was the user's
  directive. Scene serialization is the kind of thing that
  silently saves N future debugging sessions; sequencing it
  ahead of the visual work is the right discipline.
- *Open all eight in parallel.* Rejected: same reasoning as the
  Phase 13 plan — sequential is the marathon-quality bar.
- *Add a Phase 14.I "v1.0 prep" sub-phase at the end.* Rejected:
  v1.0 is user-only per the rollback ADR. Phase 14 ships
  capability; v1.0 promotion is a separate explicit gate.

## Consequences

- Phase 14 runs autonomously per the marathon directive. Stop
  condition per sub-phase: minor tag cut + ADR.
- The Phase-15 candidate list at the close of Phase 14 will
  depend on what fell out — 14.G's RT and 14.H's mobile may
  themselves spawn dependency follow-ups.
- v1.0 path is still user-only. Phase 14 does NOT promote to
  v1.0 even if all 8 axes feel met.

## References

- ADR-20260523-wave151-phase13-plan.md — Phase 13 master
- ADR-20260523-wave152..155 — Phase 13 sub-phases v0.30 →
  v0.33
- ADR-20260523-wave125-v1.0-rollback.md — the four-axis gate
  this phase pushes against (capability axis specifically)
