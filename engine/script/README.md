# cd::script

**Purpose**: Embedded Lua 5.4 scripting tier. Provides a C++ API wrapper around Lua's C API so game logic can be written in Lua (or called from C++) without exposing Lua types across the library boundary. Used by the editor (scripted console commands) and game runtime (scripted behaviors, debug utilities).

**Namespace**: `cd::script`.

**Public Headers**:
- `cd/script/Engine.hpp` — Lua VM management; execute scripts, call functions, bind C++ → Lua.

**Primary Types**:
- `Engine` — Lua VM wrapper; owns lua_State internally.
- `Function` — Lua function reference; call from C++ with type-safe argument/return marshalling.
- `Table` — Lua table access (key-value pairs).

**Build**:
```bash
cmake --build --preset ninja-debug --target cd_script
ctest --preset ninja-debug -R script --output-on-failure
```

**Vendored Dependencies**:
- **Lua 5.4.7** (MIT) — fetched via FetchContent from https://github.com/lua/lua.git.
- **Compiled Internally**: cd::script compiles Lua sources into `cd_lua_internal` (static library).

**Why Lua 5.4?**
- Tiny (~30 .c files, no third-party deps).
- Mature + stable (1993–present).
- Predictable C API → easy C++ binding without heavyweight wrappers (sol3 optional, not required).
- MIT license.

**Usage**:
```cpp
#include <cd/script/Engine.hpp>
cd::script::Engine lua;
lua.execute_file("script.lua");
auto fn = lua.get_function("on_update");
fn.call(delta_time);
```

**Design**:
- Public API exposes no Lua types (lua.h not in public header).
- Opaque void* state; C++ wrappers hide implementation.
- EXCLUDE_FROM_INSTALL: vendored Lua not in export set.

**Notes**:
- Lua is single-threaded; synchronize script execution via cd::concurrency locks.
- Hot-reload: create new Engine, load new scripts, swap globals.
- Debugger: Lua 5.4 includes hook-based profiling; tools integrate this via Engine API.
