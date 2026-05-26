# v2.0 Production Milestone Plan

v2.0 is the line where CHROMODYNAMIC stops being 'engine in
development' and starts being 'engine you can ship a product with'.
Eight ships, each independently validatable, mapped to existing
libraries where possible.

## v2.0.1 — Asset pipeline (offline cooker)

- Standalone `cd_assetc` executable that:
  - Walks a content root (e.g., ./assets/)
  - Cooks per asset type using existing loaders (asset_gltf,
    asset_ktx2, asset_obj, asset_cdmesh, asset_cdtex, asset_pak,
    asset_wav)
  - Emits a `.cdpak` bundle + manifest with content hashes for
    incremental rebuild
- Build-time hook in CMake: `add_cdpack(name FILES ...)` target
- Runtime loader: cd::asset::AsyncStreamer already exists; cooker
  emits the format the streamer reads
- Validation: cooked bundle vs source manifest hash; round-trip test
  for every supported format

## v2.0.2 — Frame profiler (CPU + GPU)

- cd::profile (exists) extended with hierarchical scopes + GPU
  timestamps via VK_KHR_timestamp_query
- ImGui panel with Tracy-style flame chart of last 120 frames
- Per-pass GPU cost broken out via frame-graph annotations
- Export to chrome://tracing JSON for offline analysis
- Budget: profiler overhead <= 0.2 ms / frame when enabled

## v2.0.3 — Crash reporter

- Windows: MiniDumpWriteDump on SEH, symbolize via PDB
- Linux: signal handlers + backtrace_symbols + addr2line
- macOS: KSCrash-style stack capture
- Standalone crash-uploader tool with user-configurable endpoint
- Privacy: stack frames + module list only; no user data
- Validation: forced crash test asserts dump file exists + signature
  matches recent build

## v2.0.4 — Audio: HRTF + spatializer

- cd::audio (exists) gains HRTF convolution path using MIT KEMAR
  IRs (public-domain dataset)
- Per-source position + listener orientation -> per-ear filter
- 7.1 / stereo / binaural output selectable
- Demo: a sound source orbiting the listener in hello_engine

## v2.0.5 — i18n + a11y

- cd::core::I18n: ICU-backed string table loader (vcpkg icu)
- ImGui font atlas regen on locale change
- Right-to-left text support for Arabic / Hebrew / Persian
- a11y: high-contrast theme, screen-reader hooks via UIA on Windows,
  keyboard-only navigation for every editor panel

## v2.0.6 — Hot-reload pipeline

- Asset hot-reload: cd::vfs (exists) emits FileChanged events;
  AsyncStreamer reloads dirty pak entries
- Shader hot-reload: cd::shader::Compiler watches glsl source dir;
  rebuilds pipelines on change
- Code hot-reload: out of scope for v2.0 (deferred to v2.1 plugin
  system)

## v2.0.7 — 24-hour stress harness

- Standalone `cd_stress` executable that:
  - Loops the hello_engine main loop for 24h with random scene
    perturbations (spawn/despawn/relight every 30s)
  - Asserts: no leaks (RSS bounded), no crash, no validation errors,
    no shader recompilations after first 60s
- Run in CI on a dedicated machine before each v2.x tag

## v2.0.8 — Documentation: ENGINE_GUIDE.md

- Single user-facing book in docs/ explaining:
  - Conceptual model (World/Project/Level/Layer, ECS, frame-graph)
  - Sample walkthrough (hello_engine from scratch)
  - Per-subsystem API surface (one page per library)
  - 'How do I…?' recipes for the 30 most common tasks
- Doxygen pass populating XML for cd::* namespaces

## Validation gates (BLOCKING for v2.0 tag)

- ctest 88/88 (current) -> expanded to ~150 tests covering new ships
- 24h stress run: PASS
- Profiler overhead: <= 0.2 ms / frame
- Memory: RSS bounded under 100 MB extra after 1h of editor work
- Code coverage: >= 80% on critical paths (rhi, asset, render core)
- Triple platform: hello_engine launches on Windows + Linux + macOS

## Risk register

- 24h stress run cadence: needs dedicated CI hardware. Without it,
  pipeline shifts left to a 1h smoke run gated by a separate weekly
  job.
- Crash reporter on Windows requires DbgHelp.dll; static link adds
  ~2 MB binary size. Acceptable.
- i18n font atlas regen blocks the UI thread for ~50 ms per locale
  swap; mitigate via background bake + atlas hot-swap.
