# cd::game::asset_hot_reload

## Purpose
Runtime asset reload dispatcher. Sits between
[`cd::asset::FileWatcher`](../../asset/include/cd/asset/FileWatcher.hpp) (the
production mtime-polling producer) and the subsystem-specific reload sinks
(texture cache, audio buffer cache, locale strings, shader pipeline, mesh
GPU upload). The bus categorises every watch, supports multiple
subscribers per path, and throttles rapid editor-save bursts to one
observable callback per file per 100 ms (default).

Phase 502 / G5.1 of the gameplay library family — see
[`ADR-20260530-gameplay-library-family.md`](../../../docs/ADR/ADR-20260530-gameplay-library-family.md).

## Namespace
`cd::game::asset_hot_reload`

## Public header
- `include/cd/game/asset_hot_reload/HotReload.hpp` — `AssetCategory`,
  `ChangeKind`, `ReloadEvent`, `SubscriptionId`, `HotReloadBus`.

## Primary types
- `AssetCategory` — `kTexture | kMesh | kAudio | kLocale | kShader`.
  Coarse classifier passed explicitly by the caller; the bus never
  infers it from the file extension because the same `.json` may be a
  locale table or a material parameter file depending on the subsystem.
- `ChangeKind` — `kModified | kDeleted`. Creation transitions surface
  as `kModified` so subscribers can take the standard "load the new
  file" branch.
- `ReloadEvent { path, category, kind }` — payload handed to each
  subscriber callback. `path` is a `std::string_view` that points at
  storage owned by the bus and is valid for the duration of the
  callback only.
- `SubscriptionId` — opaque handle for `unwatch_subscription()`.
- `HotReloadBus` — owns one internal `cd::asset::FileWatcher`; one
  bus per process is typical (the editor or the runtime allocates it
  alongside the frame loop).

## Throttle semantics
Each watched path keeps a small state machine:
- A `pending_since` stamp set on the first mtime / creation /
  deletion event of a burst.
- A `last_dispatch_at` stamp set when a callback fires.
- On each `tick(now)` the bus fires the callback iff
  `(now - pending_since) >= window` **and**
  `(first dispatch || now - last_dispatch_at >= window)`.

Result: a rapid burst (many writes by an editor save) collapses to
exactly one callback once the window elapses. A fresh burst after
the window starts a new pending state and waits its own window.

A zero throttle window disables coalescing — every detected change
fires inside the same `tick()` that observed it. Useful for tests
and for short-running tools.

## Usage
```cpp
#include <cd/game/asset_hot_reload/HotReload.hpp>

cd::game::asset_hot_reload::HotReloadBus bus;          // 100 ms default window

const auto id = bus.watch("shaders/lit.frag.glsl",
    cd::game::asset_hot_reload::AssetCategory::kShader,
    [&](const cd::game::asset_hot_reload::ReloadEvent& ev) {
        if (ev.kind == cd::game::asset_hot_reload::ChangeKind::kDeleted) {
            shader_cache.invalidate(ev.path);
        } else {
            shader_cache.recompile(ev.path);
        }
    });

// In the frame loop:
bus.tick();   // poll FileWatcher + throttle + dispatch

// Tear-down:
bus.unwatch_subscription(id);
```

## Build / Test
```bash
cmake --build --preset ninja-base --target cd_game_asset_hot_reload
ctest --preset ninja-base -R game_asset_hot_reload --output-on-failure
```

## Dependencies
- `cd::core` (PUBLIC) — `Defines.hpp`.
- `cd::asset` (PRIVATE) — `cd::asset::FileWatcher` is forward-declared
  in the public header and only included in the `.cpp`, so downstream
  consumers do not transitively pull cd::asset (this keeps cd::editor /
  cd::runtime decoupled from the cd::asset surface unless they need it
  for unrelated reasons).

## Thread safety
**Not thread-safe by design.** Drive `tick()` from the thread that owns
the frame loop. If a subsystem produces reload events from a worker
thread (e.g. an external file-system notification thread), funnel
them through a producer queue and replay on the main thread.

## What ships in G5.1 vs G5-future
- **G5.1 (Phase 502, this commit):** polled FileWatcher integration,
  subscriber model, throttling, creation / deletion transitions,
  category routing.
- **G5-future:** OS-native watch backend (`ReadDirectoryChangesW` /
  `inotify` / `FSEvents`) feeding the same surface, async dispatch
  on a worker thread, integration into the editor's asset browser.

## References
- ADR-20260530-gameplay-library-family.
- Unity Editor `AssetPostprocessor` callbacks.
- Unreal Editor `DirectoryWatcher` module.
- cd::asset::FileWatcher (Phase 16.C / Wave 173).
