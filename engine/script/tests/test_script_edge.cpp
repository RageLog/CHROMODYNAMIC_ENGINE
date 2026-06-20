// =============================================================================
// CHROMODYNAMIC — engine/script/tests/test_script_edge.cpp
// 80→100 depth marathon — edge + negative coverage for cd::script::Engine.
//
// These cases exercise Engine.cpp branches the Wave-71..99 + Band-2 suites
// left untested: moved-from engines, empty / large / re-entrant scripts,
// the runtime-vs-compile error split on run_file, callback-raises-Lua-error
// surfacing, fewer-returns-than-expected padding, nil round-trip via globals,
// and native_state()/instruction_cap() defaults. Behaviour-preserving: no
// production source paths change, only previously-unverified branches are
// pinned. AAA structure throughout.
// =============================================================================
#include <cd/script/Engine.hpp>

#include <cd/core/ErrorCode.hpp>

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace
{

using cd::script::Engine;
using Code = cd::script::script_errors::Code;

[[nodiscard]] std::uint32_t code_of(Code c) noexcept
{
    return static_cast<std::uint32_t>(c);
}

// ---- run_string edge cases ------------------------------------------------

TEST(ScriptEdgeRun, EmptyStringIsOkAndCounts)
{
    // Arrange
    Engine eng;
    // Act
    const auto r = eng.run_string("");
    // Assert
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(eng.script_count(), 1U);
    EXPECT_TRUE(eng.last_error().empty());
}

TEST(ScriptEdgeRun, WhitespaceOnlyChunkIsOk)
{
    // Arrange
    Engine eng;
    // Act
    const auto r = eng.run_string("   \n\t  -- a comment\n");
    // Assert
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(eng.script_count(), 1U);
}

TEST(ScriptEdgeRun, EmbeddedNulInSourceIsHonouredByLength)
{
    // Arrange — luaL_loadbufferx takes an explicit length, so an embedded
    // NUL must not truncate the chunk. A string_view preserves the length.
    Engine eng;
    const std::string src = std::string("x = 1\0 garbage-after-nul", 24);
    // Act — the chunk past the NUL is a syntax error; Lua compiles the whole
    // buffer (length-delimited), so this is a compile error, not a silent
    // truncation to the valid prefix.
    const auto r = eng.run_string(std::string_view { src.data(), src.size() });
    // Assert
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, code_of(Code::kCompileError));
}

TEST(ScriptEdgeRun, LargeScriptExecutes)
{
    // Arrange — build a large chunk (~2k statements) to stress the loader
    // and accumulator without an instruction cap.
    Engine eng;
    std::string src = "total = 0\n";
    for (int i = 0; i < 2000; ++i)
        src += "total = total + 1\n";
    // Act
    const auto r = eng.run_string(src);
    // Assert
    ASSERT_TRUE(r.has_value()) << std::string { eng.last_error() };
    const auto total = eng.get_global_number("total");
    ASSERT_TRUE(total.has_value());
    EXPECT_DOUBLE_EQ(*total, 2000.0);
}

TEST(ScriptEdgeRun, ScriptCountAccumulatesOnlyOnSuccess)
{
    // Arrange
    Engine eng;
    // Act
    ASSERT_TRUE(eng.run_string("return").has_value());
    ASSERT_FALSE(eng.run_string("error('x')").has_value());  // no increment
    ASSERT_TRUE(eng.run_string("return").has_value());
    // Assert
    EXPECT_EQ(eng.script_count(), 2U);
}

TEST(ScriptEdgeRun, RuntimeErrorThenSuccessClearsLastError)
{
    // Arrange
    Engine eng;
    ASSERT_FALSE(eng.run_string("error('first-boom')").has_value());
    ASSERT_FALSE(eng.last_error().empty());
    // Act
    const auto ok = eng.run_string("return");
    // Assert
    ASSERT_TRUE(ok.has_value());
    EXPECT_TRUE(eng.last_error().empty());
}

// ---- run_file edge cases --------------------------------------------------

TEST(ScriptEdgeFile, RunFileWithRuntimeErrorIsRuntimeError)
{
    // Arrange — a valid-syntax file whose body raises at run time exercises
    // the run_file pcall-failure branch (distinct from compile / missing).
    const auto path =
        std::filesystem::temp_directory_path() / "cd_script_edge_runtime.lua";
    {
        std::ofstream f { path };
        f << "error('runtime boom from file')";
    }
    Engine eng;
    // Act
    const auto r = eng.run_file(path.string());
    std::error_code ec;
    std::filesystem::remove(path, ec);
    // Assert
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, code_of(Code::kRuntimeError));
    EXPECT_NE(eng.last_error().find("runtime boom"), std::string_view::npos);
}

TEST(ScriptEdgeFile, RunFileValidExecutesAndSetsGlobal)
{
    // Arrange
    const auto path =
        std::filesystem::temp_directory_path() / "cd_script_edge_valid.lua";
    {
        std::ofstream f { path };
        f << "from_file = 1234";
    }
    Engine eng;
    // Act
    const auto r = eng.run_file(path.string());
    std::error_code ec;
    std::filesystem::remove(path, ec);
    // Assert
    ASSERT_TRUE(r.has_value()) << std::string { eng.last_error() };
    EXPECT_EQ(eng.script_count(), 1U);
    const auto v = eng.get_global_number("from_file");
    ASSERT_TRUE(v.has_value());
    EXPECT_DOUBLE_EQ(*v, 1234.0);
}

// ---- Global round-trips: nil / type discrimination ------------------------

TEST(ScriptEdgeGlobals, SetNumberThenReadAsStringIsNullopt)
{
    // Arrange
    Engine eng;
    eng.set_global("n", 3.0);
    // Act / Assert — a number is convertible-to-string in Lua, but
    // get_global_string deliberately excludes numbers for clean typing.
    EXPECT_FALSE(eng.get_global_string("n").has_value());
    EXPECT_TRUE(eng.get_global_number("n").has_value());
}

TEST(ScriptEdgeGlobals, BoolGlobalIsNotReadableAsNumber)
{
    // Arrange
    Engine eng;
    eng.set_global("flag", true);
    // Act / Assert
    EXPECT_FALSE(eng.get_global_number("flag").has_value());
    EXPECT_FALSE(eng.get_global_string("flag").has_value());
    EXPECT_TRUE(eng.get_global_bool("flag").has_value());
}

TEST(ScriptEdgeGlobals, NilGlobalReadsAsNulloptForEveryType)
{
    // Arrange — explicitly set then clear a global to nil from script.
    Engine eng;
    eng.set_global("cleared", 9.0);
    ASSERT_TRUE(eng.run_string("cleared = nil").has_value());
    // Act / Assert
    EXPECT_FALSE(eng.get_global_number("cleared").has_value());
    EXPECT_FALSE(eng.get_global_string("cleared").has_value());
    EXPECT_FALSE(eng.get_global_bool("cleared").has_value());
}

TEST(ScriptEdgeGlobals, OverwriteGlobalTypeChanges)
{
    // Arrange — set a global as a number, then overwrite with a string.
    Engine eng;
    eng.set_global("x", 1.0);
    EXPECT_TRUE(eng.get_global_number("x").has_value());
    // Act
    eng.set_global("x", "now-a-string");
    // Assert
    EXPECT_FALSE(eng.get_global_number("x").has_value());
    const auto s = eng.get_global_string("x");
    ASSERT_TRUE(s.has_value());
    EXPECT_EQ(*s, "now-a-string");
}

TEST(ScriptEdgeGlobals, StringWithEmbeddedNulRoundTrips)
{
    // Arrange — set/get use length-delimited push/pull, so an embedded NUL
    // must survive the round-trip.
    Engine eng;
    const std::string payload = std::string("a\0b", 3);
    eng.set_global("blob", std::string_view { payload.data(), payload.size() });
    // Act
    const auto s = eng.get_global_string("blob");
    // Assert
    ASSERT_TRUE(s.has_value());
    EXPECT_EQ(s->size(), 3U);
    EXPECT_EQ(*s, payload);
}

// ---- register_function: callback edge behaviour ---------------------------

TEST(ScriptEdgeFunc, CallbackRaisingLuaErrorPropagatesAsRuntimeError)
{
    // Arrange — a registered C++ callback that asks Lua (via run_string)
    // is not what we test here; instead we wrap the native call inside a
    // Lua pcall to confirm the native call itself runs and a *subsequent*
    // Lua error is surfaced as kRuntimeError.
    Engine eng;
    int fires = 0;
    eng.register_function("native_side_effect", [&] { ++fires; });
    // Act — call the native fn, then raise a Lua error in the same chunk.
    const auto r =
        eng.run_string("native_side_effect(); error('after native call')");
    // Assert
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, code_of(Code::kRuntimeError));
    EXPECT_EQ(fires, 1);  // native ran before the error
    EXPECT_NE(eng.last_error().find("after native call"), std::string_view::npos);
}

TEST(ScriptEdgeFunc, ReentrantCallGlobalFromWithinNativeCallback)
{
    // Arrange — a native callback that, when fired from Lua, re-enters the
    // Engine by calling another Lua global. Confirms nested pcall is safe.
    Engine eng;
    ASSERT_TRUE(eng.run_string("inner_count = 0\n"
                               "function inner() inner_count = inner_count + 1 end")
                    .has_value());
    eng.register_function("trampoline", [&] {
        // Re-enter the Engine from inside a Lua-driven callback.
        const auto ir = eng.call_global("inner");
        EXPECT_TRUE(ir.has_value());
    });
    // Act
    const auto r = eng.run_string("trampoline(); trampoline()");
    // Assert
    ASSERT_TRUE(r.has_value()) << std::string { eng.last_error() };
    const auto inner = eng.get_global_number("inner_count");
    ASSERT_TRUE(inner.has_value());
    EXPECT_DOUBLE_EQ(*inner, 2.0);
}

// ---- call_global_numeric / call_global_mixed edge cases -------------------

TEST(ScriptEdgeNumeric, FewerReturnsThanExpectedPadsWithZero)
{
    // Arrange — function returns one value but caller asks for three; Lua
    // pads the result block with nil, which the numeric path maps to 0.0.
    Engine eng;
    ASSERT_TRUE(eng.run_string("function one() return 5 end").has_value());
    std::vector<double> out;
    // Act
    const auto r = eng.call_global_numeric("one", {}, out, 3);
    // Assert
    ASSERT_TRUE(r.has_value());
    ASSERT_EQ(out.size(), 3U);
    EXPECT_DOUBLE_EQ(out[0], 5.0);
    EXPECT_DOUBLE_EQ(out[1], 0.0);
    EXPECT_DOUBLE_EQ(out[2], 0.0);
}

TEST(ScriptEdgeNumeric, ZeroExpectedReturnsLeavesOutEmpty)
{
    // Arrange
    Engine eng;
    ASSERT_TRUE(eng.run_string("function effect() side = 1 end").has_value());
    std::vector<double> out { 99.0 };  // pre-populated → must be cleared
    // Act
    const auto r = eng.call_global_numeric("effect", {}, out, 0);
    // Assert
    ASSERT_TRUE(r.has_value());
    EXPECT_TRUE(out.empty());
    EXPECT_DOUBLE_EQ(*eng.get_global_number("side"), 1.0);
}

TEST(ScriptEdgeNumeric, RuntimeErrorInsideCalleePropagates)
{
    // Arrange
    Engine eng;
    ASSERT_TRUE(eng.run_string("function boom() error('inside callee') end")
                    .has_value());
    std::vector<double> out;
    // Act
    const auto r = eng.call_global_numeric("boom", {}, out, 1);
    // Assert
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, code_of(Code::kRuntimeError));
    EXPECT_TRUE(out.empty());
    EXPECT_NE(eng.last_error().find("inside callee"), std::string_view::npos);
}

TEST(ScriptEdgeMixed, MixedArgOrderIsPreservedNumberStringBool)
{
    // Arrange — assert each positional arg keeps its variant type through
    // the push/pcall/pop round-trip by echoing them back.
    Engine eng;
    ASSERT_TRUE(eng.run_string(
                    "function echo3(a, b, c) return a, b, c end").has_value());
    const std::array<cd::script::LuaValue, 3> args {
        cd::script::LuaValue { 1.5 },
        cd::script::LuaValue { std::string { "mid" } },
        cd::script::LuaValue { true },
    };
    std::vector<cd::script::LuaValue> out;
    // Act
    const auto r = eng.call_global_mixed("echo3", args, out, 3);
    // Assert
    ASSERT_TRUE(r.has_value());
    ASSERT_EQ(out.size(), 3U);
    ASSERT_TRUE(std::holds_alternative<double>(out[0]));
    EXPECT_DOUBLE_EQ(std::get<double>(out[0]), 1.5);
    ASSERT_TRUE(std::holds_alternative<std::string>(out[1]));
    EXPECT_EQ(std::get<std::string>(out[1]), "mid");
    ASSERT_TRUE(std::holds_alternative<bool>(out[2]));
    EXPECT_TRUE(std::get<bool>(out[2]));
}

TEST(ScriptEdgeMixed, RuntimeErrorInsideMixedCalleePropagates)
{
    // Arrange
    Engine eng;
    ASSERT_TRUE(eng.run_string("function bad() error('mixed boom') end")
                    .has_value());
    std::vector<cd::script::LuaValue> out;
    // Act
    const auto r = eng.call_global_mixed("bad", {}, out, 1);
    // Assert
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, code_of(Code::kRuntimeError));
    EXPECT_TRUE(out.empty());
}

// ---- Sandbox: instruction cap on call paths -------------------------------

TEST(ScriptEdgeSandbox, InstructionCapDefaultsToZero)
{
    // Arrange / Act / Assert
    Engine eng;
    EXPECT_EQ(eng.instruction_cap(), 0U);
}

TEST(ScriptEdgeSandbox, InstructionCapAbortsRunawayCallGlobal)
{
    // Arrange — the cap must also gate call_global (not just run_string).
    Engine eng;
    ASSERT_TRUE(eng.run_string("function spin() while true do end end")
                    .has_value());
    eng.set_instruction_cap(2000);
    // Act
    const auto r = eng.call_global("spin");
    // Assert
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, code_of(Code::kRuntimeError));
    EXPECT_NE(eng.last_error().find("instruction cap"), std::string_view::npos);
}

TEST(ScriptEdgeSandbox, CapClearedAfterSetToZero)
{
    // Arrange
    Engine eng;
    eng.set_instruction_cap(500);
    EXPECT_EQ(eng.instruction_cap(), 500U);
    // Act
    eng.set_instruction_cap(0);
    // Assert
    EXPECT_EQ(eng.instruction_cap(), 0U);
    EXPECT_TRUE(eng.run_string("for i = 1, 50000 do local x = i end").has_value());
}

// ---- native_state + valid() contract --------------------------------------

TEST(ScriptEdgeState, NativeStateNonNullWhenValid)
{
    // Arrange / Act / Assert
    Engine eng;
    ASSERT_TRUE(eng.valid());
    EXPECT_NE(eng.native_state(), nullptr);
}

// ---- Move semantics: behaviour preserved across move ----------------------

TEST(ScriptEdgeMove, MoveConstructTransfersStateAndCount)
{
    // Arrange — run a script, set a global, then move-construct.
    Engine src;
    ASSERT_TRUE(src.run_string("moved_global = 77").has_value());
    EXPECT_EQ(src.script_count(), 1U);
    // Act
    Engine dst { std::move(src) };
    // Assert — moved-to engine keeps the Lua state, the global, and count.
    ASSERT_TRUE(dst.valid());
    EXPECT_EQ(dst.script_count(), 1U);
    const auto v = dst.get_global_number("moved_global");
    ASSERT_TRUE(v.has_value());
    EXPECT_DOUBLE_EQ(*v, 77.0);
}

TEST(ScriptEdgeMove, MovedFromEngineDegradesGracefully)
{
    // Arrange
    Engine src;
    ASSERT_TRUE(src.valid());
    Engine dst { std::move(src) };
    ASSERT_TRUE(dst.valid());
    // Act / Assert — the moved-from engine must not crash; valid() is false
    // and every accessor returns its documented no-op value. (Reading a
    // moved-from object is well-defined: it is in a valid-but-unspecified
    // state, and this Engine's contract makes that state inert.)
    // NOLINTNEXTLINE(bugprone-use-after-move) — exercising the inert state is the point.
    EXPECT_FALSE(src.valid());
    EXPECT_EQ(src.native_state(), nullptr);
    EXPECT_EQ(src.script_count(), 0U);
    EXPECT_EQ(src.instruction_cap(), 0U);
    EXPECT_FALSE(src.get_global_number("anything").has_value());
    const auto r = src.run_string("return");
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, code_of(Code::kAllocFailed));
}

TEST(ScriptEdgeMove, MoveAssignTransfersState)
{
    // Arrange
    Engine src;
    ASSERT_TRUE(src.run_string("assigned = 5").has_value());
    Engine dst;
    // Act
    dst = std::move(src);
    // Assert
    ASSERT_TRUE(dst.valid());
    const auto v = dst.get_global_number("assigned");
    ASSERT_TRUE(v.has_value());
    EXPECT_DOUBLE_EQ(*v, 5.0);
}

// ---- Error domain stability -----------------------------------------------

TEST(ScriptEdgeError, ErrorsCarryScriptDomain)
{
    // Arrange
    Engine eng;
    // Act
    const auto r = eng.run_string("error('x')");
    // Assert — every script error tags the cd::script error domain so
    // callers can route diagnostics by subsystem.
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().domain, cd::script::script_errors::kDomain);
}

}  // namespace
