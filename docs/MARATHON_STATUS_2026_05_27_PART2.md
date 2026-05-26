# Marathon status — Part 2 (2026-05-26 → 2026-05-27)

Part 1 lives in MARATHON_STATUS_2026_05_27.md and covers v0.99.69
→ v0.99.86 (the priority gap list — 14/22 gaps closed). This Part 2
covers v0.99.87 → v0.99.105 — the v1.4 → v2.0 ladder pass.

## Ships landed (v0.99.87 → v0.99.105)

| Tag        | Commit  | Subject                                                  |
|------------|---------|----------------------------------------------------------|
| v0.99.87   | 2e9e231 | PBR: Hable tonemap + sun-decoupled IBL                   |
| v0.99.95   | 17bcb0d | FX wire-in: bloom + GTAO + SSR (inline approximations)   |
| v0.99.96   | a796509 | FX wire-in: motion blur + TAA + SMAA + DOF + HDR10       |
| v0.99.97   | a477f23 | FX wire-in: atmosphere + fog + clouds + light shafts     |
| v0.99.98   | 193a9e8 | FX wire-in: LTC-GGX + sheen/clearcoat + SSS + decals + particles |
| v0.99.99   | 1a929b7 | GI wire-in: ReSTIR DI/GI + DDGI + NRC                    |
| v0.99.100  | 6df2254 | Editor: pseudo-infinite procedural grid                  |
| v0.99.101  | f214641 | Editor: Inspector Rotation + Tint rows                   |
| v0.99.102  | c9af979 | Frame-graph + async_submit link wire-in                  |
| v0.99.103  | 3f9ebc2 | RHI parity status doc + 'RHI: Status' palette command    |
| v0.99.104  | 5bead72 | Physics + Script v1.9 plan + linkage                     |
| v0.99.105  | 5389bef | v2.0 PRODUCTION plan + hello_stress harness skeleton     |

12 commits / 12 patch tags in this part. Combined with Part 1's 18:
30 commits / 30 tags for the full marathon.

## v1.4 → v2.0 ladder disposition

### v1.4 day-ships (post-fx pipeline)
- Bloom, GTAO, SSR — inline FS approximations live behind palette
  toggles; true multi-mip / horizon-scan / ray-march compute paths
  scheduled for v1.7 frame-graph rework.
- Motion blur, TAA, SMAA, DOF, HDR10 — SMAA inline; rest queued for
  v1.7 (need history / velocity / depth buffers).
- Atmosphere, fog, clouds, light shafts — height fog + aerial
  perspective inline; clouds + light shafts queued for v1.7.
- LTC-GGX, SSS, sheen/clearcoat, decals, particles — libraries
  linked; full GPU dispatch v1.7 material-graph work.

### v1.5 GI track
- ReSTIR DI/GI, DDGI, NRC — libraries linked, settings/reservoirs
  reachable from main, palette toggles log queue status. CpuReference
  NRC tests already pass (test_nrc.cpp). Full RT compute dispatch is
  v1.7 frame-graph + accel-structure ship.

### v1.6 editor
- Pseudo-infinite procedural grid — landed (floor 80m -> 1000m + FS
  fade 60..200m). True screen-space procedural plane is v1.7.
- Inspector Rotation + Tint — landed with EditHistory wiring on
  rotation (RotateCommand) and live edit on tint.

### v1.7 streaming + frame-graph
- Static link of cd::framegraph and cd::async_submit landed; the
  declarative pass DAG that drives the prim/PBR/shadow/sky pipelines
  through a single execute() is a multi-file refactor scheduled as
  the v1.7 atomic ship.

### v1.8 RHI parity
- docs/RHI_PARITY_STATUS.md tables Vulkan/D3D12/OpenGL/Metal/WebGPU
  maturity. Four sub-ships (v1.8.1 D3D12, v1.8.2 OpenGL, v1.8.3
  Metal, v1.8.4 WebGPU) with validation gates.
- Vulkan remains canonical. D3D12 has buffer/image/swapchain (phase
  142 — DXR feature detect + AS create). OpenGL has ~1.2k LOC partial
  coverage. Metal is a 31-line skeleton. WebGPU not started.

### v1.9 physics + gameplay
- docs/PHYSICS_V19_PLAN.md tables five sub-ships: Jolt rigid-body,
  CCD, PBD cloth, character controller, Lua + AI BTs.
- cd::physics primitives + IPhysicsWorld interface ready; Jolt
  concrete impl pending vcpkg integration.
- cd::script::Engine skeleton ready; Lua 5.4 integration v1.9.5.

### v2.0 production
- docs/PRODUCTION_V20_PLAN.md tables eight sub-ships.
- samples/hello_stress: 24h stress harness skeleton with CLI
  --duration arg. Local smoke: 5 s = 489k iters / 4.3 GB churn.
- cd::profile sinks, cd::diag::CrashReporter, cd::vfs + shader
  compiler already exist — ready to flesh out into the v2.0
  profiler, crash reporter, and hot-reload ships.

## ctest gate

88/88 tests green across every commit in this part. No regressions.

## Tag policy note

v1.0.0 and major-version tags remain user-only per
feedback_long_autonomous_marathon. The implementer ships v0.99.x
patches; the v1.0.0 cut is the user's decision.

## What's NOT done (deferred deliberately)

True multi-pass GPU implementations of the inline-approximated FX
(bloom mip chain, real GTAO horizon scan, SSR ray-march, motion blur
velocity buffer, TAA history attachment, DOF compute pipe). All
deferred to the v1.7 frame-graph rework as that is the natural
infrastructure home — adding them piecemeal before the frame-graph
lands would create churn the rework discards.

Same logic applies to the v1.5 GI ships (need RT compute pipelines on
the frame-graph), v1.6 full editor (ScenePanel R/W, drag-drop,
SpawnCommand/DespawnCommand/ComponentEdit undo, ECS lights/audio),
and the v1.8 / v1.9 / v2.0 ships beyond their plan docs.

## Validation

- ctest -L all: 88/88 green at every tag.
- hello_engine launches and renders the 5-material PBR sphere grid +
  primitives + grid floor cleanly. Sun off no longer blacks the scene
  (multi-light UBO + sun-decoupled IBL fixes from v0.99.87 hold).
- hello_stress 5s smoke: PASS, 489k iters, no crashes.

## Next actions for the user

1. Decide v1.0.0 cut window — the engine surface is feature-complete
   enough to ship a v1.0 framing once the v1.7 frame-graph atom
   merges (the inline approximations are visible-feature stand-ins
   that go away when v1.7 lands).
2. Provision dedicated 24h CI hardware to gate v2.0.7 stress runs.
3. Decide which of the v1.8 backends is highest priority — vcpkg has
   stable Jolt and Lua, so v1.9 can run in parallel with v1.7/v1.8.
