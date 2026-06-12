// =============================================================================
// CHROMODYNAMIC — engine/script/src/Bindings.cpp
// Phase 467 — Lua bindings for cd::math, cd::ecs, cd::scene + event hooks.
//
// Implementation notes:
//   * Each bound C++ type owns a dedicated Lua metatable, looked up in
//     the registry via a fixed light-userdata key (the address of an
//     `extern const char` sentinel inside this TU; the address is stable
//     and globally unique).
//   * Vec3f / Vec4f / Mat4f are stored by value inside a Lua userdata
//     block — no heap allocation, no pointer aliasing.
//   * World pointers are passed in via `cd.bind_world(world)` from C++
//     before scripts run; the Lua side never owns the World lifetime.
//   * Event handlers are stored in a registry-anchored table indexed by
//     event name → array of {id, ref}. The integer `ref` is a
//     `luaL_ref(LUA_REGISTRYINDEX)` handle to the function value.
//   * Type errors from binding helpers return `nil, "message"` (two
//     return values) so call sites can do `local x, err = cd.foo(...)`.
// =============================================================================
#include <cd/script/Bindings.hpp>

#include <cd/script/Engine.hpp>

#include <cd/math/Matrix.hpp>
#include <cd/math/Quaternion.hpp>
#include <cd/math/Transform.hpp>
#include <cd/math/Vector.hpp>

#include <cd/ecs/Entity.hpp>
#include <cd/ecs/World.hpp>

#include <cd/scene/Scene.hpp>

extern "C" {
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
}

#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace cd::script
{

// ---- Metatable registry keys ---------------------------------------------
//
// Each `extern const char` sentinel has a stable address that we use as a
// light-userdata key into LUA_REGISTRYINDEX. Reusing a single key across
// modules is safe because the address is unique within the linked binary.
namespace
{
constexpr const char kVec3MetaKey [] = "cd.script.Vec3f";
constexpr const char kVec4MetaKey [] = "cd.script.Vec4f";
constexpr const char kMat4MetaKey [] = "cd.script.Mat4f";
constexpr const char kWorldMetaKey[] = "cd.script.World";
constexpr const char kStateRegistryKey[] = "cd.script.BindingState";
constexpr const char kEventsRegistryKey[] = "cd.script.EventTable";
}  // namespace

// ---- Per-Engine state -----------------------------------------------------
//
// Owned via unique_ptr inside an EngineExtension shim (see end of file)
// which the Engine moves with. For Phase 467 we attach the state via a
// thread-local map keyed on the lua_State*; one entry per Engine.

class BindingState
{
public:
    explicit BindingState(lua_State* L) noexcept : L_ { L } {}

    [[nodiscard]] lua_State* state() const noexcept { return L_; }

    /// Next unique handler id. Monotonic counter, never reused.
    [[nodiscard]] std::uint32_t next_handler_id() noexcept { return ++next_id_; }

private:
    lua_State* L_ { nullptr };
    std::uint32_t next_id_ { 0 };
};

namespace
{

// Map from lua_State* → owning BindingState. Engines outlive their bound
// state via this map; clear_state_for(L) is called by a __gc closure
// attached to a tiny "sentinel" userdata in the registry.
std::unordered_map<lua_State*, std::unique_ptr<BindingState>>&
state_map()
{
    static std::unordered_map<lua_State*, std::unique_ptr<BindingState>> m;
    return m;
}

BindingState* state_for(lua_State* L)
{
    auto& m = state_map();
    auto it = m.find(L);
    return it != m.end() ? it->second.get() : nullptr;
}

extern "C" int sentinel_gc(lua_State* L)
{
    // The sentinel userdata is collected when the lua_State closes.
    // Tear down the matching BindingState entry.
    auto& m = state_map();
    m.erase(L);
    return 0;
}

// ---- Userdata helpers ----------------------------------------------------

// Push a fresh userdata containing `T` constructed from args, set its
// metatable, and leave it on top of the stack. Returns the userdata
// pointer for follow-up writes.
template <class T>
T* push_userdata(lua_State* L, const char* meta_key, const T& value)
{
    void* ud = ::lua_newuserdatauv(L, sizeof(T), 0);
    ::new (ud) T { value };  // placement new; trivially destructible POD
    ::luaL_setmetatable(L, meta_key);
    return static_cast<T*>(ud);
}

template <class T>
T* check_userdata(lua_State* L, int idx, const char* meta_key)
{
    void* ud = ::luaL_checkudata(L, idx, meta_key);
    return static_cast<T*>(ud);
}

template <class T>
T* test_userdata(lua_State* L, int idx, const char* meta_key)
{
    void* ud = ::luaL_testudata(L, idx, meta_key);
    return static_cast<T*>(ud);
}

/// Push a float as a lua_Number with explicit double promotion so the
/// build does not trip on -Wdouble-promotion.
inline void push_float(lua_State* L, float v)
{
    ::lua_pushnumber(L, static_cast<lua_Number>(static_cast<double>(v)));
}

// ---- Vec3f bindings ------------------------------------------------------

using Vec3f = cd::math::Vec3f;
using Vec4f = cd::math::Vec4f;
using Mat4f = cd::math::Mat4f;

extern "C" int vec3_new(lua_State* L)
{
    const auto x = static_cast<float>(::luaL_optnumber(L, 1, 0.0));
    const auto y = static_cast<float>(::luaL_optnumber(L, 2, 0.0));
    const auto z = static_cast<float>(::luaL_optnumber(L, 3, 0.0));
    push_userdata<Vec3f>(L, kVec3MetaKey, Vec3f { x, y, z });
    return 1;
}

extern "C" int vec3_add(lua_State* L)
{
    auto* a = check_userdata<Vec3f>(L, 1, kVec3MetaKey);
    auto* b = check_userdata<Vec3f>(L, 2, kVec3MetaKey);
    push_userdata<Vec3f>(L, kVec3MetaKey, *a + *b);
    return 1;
}

extern "C" int vec3_sub(lua_State* L)
{
    auto* a = check_userdata<Vec3f>(L, 1, kVec3MetaKey);
    auto* b = check_userdata<Vec3f>(L, 2, kVec3MetaKey);
    push_userdata<Vec3f>(L, kVec3MetaKey, *a - *b);
    return 1;
}

extern "C" int vec3_mul(lua_State* L)
{
    // Accept Vec3 * Vec3 (component-wise) or Vec3 * number (scalar).
    auto* a = test_userdata<Vec3f>(L, 1, kVec3MetaKey);
    auto* b = test_userdata<Vec3f>(L, 2, kVec3MetaKey);
    if (a != nullptr && b != nullptr)
    {
        push_userdata<Vec3f>(L, kVec3MetaKey, *a * *b);
        return 1;
    }
    if (a != nullptr && ::lua_isnumber(L, 2) != 0)
    {
        const auto s = static_cast<float>(::lua_tonumber(L, 2));
        push_userdata<Vec3f>(L, kVec3MetaKey, *a * s);
        return 1;
    }
    if (b != nullptr && ::lua_isnumber(L, 1) != 0)
    {
        const auto s = static_cast<float>(::lua_tonumber(L, 1));
        push_userdata<Vec3f>(L, kVec3MetaKey, s * *b);
        return 1;
    }
    return ::luaL_error(L, "Vec3f __mul: expected Vec3f or number operand");
}

extern "C" int vec3_index(lua_State* L)
{
    auto* v = check_userdata<Vec3f>(L, 1, kVec3MetaKey);
    const char* key = luaL_checkstring(L, 2);
    if (std::strcmp(key, "x") == 0) { push_float(L, v->x); return 1; }
    if (std::strcmp(key, "y") == 0) { push_float(L, v->y); return 1; }
    if (std::strcmp(key, "z") == 0) { push_float(L, v->z); return 1; }
    ::lua_pushnil(L);
    return 1;
}

extern "C" int vec3_newindex(lua_State* L)
{
    auto* v = check_userdata<Vec3f>(L, 1, kVec3MetaKey);
    const char* key = luaL_checkstring(L, 2);
    const auto n = static_cast<float>(::luaL_checknumber(L, 3));
    if (std::strcmp(key, "x") == 0) v->x = n;
    else if (std::strcmp(key, "y") == 0) v->y = n;
    else if (std::strcmp(key, "z") == 0) v->z = n;
    else return ::luaL_error(L, "Vec3f: unknown field '%s'", key);
    return 0;
}

extern "C" int vec3_tostring(lua_State* L)
{
    auto* v = check_userdata<Vec3f>(L, 1, kVec3MetaKey);
    ::lua_pushfstring(L, "Vec3f(%f, %f, %f)",
                      static_cast<double>(v->x),
                      static_cast<double>(v->y),
                      static_cast<double>(v->z));
    return 1;
}

// ---- Vec4f bindings ------------------------------------------------------

extern "C" int vec4_new(lua_State* L)
{
    const auto x = static_cast<float>(::luaL_optnumber(L, 1, 0.0));
    const auto y = static_cast<float>(::luaL_optnumber(L, 2, 0.0));
    const auto z = static_cast<float>(::luaL_optnumber(L, 3, 0.0));
    const auto w = static_cast<float>(::luaL_optnumber(L, 4, 0.0));
    push_userdata<Vec4f>(L, kVec4MetaKey, Vec4f { x, y, z, w });
    return 1;
}

extern "C" int vec4_add(lua_State* L)
{
    auto* a = check_userdata<Vec4f>(L, 1, kVec4MetaKey);
    auto* b = check_userdata<Vec4f>(L, 2, kVec4MetaKey);
    push_userdata<Vec4f>(L, kVec4MetaKey, *a + *b);
    return 1;
}

extern "C" int vec4_sub(lua_State* L)
{
    auto* a = check_userdata<Vec4f>(L, 1, kVec4MetaKey);
    auto* b = check_userdata<Vec4f>(L, 2, kVec4MetaKey);
    push_userdata<Vec4f>(L, kVec4MetaKey, *a - *b);
    return 1;
}

extern "C" int vec4_mul(lua_State* L)
{
    auto* a = test_userdata<Vec4f>(L, 1, kVec4MetaKey);
    auto* b = test_userdata<Vec4f>(L, 2, kVec4MetaKey);
    if (a != nullptr && b != nullptr)
    {
        push_userdata<Vec4f>(L, kVec4MetaKey, *a * *b);
        return 1;
    }
    if (a != nullptr && ::lua_isnumber(L, 2) != 0)
    {
        const auto s = static_cast<float>(::lua_tonumber(L, 2));
        push_userdata<Vec4f>(L, kVec4MetaKey, *a * s);
        return 1;
    }
    if (b != nullptr && ::lua_isnumber(L, 1) != 0)
    {
        const auto s = static_cast<float>(::lua_tonumber(L, 1));
        push_userdata<Vec4f>(L, kVec4MetaKey, s * *b);
        return 1;
    }
    return ::luaL_error(L, "Vec4f __mul: expected Vec4f or number operand");
}

extern "C" int vec4_index(lua_State* L)
{
    auto* v = check_userdata<Vec4f>(L, 1, kVec4MetaKey);
    const char* key = luaL_checkstring(L, 2);
    if (std::strcmp(key, "x") == 0) { push_float(L, v->x); return 1; }
    if (std::strcmp(key, "y") == 0) { push_float(L, v->y); return 1; }
    if (std::strcmp(key, "z") == 0) { push_float(L, v->z); return 1; }
    if (std::strcmp(key, "w") == 0) { push_float(L, v->w); return 1; }
    ::lua_pushnil(L);
    return 1;
}

extern "C" int vec4_newindex(lua_State* L)
{
    auto* v = check_userdata<Vec4f>(L, 1, kVec4MetaKey);
    const char* key = luaL_checkstring(L, 2);
    const auto n = static_cast<float>(::luaL_checknumber(L, 3));
    if (std::strcmp(key, "x") == 0) v->x = n;
    else if (std::strcmp(key, "y") == 0) v->y = n;
    else if (std::strcmp(key, "z") == 0) v->z = n;
    else if (std::strcmp(key, "w") == 0) v->w = n;
    else return ::luaL_error(L, "Vec4f: unknown field '%s'", key);
    return 0;
}

extern "C" int vec4_tostring(lua_State* L)
{
    auto* v = check_userdata<Vec4f>(L, 1, kVec4MetaKey);
    ::lua_pushfstring(L, "Vec4f(%f, %f, %f, %f)",
                      static_cast<double>(v->x),
                      static_cast<double>(v->y),
                      static_cast<double>(v->z),
                      static_cast<double>(v->w));
    return 1;
}

// ---- Mat4f bindings ------------------------------------------------------

extern "C" int mat4_identity(lua_State* L)
{
    push_userdata<Mat4f>(L, kMat4MetaKey, Mat4f::identity());
    return 1;
}

extern "C" int mat4_mul(lua_State* L)
{
    auto* a = test_userdata<Mat4f>(L, 1, kMat4MetaKey);
    auto* b = test_userdata<Mat4f>(L, 2, kMat4MetaKey);
    if (a != nullptr && b != nullptr)
    {
        push_userdata<Mat4f>(L, kMat4MetaKey, *a * *b);
        return 1;
    }
    // Mat4 * Vec4 → Vec4
    if (a != nullptr)
    {
        auto* v = test_userdata<Vec4f>(L, 2, kVec4MetaKey);
        if (v != nullptr)
        {
            push_userdata<Vec4f>(L, kVec4MetaKey, *a * *v);
            return 1;
        }
    }
    return ::luaL_error(L, "Mat4f __mul: expected Mat4f or Vec4f operand");
}

extern "C" int mat4_tostring(lua_State* L)
{
    auto* m = check_userdata<Mat4f>(L, 1, kMat4MetaKey);
    ::lua_pushfstring(L, "Mat4f(c0=(%f,%f,%f,%f), ...)",
                      static_cast<double>((*m)[0].x),
                      static_cast<double>((*m)[0].y),
                      static_cast<double>((*m)[0].z),
                      static_cast<double>((*m)[0].w));
    return 1;
}

// ---- World bindings ------------------------------------------------------

// Userdata storing a non-owning pointer to a cd::ecs::World. Lifetime is
// the caller's responsibility (C++ keeps the World alive across script
// execution).
struct WorldHandle
{
    cd::ecs::World* world { nullptr };
};

// Entity is encoded as a Lua table { id = number, generation = number }
// rather than another userdata to keep the binding simple and to make
// `is_alive` test trivial from script.
void push_entity_table(lua_State* L, cd::ecs::Entity e)
{
    ::lua_createtable(L, 0, 2);
    ::lua_pushinteger(L, static_cast<lua_Integer>(e.id));
    ::lua_setfield(L, -2, "id");
    ::lua_pushinteger(L, static_cast<lua_Integer>(e.generation));
    ::lua_setfield(L, -2, "generation");
}

cd::ecs::Entity entity_from_table(lua_State* L, int idx)
{
    cd::ecs::Entity e {};
    if (lua_istable(L, idx) == 0)
        return e;
    ::lua_getfield(L, idx, "id");
    if (::lua_isnumber(L, -1) != 0)
        e.id = static_cast<std::uint32_t>(::lua_tointeger(L, -1));
    ::lua_pop(L, 1);
    ::lua_getfield(L, idx, "generation");
    if (::lua_isnumber(L, -1) != 0)
        e.generation = static_cast<std::uint32_t>(::lua_tointeger(L, -1));
    ::lua_pop(L, 1);
    return e;
}

extern "C" int world_create(lua_State* L)
{
    auto* h = check_userdata<WorldHandle>(L, 1, kWorldMetaKey);
    if (h->world == nullptr)
    {
        ::lua_pushnil(L);
        ::lua_pushstring(L, "World handle is null");
        return 2;
    }
    push_entity_table(L, h->world->create());
    return 1;
}

extern "C" int world_destroy(lua_State* L)
{
    auto* h = check_userdata<WorldHandle>(L, 1, kWorldMetaKey);
    if (h->world == nullptr)
    {
        ::lua_pushnil(L);
        ::lua_pushstring(L, "World handle is null");
        return 2;
    }
    const cd::ecs::Entity e = entity_from_table(L, 2);
    h->world->destroy(e);
    return 0;
}

extern "C" int world_is_alive(lua_State* L)
{
    auto* h = check_userdata<WorldHandle>(L, 1, kWorldMetaKey);
    if (h->world == nullptr)
    {
        ::lua_pushnil(L);
        ::lua_pushstring(L, "World handle is null");
        return 2;
    }
    const cd::ecs::Entity e = entity_from_table(L, 2);
    ::lua_pushboolean(L, h->world->is_alive(e) ? 1 : 0);
    return 1;
}

// ---- LocalTransform read/write -------------------------------------------
//
// Returns { position = Vec3f, rotation = { x, y, z, w }, scale = Vec3f }
// on success; (nil, "error") on missing component or invalid entity.

void push_quat_table(lua_State* L, const cd::math::Quat<float>& q)
{
    ::lua_createtable(L, 0, 4);
    push_float(L, q.x); ::lua_setfield(L, -2, "x");
    push_float(L, q.y); ::lua_setfield(L, -2, "y");
    push_float(L, q.z); ::lua_setfield(L, -2, "z");
    push_float(L, q.w); ::lua_setfield(L, -2, "w");
}

cd::math::Quat<float> quat_from_table(lua_State* L, int idx)
{
    cd::math::Quat<float> q {};
    if (lua_istable(L, idx) == 0)
        return q;
    ::lua_getfield(L, idx, "x"); if (::lua_isnumber(L, -1) != 0) q.x = static_cast<float>(::lua_tonumber(L, -1)); ::lua_pop(L, 1);
    ::lua_getfield(L, idx, "y"); if (::lua_isnumber(L, -1) != 0) q.y = static_cast<float>(::lua_tonumber(L, -1)); ::lua_pop(L, 1);
    ::lua_getfield(L, idx, "z"); if (::lua_isnumber(L, -1) != 0) q.z = static_cast<float>(::lua_tonumber(L, -1)); ::lua_pop(L, 1);
    ::lua_getfield(L, idx, "w"); if (::lua_isnumber(L, -1) != 0) q.w = static_cast<float>(::lua_tonumber(L, -1)); ::lua_pop(L, 1);
    return q;
}

Vec3f vec3_from_field(lua_State* L, int parent_idx, const char* field)
{
    Vec3f r {};
    ::lua_getfield(L, parent_idx, field);
    if (auto* ud = test_userdata<Vec3f>(L, -1, kVec3MetaKey))
        r = *ud;
    ::lua_pop(L, 1);
    return r;
}

extern "C" int world_get_local_transform(lua_State* L)
{
    auto* h = check_userdata<WorldHandle>(L, 1, kWorldMetaKey);
    if (h->world == nullptr)
    {
        ::lua_pushnil(L);
        ::lua_pushstring(L, "World handle is null");
        return 2;
    }
    const cd::ecs::Entity e = entity_from_table(L, 2);
    if (!h->world->is_alive(e))
    {
        ::lua_pushnil(L);
        ::lua_pushstring(L, "Entity is not alive");
        return 2;
    }
    auto* lt = h->world->get<cd::scene::LocalTransform>(e);
    if (lt == nullptr)
    {
        ::lua_pushnil(L);
        ::lua_pushstring(L, "Entity has no LocalTransform component");
        return 2;
    }
    ::lua_createtable(L, 0, 3);
    push_userdata<Vec3f>(L, kVec3MetaKey, lt->value.position);
    ::lua_setfield(L, -2, "position");
    push_quat_table(L, lt->value.rotation);
    ::lua_setfield(L, -2, "rotation");
    push_userdata<Vec3f>(L, kVec3MetaKey, lt->value.scale);
    ::lua_setfield(L, -2, "scale");
    return 1;
}

extern "C" int world_set_local_transform(lua_State* L)
{
    auto* h = check_userdata<WorldHandle>(L, 1, kWorldMetaKey);
    if (h->world == nullptr)
    {
        ::lua_pushnil(L);
        ::lua_pushstring(L, "World handle is null");
        return 2;
    }
    const cd::ecs::Entity e = entity_from_table(L, 2);
    if (!h->world->is_alive(e))
    {
        ::lua_pushnil(L);
        ::lua_pushstring(L, "Entity is not alive");
        return 2;
    }
    if (lua_istable(L, 3) == 0)
    {
        ::lua_pushnil(L);
        ::lua_pushstring(L, "Third arg must be a LocalTransform table");
        return 2;
    }
    cd::scene::LocalTransform lt {};
    lt.value.position = vec3_from_field(L, 3, "position");
    lt.value.scale    = vec3_from_field(L, 3, "scale");
    ::lua_getfield(L, 3, "rotation");
    lt.value.rotation = quat_from_table(L, -1);
    ::lua_pop(L, 1);
    h->world->emplace<cd::scene::LocalTransform>(e, lt);
    ::lua_pushboolean(L, 1);
    return 1;
}

// ---- Event-handler bindings ----------------------------------------------
//
// Registry table layout (stored under kEventsRegistryKey):
//   events[event_name] = { { id = N, ref = R }, { id = N+1, ref = R' }, ... }

void ensure_events_table(lua_State* L)
{
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast) — Lua registry-key idiom (void* key).
    ::lua_pushlightuserdata(L, const_cast<char*>(kEventsRegistryKey));
    ::lua_rawget(L, LUA_REGISTRYINDEX);
    if (lua_isnil(L, -1))
    {
        ::lua_pop(L, 1);
        ::lua_newtable(L);
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast) — same idiom.
        ::lua_pushlightuserdata(L, const_cast<char*>(kEventsRegistryKey));
        ::lua_pushvalue(L, -2);
        ::lua_rawset(L, LUA_REGISTRYINDEX);
    }
    // events table is now on top of stack.
}

extern "C" int cd_register_handler(lua_State* L)
{
    const char* name = luaL_checkstring(L, 1);
    if (lua_isfunction(L, 2) == 0)
    {
        ::lua_pushnil(L);
        ::lua_pushstring(L, "register_handler: second arg must be a function");
        return 2;
    }

    auto* st = state_for(L);
    if (st == nullptr)
    {
        ::lua_pushnil(L);
        ::lua_pushstring(L, "register_handler: binding state not installed");
        return 2;
    }

    ensure_events_table(L);                          // [events]
    ::lua_getfield(L, -1, name);                     // [events, list?]
    if (lua_isnil(L, -1))
    {
        ::lua_pop(L, 1);
        ::lua_newtable(L);
        ::lua_pushvalue(L, -1);
        ::lua_setfield(L, -3, name);
    }
    // stack: [events, list]
    const auto list_len = static_cast<lua_Integer>(::lua_rawlen(L, -1));
    ::lua_createtable(L, 0, 2);                      // [events, list, entry]
    ::lua_pushvalue(L, 2);                           // function
    const int ref = ::luaL_ref(L, LUA_REGISTRYINDEX);
    const std::uint32_t id = st->next_handler_id();
    ::lua_pushinteger(L, static_cast<lua_Integer>(id));
    ::lua_setfield(L, -2, "id");
    ::lua_pushinteger(L, static_cast<lua_Integer>(ref));
    ::lua_setfield(L, -2, "ref");
    ::lua_rawseti(L, -2, list_len + 1);              // list[list_len+1] = entry, pops entry
    ::lua_pop(L, 2);                                 // pop list + events
    ::lua_pushinteger(L, static_cast<lua_Integer>(id));
    return 1;
}

extern "C" int cd_unregister_handler(lua_State* L)
{
    const char* name = luaL_checkstring(L, 1);
    const auto target_id = static_cast<std::uint32_t>(::luaL_checkinteger(L, 2));

    ensure_events_table(L);                          // [events]
    ::lua_getfield(L, -1, name);                     // [events, list?]
    if (lua_isnil(L, -1))
    {
        ::lua_pop(L, 2);
        ::lua_pushboolean(L, 0);
        return 1;
    }
    const auto n = static_cast<lua_Integer>(::lua_rawlen(L, -1));
    bool removed = false;
    for (lua_Integer i = 1; i <= n; ++i)
    {
        ::lua_rawgeti(L, -1, i);                     // [events, list, entry]
        ::lua_getfield(L, -1, "id");
        const auto entry_id = static_cast<std::uint32_t>(::lua_tointeger(L, -1));
        ::lua_pop(L, 1);
        if (entry_id == target_id)
        {
            ::lua_getfield(L, -1, "ref");
            const int ref = static_cast<int>(::lua_tointeger(L, -1));
            ::lua_pop(L, 1);
            ::luaL_unref(L, LUA_REGISTRYINDEX, ref);
            ::lua_pop(L, 1);                         // pop entry
            // Shift remaining entries down to keep contiguous array.
            for (lua_Integer j = i; j < n; ++j)
            {
                ::lua_rawgeti(L, -1, j + 1);
                ::lua_rawseti(L, -2, j);
            }
            ::lua_pushnil(L);
            ::lua_rawseti(L, -2, n);
            removed = true;
            break;
        }
        ::lua_pop(L, 1);                             // pop entry
    }
    ::lua_pop(L, 2);                                 // pop list + events
    ::lua_pushboolean(L, removed ? 1 : 0);
    return 1;
}

// ---- Metatable installation ----------------------------------------------

void register_vec3_metatable(lua_State* L)
{
    ::luaL_newmetatable(L, kVec3MetaKey);
    static const luaL_Reg meta[] = {
        { "__add",      &vec3_add },
        { "__sub",      &vec3_sub },
        { "__mul",      &vec3_mul },
        { "__index",    &vec3_index },
        { "__newindex", &vec3_newindex },
        { "__tostring", &vec3_tostring },
        { nullptr, nullptr },
    };
    ::luaL_setfuncs(L, meta, 0);
    ::lua_pop(L, 1);  // metatable
}

void register_vec4_metatable(lua_State* L)
{
    ::luaL_newmetatable(L, kVec4MetaKey);
    static const luaL_Reg meta[] = {
        { "__add",      &vec4_add },
        { "__sub",      &vec4_sub },
        { "__mul",      &vec4_mul },
        { "__index",    &vec4_index },
        { "__newindex", &vec4_newindex },
        { "__tostring", &vec4_tostring },
        { nullptr, nullptr },
    };
    ::luaL_setfuncs(L, meta, 0);
    ::lua_pop(L, 1);
}

void register_mat4_metatable(lua_State* L)
{
    ::luaL_newmetatable(L, kMat4MetaKey);
    static const luaL_Reg meta[] = {
        { "__mul",      &mat4_mul },
        { "__tostring", &mat4_tostring },
        { nullptr, nullptr },
    };
    ::luaL_setfuncs(L, meta, 0);
    ::lua_pop(L, 1);
}

void register_world_metatable(lua_State* L)
{
    ::luaL_newmetatable(L, kWorldMetaKey);
    // Methods table → __index
    ::lua_newtable(L);
    static const luaL_Reg methods[] = {
        { "create",              &world_create },
        { "destroy",             &world_destroy },
        { "is_alive",            &world_is_alive },
        { "get_local_transform", &world_get_local_transform },
        { "set_local_transform", &world_set_local_transform },
        { nullptr, nullptr },
    };
    ::luaL_setfuncs(L, methods, 0);
    ::lua_setfield(L, -2, "__index");
    ::lua_pop(L, 1);  // metatable
}

void install_sentinel(lua_State* L)
{
    // Tiny userdata whose __gc clears the global state-map entry when
    // the lua_State is closed. Anchored in the registry so it is never
    // collected before VM teardown.
    void* sentinel = ::lua_newuserdatauv(L, sizeof(char), 0);
    *static_cast<char*>(sentinel) = 0;
    ::lua_createtable(L, 0, 1);
    ::lua_pushcfunction(L, &sentinel_gc);
    ::lua_setfield(L, -2, "__gc");
    ::lua_setmetatable(L, -2);
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast) — same idiom.
    ::lua_pushlightuserdata(L, const_cast<char*>(kStateRegistryKey));
    ::lua_pushvalue(L, -2);
    ::lua_rawset(L, LUA_REGISTRYINDEX);
    ::lua_pop(L, 1);
}

extern "C" int cd_bind_world(lua_State* L)
{
    // cd.bind_world(lightuserdata_or_world_handle)
    if (lua_islightuserdata(L, 1) == 0)
    {
        ::lua_pushnil(L);
        ::lua_pushstring(L, "bind_world: expected light userdata");
        return 2;
    }
    auto* world = static_cast<cd::ecs::World*>(::lua_touserdata(L, 1));
    auto* h = push_userdata<WorldHandle>(L, kWorldMetaKey, WorldHandle { world });
    (void)h;
    return 1;
}

}  // namespace

// ---- Public registration --------------------------------------------------

BindingState* register_bindings(Engine& engine)
{
    if (!engine.valid())
        return nullptr;
    auto* L = static_cast<lua_State*>(engine.native_state());
    if (L == nullptr)
        return nullptr;

    auto& m = state_map();
    auto it = m.find(L);
    if (it != m.end())
        return it->second.get();

    auto state = std::make_unique<BindingState>(L);
    auto* raw = state.get();
    m.emplace(L, std::move(state));

    install_sentinel(L);
    register_vec3_metatable(L);
    register_vec4_metatable(L);
    register_mat4_metatable(L);
    register_world_metatable(L);

    // Build the `cd` global table.
    ::lua_newtable(L);  // cd

    ::lua_pushcfunction(L, &vec3_new);   ::lua_setfield(L, -2, "Vec3f");
    ::lua_pushcfunction(L, &vec4_new);   ::lua_setfield(L, -2, "Vec4f");

    // cd.Mat4f sub-table with .identity()
    ::lua_newtable(L);
    ::lua_pushcfunction(L, &mat4_identity); ::lua_setfield(L, -2, "identity");
    ::lua_setfield(L, -2, "Mat4f");

    ::lua_pushcfunction(L, &cd_bind_world); ::lua_setfield(L, -2, "bind_world");
    ::lua_pushcfunction(L, &cd_register_handler);   ::lua_setfield(L, -2, "register_handler");
    ::lua_pushcfunction(L, &cd_unregister_handler); ::lua_setfield(L, -2, "unregister_handler");

    ::lua_setglobal(L, "cd");

    return raw;
}

std::uint32_t fire_event(BindingState* state, std::string_view event_name)
{
    if (state == nullptr)
        return 0;
    lua_State* L = state->state();
    if (L == nullptr)
        return 0;

    ensure_events_table(L);                         // [events]
    const std::string name { event_name };
    ::lua_getfield(L, -1, name.c_str());            // [events, list?]
    if (lua_isnil(L, -1))
    {
        ::lua_pop(L, 2);
        return 0;
    }
    const auto n = static_cast<lua_Integer>(::lua_rawlen(L, -1));
    std::uint32_t fired = 0;
    for (lua_Integer i = 1; i <= n; ++i)
    {
        ::lua_rawgeti(L, -1, i);                    // [events, list, entry]
        ::lua_getfield(L, -1, "ref");
        const int ref = static_cast<int>(::lua_tointeger(L, -1));
        ::lua_pop(L, 1);
        ::lua_rawgeti(L, LUA_REGISTRYINDEX, ref);   // [events, list, entry, fn]
        const int rc = ::lua_pcall(L, 0, 0, 0);
        if (rc == LUA_OK)
            ++fired;
        else
        {
            // pcall pushed an error message; discard it (the engine's
            // last_error is updated only via run_string/call_global*).
            ::lua_pop(L, 1);
        }
        ::lua_pop(L, 1);                            // pop entry
    }
    ::lua_pop(L, 2);                                // pop list + events
    return fired;
}

std::uint32_t handler_count(const BindingState* state, std::string_view event_name)
{
    if (state == nullptr)
        return 0;
    lua_State* L = state->state();
    if (L == nullptr)
        return 0;
    ensure_events_table(L);
    const std::string name { event_name };
    ::lua_getfield(L, -1, name.c_str());
    std::uint32_t n = 0;
    if (lua_isnil(L, -1) == 0)
        n = static_cast<std::uint32_t>(::lua_rawlen(L, -1));
    ::lua_pop(L, 2);
    return n;
}

bool bind_world(BindingState* state, cd::ecs::World* world,
                std::string_view global_name)
{
    if (state == nullptr || world == nullptr)
        return false;
    lua_State* L = state->state();
    if (L == nullptr)
        return false;
    push_userdata<WorldHandle>(L, kWorldMetaKey, WorldHandle { world });
    const std::string n { global_name };
    ::lua_setglobal(L, n.c_str());
    return true;
}

}  // namespace cd::script
