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
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace cd::script
{

/// Heterogeneous Lua-side value: number, string, or bool. Sufficient
/// for typical game-script callback patterns (config arrays, mixed
/// argument lists, multi-typed return tuples). Lua tables and
/// nested objects stay outside the variant for v1 — callers that
/// need tables use set_global / get_global as a side channel
/// (Wave 9+ wave can extend if needed).
using LuaValue = std::variant<double, std::string, bool>;

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

    // ---- Global variable bindings (Wave 73) -----------------------------

    /// Set a Lua global to a primitive C++ value.
    void set_global(std::string_view name, double value);
    void set_global(std::string_view name, std::string_view value);
    void set_global(std::string_view name, bool value);
    /// Convenience overload — disambiguates string literal vs. bool.
    void set_global(std::string_view name, const char* value);

    /// Pull a Lua global as a typed value. Returns nullopt if the
    /// global is unset or holds a value of an incompatible type.
    [[nodiscard]] std::optional<double> get_global_number(std::string_view name);
    [[nodiscard]] std::optional<std::string> get_global_string(std::string_view name);
    [[nodiscard]] std::optional<bool> get_global_bool(std::string_view name);

    /// Most recent Lua error message captured during the last failing
    /// `run_*` call. Empty string when no error has occurred since
    /// construction (or since the last successful run). Stable across
    /// returns — the Engine owns the backing storage.
    [[nodiscard]] std::string_view last_error() const noexcept;

    // ---- Function bindings (Wave 74) ------------------------------------

    using VoidFunction = std::function<void()>;

    /// Bind a C++ callable to a Lua global of the given name. When Lua
    /// code calls `name()` the closure runs on the calling thread; no
    /// arguments are passed and no return value is produced. Sufficient
    /// for the common "Lua triggers a C++ side effect" pattern.
    void register_function(std::string_view name, VoidFunction fn);

    /// Call a Lua global function with no args / no return value.
    /// Returns kRuntimeError + a populated last_error() if the call
    /// failed (e.g. the named global is nil or not callable).
    [[nodiscard]] cd::core::Result<void> call_global(std::string_view name);

    /// Numeric-call entry: pushes each `args` value as a Lua number,
    /// invokes the global, then pops `expected_returns` numeric
    /// results back into `out`. Sufficient for "config callback
    /// returns N doubles" patterns; richer types (strings, tables)
    /// keep using set/get_global as the side channel.
    [[nodiscard]] cd::core::Result<void>
    call_global_numeric(std::string_view name,
                        std::span<const double> args,
                        std::vector<double>& out,
                        std::uint32_t expected_returns = 1);

    /// Heterogeneous-type call entry. Pushes each `args` value with
    /// its variant-tagged Lua type (number, string, or bool),
    /// invokes the global, then pops `expected_returns` results
    /// back into `out` as LuaValue variants. The popped variant
    /// reflects the Lua value's actual type at that stack slot;
    /// mismatches between expected and actual count return
    /// kRuntimeError + populated last_error().
    [[nodiscard]] cd::core::Result<void>
    call_global_mixed(std::string_view name,
                      std::span<const LuaValue> args,
                      std::vector<LuaValue>& out,
                      std::uint32_t expected_returns = 1);

    // ---- Sandboxing (Wave 92) -------------------------------------------

    /// Cap the number of Lua VM instructions any subsequent `run_*` /
    /// `call_global*` call is allowed to execute. The next call that
    /// exceeds the cap is aborted with kRuntimeError + a populated
    /// `last_error()` message describing the limit. Pass 0 to clear
    /// the cap (unlimited — the default).
    void set_instruction_cap(std::uint64_t max_instructions) noexcept;
    [[nodiscard]] std::uint64_t instruction_cap() const noexcept;

    // ---- Internal access for binding libraries (Phase 467) --------------
    //
    // Returns the raw `lua_State*` typed as `void*` so callers don't pull
    // `lua.h` through this public header. The cd/script/Bindings.hpp layer
    // includes `lua.h` inside its TU and `static_cast<lua_State*>` the
    // result. Stable null when valid() is false.
    [[nodiscard]] void* native_state() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace cd::script
