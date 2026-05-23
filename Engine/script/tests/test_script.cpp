// =============================================================================
// CHROMODYNAMIC — cd::script tests (Sprint 7 Wave 71-72)
// =============================================================================
#include <cd/script/Engine.hpp>
#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <vector>

namespace
{

TEST(ScriptEngine, ConstructsValidLuaState)
{
    cd::script::Engine eng;
    EXPECT_TRUE(eng.valid());
    EXPECT_EQ(eng.script_count(), 0U);
}

TEST(ScriptEngine, RunSimpleString)
{
    cd::script::Engine eng;
    auto r = eng.run_string("return");  // trivial empty chunk
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(eng.script_count(), 1U);
}

TEST(ScriptEngine, ArithmeticChunkRuns)
{
    cd::script::Engine eng;
    auto r = eng.run_string("local x = 2 + 3 * 4");
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(eng.script_count(), 1U);
}

TEST(ScriptEngine, CompileErrorIsReported)
{
    cd::script::Engine eng;
    // Syntax error: unterminated string.
    auto r = eng.run_string("local x = \"unterminated");
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code,
              static_cast<std::uint32_t>(cd::script::script_errors::Code::kCompileError));
}

TEST(ScriptEngine, RuntimeErrorIsReported)
{
    cd::script::Engine eng;
    // Lua runtime error: explicit `error()` call. We only check the
    // error code; the message string_view inside ErrorCode is not
    // captured for dynamic Lua errors (the engine convention uses
    // static string_views for messages — Lua-side strings would
    // dangle once the catching function returns).
    auto r = eng.run_string("error('boom from inside lua')");
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code,
              static_cast<std::uint32_t>(cd::script::script_errors::Code::kRuntimeError));
}

TEST(ScriptEngine, RunMissingFileReturnsError)
{
    cd::script::Engine eng;
    auto r = eng.run_file("this-script-does-not-exist-zxyq.lua");
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code,
              static_cast<std::uint32_t>(cd::script::script_errors::Code::kFileNotFound));
}

TEST(ScriptEngine, StandardLibraryAvailable)
{
    // luaL_openlibs() should give us `string`, `math`, `table` etc. on
    // the global environment. Verify by calling math.sqrt from script.
    cd::script::Engine eng;
    auto r = eng.run_string("if math.sqrt(16) ~= 4 then error('math broken') end");
    ASSERT_TRUE(r.has_value());
}

// --- Global variable bindings (Wave 73) ------------------------------------

TEST(ScriptGlobals, SetAndGetNumber)
{
    cd::script::Engine eng;
    eng.set_global("answer", 42.0);
    auto r = eng.get_global_number("answer");
    ASSERT_TRUE(r.has_value());
    EXPECT_DOUBLE_EQ(*r, 42.0);
}

TEST(ScriptGlobals, SetAndGetString)
{
    cd::script::Engine eng;
    eng.set_global("greeting", std::string_view { "hello" });
    auto r = eng.get_global_string("greeting");
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(*r, "hello");
}

TEST(ScriptGlobals, SetAndGetBool)
{
    cd::script::Engine eng;
    eng.set_global("flag", true);
    auto r = eng.get_global_bool("flag");
    ASSERT_TRUE(r.has_value());
    EXPECT_TRUE(*r);
    eng.set_global("flag", false);
    auto r2 = eng.get_global_bool("flag");
    ASSERT_TRUE(r2.has_value());
    EXPECT_FALSE(*r2);
}

TEST(ScriptGlobals, ScriptReadsCppGlobal)
{
    cd::script::Engine eng;
    eng.set_global("threshold", 0.5);
    auto r = eng.run_string("if threshold ~= 0.5 then error('mismatch') end");
    EXPECT_TRUE(r.has_value());
}

TEST(ScriptGlobals, CppReadsScriptGlobal)
{
    cd::script::Engine eng;
    auto r = eng.run_string("result = 7 * 6");
    ASSERT_TRUE(r.has_value());
    auto val = eng.get_global_number("result");
    ASSERT_TRUE(val.has_value());
    EXPECT_DOUBLE_EQ(*val, 42.0);
}

TEST(ScriptGlobals, TypeMismatchReturnsNullopt)
{
    cd::script::Engine eng;
    eng.set_global("name", "alice");
    EXPECT_FALSE(eng.get_global_number("name").has_value());
    EXPECT_FALSE(eng.get_global_bool("name").has_value());
}

TEST(ScriptGlobals, UnsetGlobalReturnsNullopt)
{
    cd::script::Engine eng;
    EXPECT_FALSE(eng.get_global_number("does_not_exist_zxyq").has_value());
    EXPECT_FALSE(eng.get_global_string("does_not_exist_zxyq").has_value());
    EXPECT_FALSE(eng.get_global_bool("does_not_exist_zxyq").has_value());
}

TEST(ScriptGlobals, LastErrorPopulatedOnFailureClearedOnSuccess)
{
    cd::script::Engine eng;
    EXPECT_TRUE(eng.last_error().empty());

    auto bad = eng.run_string("error('boom-now')");
    ASSERT_FALSE(bad.has_value());
    EXPECT_NE(eng.last_error().find("boom-now"), std::string_view::npos);

    auto good = eng.run_string("return");
    ASSERT_TRUE(good.has_value());
    EXPECT_TRUE(eng.last_error().empty());
}

// --- Function bindings (Wave 74) -------------------------------------------

TEST(ScriptFunctions, RegisteredFunctionFiresFromLua)
{
    cd::script::Engine eng;
    int fires = 0;
    eng.register_function("native_ping", [&] { ++fires; });
    auto r = eng.run_string("native_ping(); native_ping(); native_ping()");
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(fires, 3);
}

TEST(ScriptFunctions, CallGlobalScriptFunction)
{
    cd::script::Engine eng;
    ASSERT_TRUE(eng.run_string("function bump() counter = (counter or 0) + 1 end").has_value());
    ASSERT_TRUE(eng.call_global("bump").has_value());
    ASSERT_TRUE(eng.call_global("bump").has_value());
    ASSERT_TRUE(eng.call_global("bump").has_value());
    auto v = eng.get_global_number("counter");
    ASSERT_TRUE(v.has_value());
    EXPECT_DOUBLE_EQ(*v, 3.0);
}

TEST(ScriptFunctions, CallGlobalOnNonFunctionFails)
{
    cd::script::Engine eng;
    eng.set_global("not_a_function", 7.0);
    auto r = eng.call_global("not_a_function");
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code,
              static_cast<std::uint32_t>(cd::script::script_errors::Code::kRuntimeError));
    EXPECT_FALSE(eng.last_error().empty());
}

TEST(ScriptFunctions, RegisteredFunctionAndScriptCoexist)
{
    cd::script::Engine eng;
    int fires = 0;
    eng.register_function("on_event", [&] { ++fires; });
    ASSERT_TRUE(eng.run_string(R"(
        for i = 1, 5 do
            if i % 2 == 0 then on_event() end
        end
    )").has_value());
    EXPECT_EQ(fires, 2);  // i=2 and i=4
}

// --- Numeric args + returns (Wave 92) --------------------------------------

TEST(ScriptNumeric, CallReturnsSingleNumber)
{
    cd::script::Engine eng;
    ASSERT_TRUE(eng.run_string("function sq(x) return x * x end").has_value());
    std::vector<double> out;
    std::array<double, 1> args { 7.0 };
    auto r = eng.call_global_numeric("sq", args, out, /*expected_returns=*/1);
    ASSERT_TRUE(r.has_value());
    ASSERT_EQ(out.size(), 1U);
    EXPECT_DOUBLE_EQ(out[0], 49.0);
}

TEST(ScriptNumeric, CallReturnsMultipleNumbers)
{
    cd::script::Engine eng;
    ASSERT_TRUE(eng.run_string(
        "function split(x) return x, x + 1, x * 2 end").has_value());
    std::vector<double> out;
    std::array<double, 1> args { 10.0 };
    auto r = eng.call_global_numeric("split", args, out, 3);
    ASSERT_TRUE(r.has_value());
    ASSERT_EQ(out.size(), 3U);
    EXPECT_DOUBLE_EQ(out[0], 10.0);
    EXPECT_DOUBLE_EQ(out[1], 11.0);
    EXPECT_DOUBLE_EQ(out[2], 20.0);
}

TEST(ScriptNumeric, MultipleArgsArePushedInOrder)
{
    cd::script::Engine eng;
    ASSERT_TRUE(eng.run_string(
        "function add3(a, b, c) return a + b + c end").has_value());
    std::vector<double> out;
    std::array<double, 3> args { 1.0, 2.0, 4.0 };
    auto r = eng.call_global_numeric("add3", args, out, 1);
    ASSERT_TRUE(r.has_value());
    ASSERT_EQ(out.size(), 1U);
    EXPECT_DOUBLE_EQ(out[0], 7.0);
}

TEST(ScriptNumeric, NonCallableGlobalFails)
{
    cd::script::Engine eng;
    eng.set_global("nope", 1.0);
    std::vector<double> out;
    auto r = eng.call_global_numeric("nope", {}, out, 1);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code,
              static_cast<std::uint32_t>(cd::script::script_errors::Code::kRuntimeError));
}

// --- Sandbox: instruction cap (Wave 92) ------------------------------------

TEST(ScriptSandbox, InstructionCapAbortsRunawayLoop)
{
    cd::script::Engine eng;
    eng.set_instruction_cap(1000);
    EXPECT_EQ(eng.instruction_cap(), 1000U);
    auto r = eng.run_string("while true do end");
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code,
              static_cast<std::uint32_t>(cd::script::script_errors::Code::kRuntimeError));
    EXPECT_NE(eng.last_error().find("instruction cap"), std::string_view::npos);
}

TEST(ScriptSandbox, ClearCapAllowsLongLoops)
{
    cd::script::Engine eng;
    eng.set_instruction_cap(1000);
    auto bad = eng.run_string("for i = 1, 100000 do local x = i end");
    EXPECT_FALSE(bad.has_value());

    eng.set_instruction_cap(0);  // clear
    auto good = eng.run_string("for i = 1, 100000 do local x = i end");
    EXPECT_TRUE(good.has_value());
}

// --- Mixed-type call (Wave 99) ---------------------------------------------

TEST(ScriptMixed, MixedArgsRoundTrip)
{
    cd::script::Engine eng;
    ASSERT_TRUE(eng.run_string(
        "function describe(n, name, flag) "
        "  if flag then return name .. ':' .. tostring(n) "
        "  else return name end "
        "end").has_value());
    std::array<cd::script::LuaValue, 3> args {
        cd::script::LuaValue { 7.0 },
        cd::script::LuaValue { std::string { "score" } },
        cd::script::LuaValue { true }
    };
    std::vector<cd::script::LuaValue> out;
    auto r = eng.call_global_mixed("describe", args, out, 1);
    ASSERT_TRUE(r.has_value());
    ASSERT_EQ(out.size(), 1U);
    ASSERT_TRUE(std::holds_alternative<std::string>(out[0]));
    EXPECT_EQ(std::get<std::string>(out[0]), "score:7.0");
}

TEST(ScriptMixed, ReturnsHeterogeneousTuple)
{
    cd::script::Engine eng;
    ASSERT_TRUE(eng.run_string(
        "function triple() return 42, 'hello', true end").has_value());
    std::vector<cd::script::LuaValue> out;
    auto r = eng.call_global_mixed("triple", {}, out, 3);
    ASSERT_TRUE(r.has_value());
    ASSERT_EQ(out.size(), 3U);
    ASSERT_TRUE(std::holds_alternative<double>(out[0]));
    EXPECT_DOUBLE_EQ(std::get<double>(out[0]), 42.0);
    ASSERT_TRUE(std::holds_alternative<std::string>(out[1]));
    EXPECT_EQ(std::get<std::string>(out[1]), "hello");
    ASSERT_TRUE(std::holds_alternative<bool>(out[2]));
    EXPECT_TRUE(std::get<bool>(out[2]));
}

TEST(ScriptMixed, FalseBoolReturnsExplicitly)
{
    cd::script::Engine eng;
    ASSERT_TRUE(eng.run_string("function neg() return false end").has_value());
    std::vector<cd::script::LuaValue> out;
    auto r = eng.call_global_mixed("neg", {}, out, 1);
    ASSERT_TRUE(r.has_value());
    ASSERT_EQ(out.size(), 1U);
    ASSERT_TRUE(std::holds_alternative<bool>(out[0]));
    EXPECT_FALSE(std::get<bool>(out[0]));
}

TEST(ScriptMixed, EmptyArgsZeroReturnsOk)
{
    cd::script::Engine eng;
    ASSERT_TRUE(eng.run_string("function noop() end").has_value());
    std::vector<cd::script::LuaValue> out;
    auto r = eng.call_global_mixed("noop", {}, out, 0);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(out.size(), 0U);
}

TEST(ScriptMixed, NonCallableGlobalFails)
{
    cd::script::Engine eng;
    eng.set_global("notfn", 5.0);
    std::vector<cd::script::LuaValue> out;
    auto r = eng.call_global_mixed("notfn", {}, out, 1);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code,
              static_cast<std::uint32_t>(cd::script::script_errors::Code::kRuntimeError));
}

}  // namespace
