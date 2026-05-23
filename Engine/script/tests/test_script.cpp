// =============================================================================
// CHROMODYNAMIC — cd::script tests (Sprint 7 Wave 71-72)
// =============================================================================
#include <cd/script/Engine.hpp>
#include <gtest/gtest.h>

#include <cstdint>

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

}  // namespace
