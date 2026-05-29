# cd::asset_pak

**Purpose**: Single-file asset bundle format and runtime reader. Packages multiple assets (meshes, textures, audio, animations) into one .pak container with a directory table. Enables atomic asset delivery, streaming, and DLC packaging.

**Namespace**: `cd::asset_pak`.

**Public Headers**:
- `cd/asset_pak/Pak.hpp` — PAK container format; directory table, mmap-able offset lookups.

**Primary Types** (Header-only):
- `PakHeader` — .pak file header (magic, version, directory offset, asset count).
- `PakAssetEntry` — asset descriptor (name, offset, size, type ID).
- `PakReader` — runtime accessor (open .pak file, lookup asset by name, read raw bytes).

**Build**:
```bash
cmake --build --preset ninja-debug --target cd_asset_pak
ctest --preset ninja-debug -R asset_pak --output-on-failure
```

**Dependencies**: cd::core (header-only library).

**Format Structure**:
```
Header (64 bytes) { magic, version, dir_offset, asset_count }
[ Asset 0 data ]
[ Asset 1 data ]
...
[ Directory table ]
```

**Asset Types**:
- Type IDs are user-defined (0=mesh, 1=texture, 2=audio, etc.).
- Runtime type dispatch via asset_type() callback.

**Notes**:
- Header-only; no .cpp (just .hpp with inline definitions).
- Designed for mmap (asset_offset is directly seekable for zero-copy reads).
- Directory stored at end so streaming tools can append without rewriting existing data.
- PAK files are production bundles; tools/pack_assets creates them from loose asset trees.
