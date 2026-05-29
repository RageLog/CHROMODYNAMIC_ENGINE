# cd::mem

**Purpose**: custom allocators + memory primitives. Linear / stack / pool / freelist / arena allocators sized for engine hot paths; rolls into mimalloc as the default global allocator when CD_WITH_MIMALLOC=ON.

**Namespace**: `cd::mem`.

**Headers**: `cd/mem/{LinearAllocator,StackAllocator,PoolAllocator,FreelistAllocator,ScopedArena,Bytes}.hpp`.

**Primary types**:
- `cd::mem::LinearAllocator` -- bump pointer; O(1) alloc, no per-allocation free (only `reset()`).
- `cd::mem::StackAllocator` -- LIFO; supports nested scopes via marker/restore.
- `cd::mem::PoolAllocator` -- fixed-size block pool; O(1) alloc + free.
- `cd::mem::FreelistAllocator` -- variable-size with explicit best-fit free list.
- `cd::mem::ScopedArena` -- RAII wrapper around a LinearAllocator; resets on scope exit.

**Test command**: `ctest --preset ninja-debug -R cd_test_mem --output-on-failure`.

**Notes**:
- Header-only.
- LinearAllocator is the default per-frame transient allocator (job system command buffers).
- Pool + Freelist used by cd::ecs for component-storage chunks.
- ASan-compatible: poison/unpoison hooks at every alloc/free boundary in debug builds.
