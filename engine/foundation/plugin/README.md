# cd::plugin

## Purpose
Dynamic plugin loading, hot-reload support, and file-watching infrastructure. Enables runtime loading of shared libraries (.dll / .so / .dylib) with optional file-system polling for development-time code iteration.

## Namespace
`cd::plugin::` — all public symbols.

## Public headers
- `Loader.hpp` — Plugin discovery and dynamic symbol resolution
- `HotReload.hpp` — Live recompilation and reload state machine
- `FileWatcher.hpp` — Filesystem event polling (platform-native: Win32, FSEvents, inotify)
- `IPlugin.hpp` — Plugin interface contract

## Primary types
- `PluginHandle` — Opaque plugin reference; symbol lookup
- `HotReloadState` — Tracks reload generation + pending recompilation
- `FileWatcher` — Platform-agnostic filesystem monitor (uses native APIs)
- `IPlugin` — Virtual interface for plugin implementations

## Usage example
```cpp
#include <cd/plugin/Loader.hpp>
#include <cd/plugin/HotReload.hpp>

// Load a plugin (RAII: LoadedPlugin's dtor calls shutdown() + unloads the lib)
cd::plugin::Loader loader;
auto loaded = loader.load("./plugins/my_feature.so");  // Result<LoadedPlugin>
if (loaded) {
  cd::plugin::IPlugin& p = *loaded->instance;
  // … use p …
}

// Watcher-driven hot reload: one object fuses the file watcher with the
// unload → load → re-register orchestrator (load-new-before-teardown-old,
// so a failed reload rolls back to the last-known-good plugin).
auto monitor = cd::plugin::make_watched_hot_reloader(loader);
monitor->set_before_unload([](cd::plugin::IPlugin& old) { /* release refs */ });
monitor->set_after_load([](cd::plugin::IPlugin& fresh) { /* re-register */ });
if (auto m = monitor->mount("./plugins/my_feature.so"); m) {
  while (running) {
    auto r = monitor->poll_and_reload();  // Result<bool>: true == reloaded
    if (!r.has_value()) { /* reload failed; previous plugin still live */ }
  }
}
```

## Build
```bash
cmake --build --preset ninja-debug --target cd_plugin
```

## Test
```bash
ctest --preset ninja-debug -R plugin
```

## Dependencies (per CMakeLists)
- `cd::core` — ErrorCode, Result, path handling

## Platform notes
- **Windows**: Win32 file change notification (directory + file pattern match)
- **macOS**: FSEvents (CoreServices framework, linked PUBLIC)
- **Linux**: inotify syscalls

## Notes
- Static library
- macOS requires CoreServices framework linkage (PUBLIC, transitive)
- FileWatcher integrates platform-native APIs; abstraction at C++ level
- Hot reload uses generation counter for safe reload detection
- Sprint S2.4 — ADR-017 P2

## References
- ADR-017 — Foundation pattern salvage (hot reload)
- Platform-specific file watcher implementations
