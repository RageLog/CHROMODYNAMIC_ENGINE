# cd::game::settings

## Purpose
Persistent user-preferences container for the gameplay tier
(ADR-20260530-gameplay-library-family G1.4). Strongly-typed key/value
store with live broadcast on change, INI-flavoured on-disk format that
preserves comments and blank lines across save/load.

Use this library for: graphics options, audio mix, control sensitivity,
accessibility toggles, language, UI scale, last-used save slot —
anything the player edits in a settings menu and expects to survive
across runs.

## Namespace
`cd::game::settings`

## Public headers
- `include/cd/game/settings/Settings.hpp` — `Value` typedef,
  `ChangeCallback`, `SubscriptionId`, `Settings` class.

## Primary types

| Type             | Role                                                        |
|------------------|-------------------------------------------------------------|
| `Value`          | `std::variant<bool, int, float, std::string>`               |
| `ChangeCallback` | `std::function<void(const Value&)>` fired on change         |
| `SubscriptionId` | Opaque handle returned by `on_change`                       |
| `Settings`       | The container — `get` / `set` / `load` / `save` / `on_change` |

## API surface

| Operation                              | Effect                                                              |
|----------------------------------------|---------------------------------------------------------------------|
| `get<T>(key)` -> `std::optional<T>`    | Returns the value iff the key exists AND stored alternative is T   |
| `set<T>(key, value)`                   | Insert-or-overwrite; broadcasts only when the stored value changes  |
| `load(path)` -> `bool`                 | Replaces in-memory state from file; fires observers per applied key |
| `save(path)` -> `bool`                 | Writes current state; preserves comments + blanks captured by load  |
| `on_change(key, callback)` -> `SubscriptionId` | Register live observer                                      |
| `has_key(key)`                         | Existence check                                                     |
| `key_count()` / `observer_count(key)`  | Diagnostic accessors                                                |
| `clear()`                              | Wipe values + observers + preserved layout                          |

### Semantic guarantees

1. **No-op set is silent.** `set<int>("foo", 4); set<int>("foo", 4);`
   broadcasts exactly once. Observer dedup is the caller's
   responsibility if they register multiple callbacks; we do not
   silently collapse them.
2. **Type-strict get.** `get<int>(key)` on a string-typed entry returns
   `std::nullopt` — no numeric coercion, no silent zero. This mirrors
   `std::get_if<T>` and surfaces user-config typos at the call site.
3. **Layout-preserving save.** Comments (`# …`) and blank lines from
   the on-disk file are recorded by `load()` and re-emitted by
   `save()` in the original order. Keys added between load and save
   are appended at the end.
4. **Live broadcast on load.** `load()` fires observers for every key
   applied — runtime config-file edits look identical to "user clicks
   apply in the settings menu". This is what makes hot-reload of a
   settings file possible (Phase G5 plumbing).

## On-disk format

Trivial INI-flavoured key/value:

```ini
# CHROMODYNAMIC settings — hand-authored or auto-generated.
# Section comments are preserved across save.

audio.enabled=true
audio.master_volume=0.5

# graphics
graphics.cascades=4
graphics.scaling=1.0
user.locale=en-GB
```

Parser rules:
- `# …` line comments preserved verbatim.
- Blank lines preserved.
- `key=value`, ASCII whitespace around `=` is trimmed.
- Type-deduction order: `true`/`false` -> bool; signed-integer literal
  -> int; literal with `.` or `e` -> float; everything else -> string.
- No quoting, no escapes, no sections. Phase G1 contract — schema
  versioning + TOML upgrade is a Phase G5 ADR.

## Threading
**Not thread-safe.** Drive from the engine main thread that owns the
settings UI; worker threads needing change events should marshal them
through the engine event bus instead of subscribing here directly. The
library deliberately avoids embedding a mutex so the cost of
`get<T>()` (the dominant operation) stays at one map lookup + one
variant get.

## Usage example
```cpp
#include <cd/game/settings/Settings.hpp>

cd::game::settings::Settings prefs;
prefs.load("user/prefs.ini");

// Read current values.
const auto cascades = prefs.get<int>("graphics.cascades").value_or(4);
const auto volume   = prefs.get<float>("audio.master_volume").value_or(1.0F);

// Live broadcast — re-apply when the user moves a slider.
prefs.on_change("graphics.cascades", [&](const auto& v) {
    renderer.set_shadow_cascades(std::get<int>(v));
});

prefs.on_change("audio.master_volume", [&](const auto& v) {
    audio.set_master_volume(std::get<float>(v));
});

// User toggles a setting in the UI.
prefs.set<int>("graphics.cascades", 8);   // fires the observer
prefs.set<int>("graphics.cascades", 8);   // silent — no-op

prefs.save("user/prefs.ini");
```

## Build / Test
```bash
cmake --build --preset ninja-base --target cd_game_settings
ctest  --preset ninja-base -R game_settings --output-on-failure
```

## Dependencies
- `cd::core` — `Defines.hpp` only (header-level dependency).

## References
- ADR-20260530-gameplay-library-family §2.2 G-09 (this library).
- Unreal Engine `GameUserSettings` / `UDeveloperSettings`.
- Unity `PlayerPrefs` + custom serialised settings.
- Godot 4 `ConfigFile`.
- INI format historical lineage (Win 3.x `WritePrivateProfileString`).

## Notes / future work
- Phase G5 follow-ups:
  - `off_change(SubscriptionId)` overload (the id is already returned
    and stable).
  - Schema versioning + per-key migration on load.
  - TOML upgrade for nested sections.
  - Hot-reload watcher (`cd::game::asset_hot_reload` integration).
- Single TU; header-only would force every consumer to recompile on
  internal parser tweaks.
