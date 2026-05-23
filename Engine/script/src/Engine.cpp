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
};

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
        // Drop the Lua-side error message — ErrorCode::message is a
        // string_view that can't own dynamic strings. Engine consumers
        // call last_error() (Wave 73+) to retrieve the full message.
        ::lua_pop(impl_->L, 1);
        return std::unexpected(script_errors::make(
            script_errors::Code::kCompileError));
    }
    const int call_rc = ::lua_pcall(impl_->L, 0, 0, 0);
    if (call_rc != LUA_OK)
    {
        ::lua_pop(impl_->L, 1);
        return std::unexpected(script_errors::make(
            script_errors::Code::kRuntimeError));
    }
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
        ::lua_pop(impl_->L, 1);
        return std::unexpected(script_errors::make(
            script_errors::Code::kFileNotFound));
    }
    if (load_rc != LUA_OK)
    {
        // Drop the Lua-side error message — ErrorCode::message is a
        // string_view that can't own dynamic strings. Engine consumers
        // call last_error() (Wave 73+) to retrieve the full message.
        ::lua_pop(impl_->L, 1);
        return std::unexpected(script_errors::make(
            script_errors::Code::kCompileError));
    }
    const int call_rc = ::lua_pcall(impl_->L, 0, 0, 0);
    if (call_rc != LUA_OK)
    {
        ::lua_pop(impl_->L, 1);
        return std::unexpected(script_errors::make(
            script_errors::Code::kRuntimeError));
    }
    ++impl_->script_count;
    return {};
}

std::uint64_t Engine::script_count() const noexcept
{
    return impl_ ? impl_->script_count : 0;
}

}  // namespace cd::script
