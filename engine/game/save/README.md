# cd::game::save

## Purpose
Slot-based save / load with atomic on-disk writes. Each slot is a small
folder on the user's data path holding a `meta.json` header and a
`body.<json|bin>` blob. `save()` writes through a `.tmp` sibling and
renames, so a crash mid-write leaves either the previous body intact or
no body at all — never a half-written corrupt file. This is the
foundation tier (G1.3) of the gameplay library family per
[`ADR-20260530-gameplay-library-family.md`](../../../docs/ADR/ADR-20260530-gameplay-library-family.md);
versioning, migration, compression, and cloud-save hooks land in Phase G5.

## Namespace
`cd::game::save`

## Public headers
- `include/cd/game/save/Save.hpp` — `SaveFormat`, `SaveSlot`,
  `SaveSystem`, `is_valid_slot_id`, `default_storage_root`,
  `save_errors::{Code, make}`.

## Primary types
- `SaveFormat` — `kJson` or `kBinary`. Body bytes are *not* transcoded;
  the value only drives the file extension and a sanity check on
  `load(slot, expected)`.
- `SaveSlot` — `{ id, label, timestamp, format, size_bytes }`. Returned
  by `list_slots()` sorted by `timestamp` descending.
- `SaveMeta` — extended header block `{ version, format, timestamp,
  app_name, app_version, payload_size }`. Persisted alongside the Phase-1
  keys in `meta.json` (forward-compatible: Phase-1 readers ignore the
  extra keys).
- `SaveSystem` — owns one storage root. Phase-1: `save()`, `load()`,
  `delete_slot()`, `list_slots()`, `set_storage_root()`, `slot_exists()`,
  `slot_directory()`. Phase-2: `save_with_meta()`, `load_with_meta()`,
  `register_migration()`, `migrate()`, `set_cloud_handler()`,
  `has_cloud_handler()`.

## On-disk layout
```
<storage_root>/
  slot_01/
    meta.json     # { id, label, timestamp, format, size_bytes }
    body.bin      # opaque caller-supplied bytes (or body.json)
  slot_02/
    meta.json
    body.json
  ...
```

## Atomic write protocol
1. Caller-supplied blob is written into `body.<ext>.tmp` (newly opened,
   truncated).
2. Stream is flushed + closed.
3. `std::filesystem::rename(tmp, body)` puts the new body in place.
4. `meta.json` is written through the same temp-then-rename dance.

On a hard crash between steps 1 and 3 the prior `body.<ext>` is
untouched; the orphan `.tmp` is harmless (overwritten by the next save).
On hosts where rename refuses to overwrite (legacy Windows behaviour)
the implementation falls back to `remove(final) + rename(tmp, final)` so
the contract is preserved as strongly as the platform allows.

## Default storage root
`default_storage_root()` resolves to:

| OS      | Path                                                            |
|---------|-----------------------------------------------------------------|
| Windows | `%LOCALAPPDATA%/CHROMODYNAMIC/saves` (fallback `%APPDATA%` then `%USERPROFILE%/AppData/Local`) |
| macOS   | `$HOME/Library/Application Support/CHROMODYNAMIC/saves`         |
| Linux   | `$XDG_DATA_HOME/CHROMODYNAMIC/saves` (fallback `$HOME/.local/share/CHROMODYNAMIC/saves`) |

The path is *computed*, not necessarily created; the directory springs
into existence on first `save()`.

## Slot-id validation
Slot ids are used verbatim as folder names. To stay portable across
Windows / macOS / Linux they are restricted to ASCII `[A-Za-z0-9_-]`,
length 1..64, no leading/trailing dots or spaces, and not equal to the
reserved Windows device names (`CON`, `PRN`, `AUX`, `NUL`, `COM1..9`,
`LPT1..9`, case-insensitive). `is_valid_slot_id()` is public so callers
(REST endpoints, UI dialogs) can pre-validate.

## Error codes
Domain `0x4753` ("GS" — Game Save). Returned via
`cd::core::Result<T>` (which is `std::expected<T, ErrorCode>`).

| Code              | Meaning                                                    |
|-------------------|------------------------------------------------------------|
| `kInvalidSlotId`  | Slot id failed `is_valid_slot_id()`.                       |
| `kSlotNotFound`   | No on-disk record under `<root>/<slot_id>`.                |
| `kIoFailed`       | Underlying filesystem operation reported an error.         |
| `kCorruptHeader`  | `meta.json` is missing or unparseable.                     |
| `kFormatMismatch` | Asked for `kJson` but body was written as `kBinary`, etc.  |

## Usage
```cpp
#include <cd/game/save/Save.hpp>

cd::game::save::SaveSystem saves;            // uses default user-data root

const std::string state = serialize_world(); // your snapshot
std::span<const std::byte> bytes {
    reinterpret_cast<const std::byte*>(state.data()), state.size()
};
if (auto r = saves.save("autosave_01", bytes,
                        cd::game::save::SaveFormat::kBinary,
                        "Autosave 1"); !r) {
    LOGE("save failed: {}", r.error().message);
}

for (const auto& slot : saves.list_slots()) {        // most-recent first
    show_in_menu(slot.id, slot.label, slot.timestamp);
}

if (auto blob = saves.load("autosave_01")) {
    deserialize_world(*blob);
}
```

## Build / Test
```bash
cmake --build --preset ninja-debug --target cd_game_save
ctest --preset ninja-debug -R game_save --output-on-failure
```

## Dependencies
- `cd::core` — `Result`, `ErrorCode`, `Defines`. **No** zstd / rapidjson /
  nlohmann::json transitively. The internal `meta.json` reader/writer is
  a tiny hand-rolled parser covering only the four scalars the header
  stores; full JSON parsing belongs in `cd::asset::json` and would defeat
  the "cd::core-only" contract that keeps this library at the bottom of
  the gameplay DAG.

## Thread safety
Not thread-safe by design. Drive from the thread that owns the save UI
(usually the main loop); kick off compression or world-snapshot work to
a worker pool and hand the resulting bytes back to `save()` on the main
thread. A future Phase G5 addition may add an internal queue.

## What ships in G1 vs G5
- **G1 (Phase 470, this commit):** slot folders, atomic body + meta
  writes, default user-data root resolution, slot-id validation,
  list / load / delete, `kJson` and `kBinary` extension hints.
- **G5 (planned):** zstd compression, header schema versioning,
  `Save::Migration` callbacks (`v0.1 -> v0.2`), cloud-save hook,
  encryption hook, async snapshot pipeline.

## References
- ADR-20260530-gameplay-library-family §2.2 G-08.
- Unreal Engine `USaveGame` + `UGameplayStatics::SaveGameToSlot`.
- Unity `JsonUtility` + `PlayerPrefs` + custom binary serializers.
- Rust `serde` + `bincode` for the atomic-rename idiom on a typed save layer.
- POSIX `rename(2)` atomicity guarantee.
