// =============================================================================
// CHROMODYNAMIC — cd/script/Engine.hpp
// Phase 7 / Wave 71 — Lua 5.4 scripting tier (PIMPL + opaque state).
//
// `cd::script::Engine` wraps a `lua_State*` so consumers can compile and
// run script strings + files without including `lua.h` themselves. The
// public header is C++23-only; Lua headers stay in the .cpp.
//
// Scope at v0.12.0 (Sprint 7 closure):
//   * Wave 71 (THIS) — Engine ctor/dtor, error domain, library skeleton.
//   * Wave 72 — `run_string(src)` + `run_file(path)`: compile + execute,
//                surface Lua errors as cd::core::Result.
//   * Wave 73 — Type bindings: get/set globals (number, string, bool).
//   * Wave 74 — Function registration: bind a C++ callable, callable
//                back through `call_global(name, args)`.
//   * Wave 75 — hello_script sample.
//
// Sandboxing strategy: each Engine owns its own lua_State. There is NO
// shared global state. To run two scripts in isolation, make two
// Engine instances. Per-Engine memory + step limits land in Sprint 7+.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <cd/core/ErrorCode.hpp>
#include <cd/core/Result.hpp>

#include <cstdint>
#include <memory>
#include <string_view>

namespace cd::script
{

namespace script_errors
{
inline constexpr std::uint32_t kDomain = 0x001C;

enum class Code : std::uint32_t
{
    kOk = 0,
    kAllocFailed = 1,
    kCompileError = 2,
    kRuntimeError = 3,
    kFileNotFound = 4,
    kInvalidArgument = 5,
};

[[nodiscard]] inline cd::core::ErrorCode make(Code c, std::string_view m = {}) noexcept
{
    return cd::core::ErrorCode { kDomain, static_cast<std::uint32_t>(c), m };
}
}  // namespace script_errors

class Engine
{
public:
    Engine();
    ~Engine();

    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;
    Engine(Engine&&) noexcept;
    Engine& operator=(Engine&&) noexcept;

    /// True when the underlying Lua state was constructed successfully.
    /// A false return indicates allocation failure inside `luaL_newstate`
    /// (e.g. the system is out of memory) — every other method becomes
    /// a no-op returning `kAllocFailed` in that state.
    [[nodiscard]] bool valid() const noexcept;

    /// Reserved for Wave 72.
    [[nodiscard]] cd::core::Result<void> run_string(std::string_view source);

    /// Reserved for Wave 72.
    [[nodiscard]] cd::core::Result<void> run_file(std::string_view path);

    /// Diagnostic counter — incremented on every successful `run_*` call.
    [[nodiscard]] std::uint64_t script_count() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace cd::script
