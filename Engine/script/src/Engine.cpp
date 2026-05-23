// =============================================================================
// CHROMODYNAMIC — engine/script/src/Engine.cpp
//
// Lua 5.4 wrapped behind an opaque PIMPL so consumers don't include
// lua.h. Wave 71 lands the skeleton (ctor/dtor + valid()); Wave 72+
// fills in run_string / run_file / type bindings.
// =============================================================================
#include <cd/script/Engine.hpp>

extern "C" {
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
}

#include <cstdint>
#include <string>

namespace cd::script
{

struct Engine::Impl
{
    lua_State* L { nullptr };
    std::uint64_t script_count { 0 };
    std::string last_error;  ///< Most recent Lua-side error message.
};

namespace
{

void capture_top_error(lua_State* L, std::string& dst)
{
    if (L == nullptr)
        return;
    const char* msg = ::lua_tostring(L, -1);
    if (msg != nullptr)
        dst.assign(msg);
    else
        dst.clear();
    ::lua_pop(L, 1);
}

}  // namespace

Engine::Engine() : impl_ { std::make_unique<Impl>() }
{
    impl_->L = ::luaL_newstate();
    if (impl_->L != nullptr)
    {
        ::luaL_openlibs(impl_->L);
    }
}

Engine::~Engine()
{
    if (impl_ && impl_->L != nullptr)
        ::lua_close(impl_->L);
}

Engine::Engine(Engine&&) noexcept = default;
Engine& Engine::operator=(Engine&&) noexcept = default;

bool Engine::valid() const noexcept
{
    return impl_ != nullptr && impl_->L != nullptr;
}

cd::core::Result<void> Engine::run_string(std::string_view source)
{
    if (!valid())
        return std::unexpected(script_errors::make(script_errors::Code::kAllocFailed));

    // luaL_loadbufferx compiles the chunk; lua_pcall runs it. Both
    // push an error message on the stack on failure — pop and copy it
    // into the ErrorCode message string for upstream diagnostics.
    const std::string src { source };
    const int load_rc = ::luaL_loadbufferx(
        impl_->L, src.data(), src.size(), "=run_string", nullptr);
    if (load_rc != LUA_OK)
    {
        capture_top_error(impl_->L, impl_->last_error);
        return std::unexpected(script_errors::make(
            script_errors::Code::kCompileError));
    }
    const int call_rc = ::lua_pcall(impl_->L, 0, 0, 0);
    if (call_rc != LUA_OK)
    {
        capture_top_error(impl_->L, impl_->last_error);
        return std::unexpected(script_errors::make(
            script_errors::Code::kRuntimeError));
    }
    impl_->last_error.clear();
    ++impl_->script_count;
    return {};
}

cd::core::Result<void> Engine::run_file(std::string_view path)
{
    if (!valid())
        return std::unexpected(script_errors::make(script_errors::Code::kAllocFailed));

    const std::string p { path };
    const int load_rc = ::luaL_loadfilex(impl_->L, p.c_str(), nullptr);
    if (load_rc == LUA_ERRFILE)
    {
        capture_top_error(impl_->L, impl_->last_error);
        return std::unexpected(script_errors::make(
            script_errors::Code::kFileNotFound));
    }
    if (load_rc != LUA_OK)
    {
        capture_top_error(impl_->L, impl_->last_error);
        return std::unexpected(script_errors::make(
            script_errors::Code::kCompileError));
    }
    const int call_rc = ::lua_pcall(impl_->L, 0, 0, 0);
    if (call_rc != LUA_OK)
    {
        capture_top_error(impl_->L, impl_->last_error);
        return std::unexpected(script_errors::make(
            script_errors::Code::kRuntimeError));
    }
    impl_->last_error.clear();
    ++impl_->script_count;
    return {};
}

std::uint64_t Engine::script_count() const noexcept
{
    return impl_ ? impl_->script_count : 0;
}

// ---- Global variable bindings (Wave 73) -----------------------------------

void Engine::set_global(std::string_view name, double value)
{
    if (!valid())
        return;
    ::lua_pushnumber(impl_->L, value);
    const std::string n { name };
    ::lua_setglobal(impl_->L, n.c_str());
}

void Engine::set_global(std::string_view name, std::string_view value)
{
    if (!valid())
        return;
    ::lua_pushlstring(impl_->L, value.data(), value.size());
    const std::string n { name };
    ::lua_setglobal(impl_->L, n.c_str());
}

void Engine::set_global(std::string_view name, bool value)
{
    if (!valid())
        return;
    ::lua_pushboolean(impl_->L, value ? 1 : 0);
    const std::string n { name };
    ::lua_setglobal(impl_->L, n.c_str());
}

void Engine::set_global(std::string_view name, const char* value)
{
    set_global(name, std::string_view { value });
}

std::optional<double> Engine::get_global_number(std::string_view name)
{
    if (!valid())
        return std::nullopt;
    const std::string n { name };
    ::lua_getglobal(impl_->L, n.c_str());
    std::optional<double> r;
    if (::lua_isnumber(impl_->L, -1) != 0)
        r = ::lua_tonumber(impl_->L, -1);
    ::lua_pop(impl_->L, 1);
    return r;
}

std::optional<std::string> Engine::get_global_string(std::string_view name)
{
    if (!valid())
        return std::nullopt;
    const std::string n { name };
    ::lua_getglobal(impl_->L, n.c_str());
    std::optional<std::string> r;
    if (::lua_isstring(impl_->L, -1) != 0 && ::lua_isnumber(impl_->L, -1) == 0)
    {
        // lua_isstring returns true for numbers too (they're convertible);
        // exclude that case so callers get clean type discrimination.
        std::size_t len = 0;
        const char* s = ::lua_tolstring(impl_->L, -1, &len);
        if (s != nullptr)
            r = std::string { s, len };
    }
    ::lua_pop(impl_->L, 1);
    return r;
}

std::optional<bool> Engine::get_global_bool(std::string_view name)
{
    if (!valid())
        return std::nullopt;
    const std::string n { name };
    ::lua_getglobal(impl_->L, n.c_str());
    std::optional<bool> r;
    if (lua_isboolean(impl_->L, -1))
        r = ::lua_toboolean(impl_->L, -1) != 0;
    ::lua_pop(impl_->L, 1);
    return r;
}

std::string_view Engine::last_error() const noexcept
{
    return impl_ ? std::string_view { impl_->last_error } : std::string_view {};
}

}  // namespace cd::script
