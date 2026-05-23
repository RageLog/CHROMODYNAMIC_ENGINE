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

}  // namespace
