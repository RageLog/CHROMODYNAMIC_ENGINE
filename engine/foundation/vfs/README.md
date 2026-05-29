# cd::vfs

**Purpose**: Virtual file system with overlay layers. Abstracts asset loading behind a pluggable source interface (filesystem, PAK bundles, memory buffers). Enables hot-reload, VFS path remapping, and asset streaming without coupling code to physical storage layout.

**Namespace**: `cd::vfs`.

**Public Headers**:
- `cd/vfs/VirtualFileSystem.hpp` — VFS registry; mount/unmount sources, file I/O.
- `cd/vfs/IFileSource.hpp` — abstract file source interface.
- `cd/vfs/FilesystemSource.hpp` — disk filesystem source (open dir, list files, read).
- `cd/vfs/MemorySource.hpp` — in-memory source (preloaded bundles, test fixtures).

**Primary Types**:
- `VirtualFileSystem` — global VFS; mount multiple sources.
- `IFileSource` — abstract interface (open_file, list_directory, exists, stat).
- `FilesystemSource` — OS filesystem mounting.
- `MemorySource` — RAM-based file tree.
- `VfsFile` — RAII file handle (automatic close).

**Build**:
```bash
cmake --build --preset ninja-debug --target cd_vfs
ctest --preset ninja-debug -R vfs --output-on-failure
```

**Dependencies**: cd::core, cd::io.

**Usage**:
```cpp
#include <cd/vfs/VirtualFileSystem.hpp>
cd::vfs::VirtualFileSystem vfs;
vfs.mount("/assets", std::make_unique<cd::vfs::FilesystemSource>("assets/"));
auto file = vfs.open("/assets/mesh.glb");
```

**Design** (ADR-017 P4):
- Overlay architecture: sources stacked by priority; first-match wins.
- Path normalization: all paths use forward slashes, case-insensitive on Windows.
- Hot-reload: can re-mount sources without restarting.

**Notes**:
- VFS enables PAK file streaming (mount .pak as a source).
- Mods use overlay mounts (new VFS layer shadows base assets).
- Race-free file discovery via snapshot-based directory listing.
