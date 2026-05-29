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

// Load a plugin
auto plugin = cd::plugin::load("./plugins/my_feature.so");
auto my_function = plugin->get_symbol<int(*)()>("my_function");

// With hot reload
auto hot_reload = cd::plugin::create_hot_reload_monitor("./plugins");
while (true) {
  if (hot_reload->poll_and_reload()) {
    // Plugin was recompiled and reloaded
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
