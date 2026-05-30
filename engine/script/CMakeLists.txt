# engine/script — cd::script
#
# Embedded Lua 5.4 scripting tier. Lua sources are vendored via
# FetchContent and compiled into an internal static library
# (`cd_lua_internal`) so cd::script ships as a self-contained
# dependency. The Lua git tag is pinned; bump it deliberately.
#
# Why Lua 5.4 over alternatives:
#   * Mature, tiny (~30 .c files, no third-party deps), MIT licensed.
#   * Predictable ABI for the C API → easy to bind from C++ without
#     a heavyweight wrapper (sol3 / LuaBridge optional, not required).
#   * Stable since 1993; the language semantics don't churn.
# =============================================================================

include(FetchContent)

FetchContent_Declare(
  lua
  GIT_REPOSITORY https://github.com/lua/lua.git
  GIT_TAG        v5.4.7
  GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(lua)

# --- Internal Lua static library --------------------------------------------
# Lua's upstream Makefile is plain-make (no CMakeLists). We compile the
# C sources ourselves so the build is uniform across MSVC / clang-cl /
# Clang / MinGW GCC. lua.c (REPL) and luac.c (offline compiler) are
# stand-alone executables; we skip both.

file(GLOB CD_LUA_SOURCES "${lua_SOURCE_DIR}/*.c")
list(FILTER CD_LUA_SOURCES EXCLUDE REGEX "/lua\\.c$")
list(FILTER CD_LUA_SOURCES EXCLUDE REGEX "/luac\\.c$")
list(FILTER CD_LUA_SOURCES EXCLUDE REGEX "/onelua\\.c$")

add_library(cd_lua_internal STATIC ${CD_LUA_SOURCES})
target_include_directories(cd_lua_internal SYSTEM PUBLIC ${lua_SOURCE_DIR})

# Lua C code is canonical K&R style; suppress every warning we enforce
# on engine code so the upstream sources don't trip -Werror.
if(MSVC)
  target_compile_options(cd_lua_internal PRIVATE
    /wd4267 /wd4244 /wd4310 /wd4456 /wd4505 /wd4996 /wd4146
  )
else()
  target_compile_options(cd_lua_internal PRIVATE -w)
endif()

set_target_properties(cd_lua_internal PROPERTIES
  FOLDER "vendored"
  POSITION_INDEPENDENT_CODE ON
)

# --- cd::script public library ----------------------------------------------

cd_add_library(script
  EXCLUDE_FROM_INSTALL  # vendored Lua via FetchContent — not in export set
  SOURCES
    src/Engine.cpp
    src/Bindings.cpp   # phase467: math + ecs + scene + event-hook bindings
  PUBLIC_DEPS
    cd::core
    cd::math   # phase467: Vec3f / Vec4f / Mat4f userdata bindings
    cd::ecs    # phase467: World + Entity bindings
    cd::scene  # phase467: LocalTransform read/write bindings
)
# Lua is a PRIVATE dep: the public header MUST NOT expose Lua types so
# downstream consumers don't transitively depend on lua.h. The .cpp
# wraps the C API behind opaque void* state.
target_link_libraries(cd_script PRIVATE cd_lua_internal)

if(CD_ENABLE_TESTING)
  add_subdirectory(tests)
endif()
