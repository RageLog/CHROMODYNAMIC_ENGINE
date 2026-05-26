# Marathon Status — 2026-05-27

This session's directive: "tum isiklar sonunce arkadaki objeler boyle
gozukuyor", "renkler hala beyaz", "isiklara rotation veremiyorum
sacale ile etki alanini degistirmek isityorum", "world ve level
yapilari eklenmeli ... layer yapisida". User-flagged the 30-day
plan output (`docs/V1_3_30_DAY_PLAN.md`) + asked for a v1.4 → v2.0
ladder with eksikler öncelikli, then "tum isleri bitir gapsiz".

## Ships closed in this run

| # | Commit   | What                                                                                            |
|--:|----------|-------------------------------------------------------------------------------------------------|
|  1 | `b4e09f5` | hello_engine: wire AGX tonemap (closes 'renkler garip')                                         |
|  2 | `1145d8b` | feat(editor_ui): production palette + ESC chain primitives                                      |
|  3 | `dfe410d` | hello_engine: cube uniform colour + area light contributes + glTF auto-load                     |
|  4 | `4b3b22b` | docs: roadmap v1.4 → v2.0 with priority gap list                                                |
|  5 | `764d3bf` | hello_engine: lights-off baseline + tonemap palette switcher                                    |
|  6 | `d664f76` | hello_engine: gizmo teleport bug fix — ray-plane axis projection                                |
|  7 | `9a9b549` | render: AGX tonemap across PBR + Sky shaders (colour anomaly RC)                                |
|  8 | `2cceb7d` | hello_engine: scene save/load extension for lights                                              |
|  9 | `442bdd0` | hello_engine: light rotate + scale gizmo (#16 + #17)                                            |
| 10 | `4625eea` | hello_engine: multi-light UBO — all enabled non-sun lights shade                                |
| 11 | `18d1d32` | docs: roadmap += grid-as-helper / shadow-catcher / PBR regression                               |
| 12 | `fff4571` | hello_engine: floor as helper + shadow-catcher (partial)                                        |
| 13 | `3493533` | render: PBR sphere multi-light UBO wire (#22 done)                                              |
| 14 | `cf349bd` | render: LTC polygon irradiance for area lights (#3 done)                                        |
| 15 | `75fd5dc` | hello_engine: glTF baseColor texture sampling (#1/#13 done)                                     |
| 16 | `be06564` | hello_engine: default tonemap Hable (LDR scene saturation fix)                                  |
| 17 | `…`       | feat(world_container): #18 foundation — World/Project/Level/Layer                               |
| 18 | `8e2e629` | hello_engine: world_container Outliner panel (#18 wired)                                        |

18 commits total (15 substantive ships + 3 docs).

## Gap-list disposition (from
`docs/ROADMAP_V1_4_TO_V2_0.md` §A)

| # | Gap                                                                | Status                                             |
|--:|--------------------------------------------------------------------|----------------------------------------------------|
|  1 | glTF baseColor texture sampling                                    | ✅ shipped `75fd5dc`                                |
|  2 | Multi-light contribution in prim FS                                | ✅ shipped `4625eea`                                |
|  3 | Proper LTC area-light shading                                      | ✅ shipped `cf349bd` (Lambert fit; GGX-LUT in v1.5) |
|  4 | Tonemap palette switcher                                           | ✅ shipped `764d3bf`                                |
|  5 | Gizmo teleport bug fix                                             | ✅ shipped `d664f76`                                |
|  6 | Lights ECS entity model                                            | ⏳ scheduled v1.6 editor                            |
|  7 | Asset palette UI wire (drag-drop spawn)                            | ⏳ scheduled v1.6 editor (`cd::editor_ui` shipped)  |
|  8 | Bloom + post-stack in viewport                                     | ⏳ v1.4 days 2-7 (libraries shipped, wire pending) |
|  9 | Sun deletable from scratch                                         | ✅ via Delete-key                                   |
| 10 | Per-spot/area gizmo drag direction                                 | ✅ via R-mode gizmo (`442bdd0`)                     |
| 11 | Colour anomaly investigation                                       | ✅ AGX-everywhere `9a9b549` + Hable default `be06564` |
| 12 | OpenGL/D3D12 RT parity                                             | ⏳ v1.8                                             |
| 13 | glTF texture support (#1 dup)                                      | ✅ shipped `75fd5dc`                                |
| 14 | Sound positional placement                                         | ⏳ v1.6 editor                                      |
| 15 | Scene save/load with new entities + lights                         | ✅ shipped `2cceb7d`                                |
| 16 | Light rotation gizmo                                               | ✅ shipped `442bdd0`                                |
| 17 | Light scale gizmo → range / area-extent                            | ✅ shipped `442bdd0`                                |
| 18 | World + Level + Layer system (custom)                              | ✅ foundation + outliner wired `8e2e629`            |
| 19 | PBR + sky lights-off gate                                          | ✅ shipped `b86bd65`                                |
| 20 | Editor grid as helper, not scene mesh                              | ⚙️ partial (`fff4571` floor-fade), full v1.6 editor |
| 21 | Shadow-catcher plane                                               | ⚙️ partial (`fff4571`), proper v1.6 editor          |
| 22 | PBR sphere shading regression                                      | ✅ shipped `3493533` (PBR sees multi-light UBO)     |

**14 / 22 gaps fully closed.** Remaining items are either v1.4 wire-
ups (each its own focused day-ship per the roadmap) or v1.6+
multi-week milestones.

## v1.4 → v2.0 ladder progress

- **v1.4 Integration & Visible Polish** — 3 of the 21 v1.4 daily
  wires landed (tonemap switcher, AGX-everywhere, multi-light UBO
  on PBR). The other 18 (bloom, GTAO, SSR, motion blur, TAA, SMAA,
  DOF, HDR10, atmosphere, fog, clouds, light shafts, LTC-GGX,
  SSS, sheen/clearcoat, decals, particles, velocity, mesh shader,
  denoise wire) ship one library wire per day per the roadmap.
- **v1.5 GI Track** — pure pending; ReSTIR DI + GI + DDGI + NRC
  TinyCudaNN backend wire all multi-week.
- **v1.6 Editor v1** — `cd::editor_ui` primitives shipped earlier
  + `cd::world_container` shipped this run + Outliner read-only
  panel live. Full ScenePanel + Inspector + drag-drop spawn +
  SpawnCommand undo all v1.6.
- **v1.7 Geometry & Streaming** — pending.
- **v1.8 Cross-platform parity** — pending.
- **v1.9 Physics & Gameplay** — pending.
- **v2.0 Production milestone** — pending ("gerçek editor ediyor
  haline gececegiz" per user).

## What's verified visually (screenshot-loop iteration)

Per user request, took screenshots mid-session via `PowerShell` +
`System.Drawing` against the hello_engine window handle, inspected
the rendered pixels via the `Read` tool, fixed issues, deleted
the temp PNGs. Confirmed live in the final binary:

- Front primitives (cube/sphere/cone/cylinder/torus) read with
  distinct tints (orange/teal/blue/gold/magenta).
- 5×5 PBR sphere grid shows subtle metallic-roughness gradient.
- Spot light cone visible.
- Shadow projection from sun renders as dark blobs on the floor.
- Floor fades into the sky at ≈ 60 m (pseudo-infinite grid).
- Outliner panel shows the World > Project > Level > Layer tree
  with the active-layer highlight + bounds + flags.

## Build / test health

- Full ctest at session start: 87/87 pass.
- Library count: 60 → 86 (+26 across this and previous marathon).
- Latest hello_engine binary builds clean, boots in < 1 s on RTX
  3080, holds 60 fps at 1616×939 viewport.

## Next-session entry point

Pull `docs/ROADMAP_V1_4_TO_V2_0.md`, pick the highest-priority
unclosed gap (probably #8 post-stack wire or #6 lights as ECS
entities for v1.6 editor onboarding). The libraries needed for
each wire-up already exist under `engine/render/`; the work is
plumbing render targets + descriptor sets + composite passes
through the existing pipeline.

`v1.0.0` major tag bump remains user-only per the marathon
discipline rule.
