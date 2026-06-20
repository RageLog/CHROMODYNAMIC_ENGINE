// =============================================================================
// CHROMODYNAMIC — engine/script/tests/test_lua_bindings_edge.cpp
// 80→100 depth marathon — edge + negative coverage for the cd::math/ecs/scene
// Lua binding surface (Bindings.cpp). Pins the (nil, "message") error-return
// contract, the dead/invalid-entity branches, Vec4f/Mat4f metamethods that the
// happy-path suite skipped, handler-table edge cases (unregister miss, erroring
// handler counted as failed, idempotent register_bindings), and the C++-side
// null-guards on bind_world / fire_event / handler_count.
//
// Behaviour-preserving: only previously-unverified branches are exercised; no
// production source changes. AAA throughout; error strings surfaced via the
// documented two-return (nil, message) convention, never via Lua raise.
// =============================================================================
#include <cd/script/Bindings.hpp>
#include <cd/script/Engine.hpp>

#include <cd/ecs/Entity.hpp>
#include <cd/ecs/World.hpp>

#include <cd/scene/Scene.hpp>

#include <gtest/gtest.h>

#include <cstdint>
#include <string>

namespace
{

using cd::script::Engine;

// ---- World: destroy → is_alive false --------------------------------------

TEST(LuaBindingsEdge, DestroyMakesEntityNotAlive)
{
    // Arrange
    Engine eng;
    auto* st = cd::script::register_bindings(eng);
    ASSERT_NE(st, nullptr);
    cd::ecs::World world;
    ASSERT_TRUE(cd::script::bind_world(st, &world));
    // Act
    const auto r = eng.run_string(R"(
        local e = world:create()
        alive_before = world:is_alive(e)
        world:destroy(e)
        alive_after = world:is_alive(e)
    )");
    // Assert
    ASSERT_TRUE(r.has_value()) << std::string { eng.last_error() };
    EXPECT_TRUE(*eng.get_global_bool("alive_before"));
    EXPECT_FALSE(*eng.get_global_bool("alive_after"));
    EXPECT_EQ(world.alive_count(), 0U);
}

// ---- World: invalid (never-created) entity is not alive --------------------

TEST(LuaBindingsEdge, FabricatedEntityIsNotAlive)
{
    // Arrange — a script-fabricated entity table that was never created by
    // the world must report not-alive (generation/id mismatch).
    Engine eng;
    auto* st = cd::script::register_bindings(eng);
    ASSERT_NE(st, nullptr);
    cd::ecs::World world;
    ASSERT_TRUE(cd::script::bind_world(st, &world));
    // Act
    const auto r = eng.run_string(R"(
        local fake = { id = 9999, generation = 7 }
        bogus_alive = world:is_alive(fake)
    )");
    // Assert
    ASSERT_TRUE(r.has_value()) << std::string { eng.last_error() };
    EXPECT_FALSE(*eng.get_global_bool("bogus_alive"));
}

TEST(LuaBindingsEdge, NonTableEntityArgIsNotAlive)
{
    // Arrange — entity_from_table on a non-table yields a default (invalid,
    // generation-0) entity; is_alive must be false, no crash.
    Engine eng;
    auto* st = cd::script::register_bindings(eng);
    ASSERT_NE(st, nullptr);
    cd::ecs::World world;
    ASSERT_TRUE(cd::script::bind_world(st, &world));
    // Act — pass a number where a table is expected.
    const auto r = eng.run_string("nontable_alive = world:is_alive(42)");
    // Assert
    ASSERT_TRUE(r.has_value()) << std::string { eng.last_error() };
    EXPECT_FALSE(*eng.get_global_bool("nontable_alive"));
}

// ---- get_local_transform: missing component → (nil, message) --------------

TEST(LuaBindingsEdge, GetTransformOnEntityWithoutComponentReturnsNilMessage)
{
    // Arrange — a live entity that has no LocalTransform component.
    Engine eng;
    auto* st = cd::script::register_bindings(eng);
    ASSERT_NE(st, nullptr);
    cd::ecs::World world;
    const auto e = world.create();
    ASSERT_TRUE(cd::script::bind_world(st, &world));
    eng.set_global("ent_id", static_cast<double>(e.id));
    eng.set_global("ent_gen", static_cast<double>(e.generation));
    // Act
    const auto r = eng.run_string(R"(
        local e = { id = math.floor(ent_id), generation = math.floor(ent_gen) }
        local t, err = world:get_local_transform(e)
        got_nil = (t == nil)
        err_kind = type(err)
        err_msg = err
    )");
    // Assert
    ASSERT_TRUE(r.has_value()) << std::string { eng.last_error() };
    EXPECT_TRUE(*eng.get_global_bool("got_nil"));
    EXPECT_EQ(*eng.get_global_string("err_kind"), "string");
    EXPECT_NE(eng.get_global_string("err_msg")->find("LocalTransform"),
              std::string::npos);
}

// ---- get_local_transform: dead entity → (nil, "Entity is not alive") ------

TEST(LuaBindingsEdge, GetTransformOnDeadEntityReturnsNilMessage)
{
    // Arrange
    Engine eng;
    auto* st = cd::script::register_bindings(eng);
    ASSERT_NE(st, nullptr);
    cd::ecs::World world;
    const auto e = world.create();
    world.emplace<cd::scene::LocalTransform>(e);
    world.destroy(e);  // now dead
    ASSERT_TRUE(cd::script::bind_world(st, &world));
    eng.set_global("ent_id", static_cast<double>(e.id));
    eng.set_global("ent_gen", static_cast<double>(e.generation));
    // Act
    const auto r = eng.run_string(R"(
        local e = { id = math.floor(ent_id), generation = math.floor(ent_gen) }
        local t, err = world:get_local_transform(e)
        dead_nil = (t == nil)
        dead_msg = err
    )");
    // Assert
    ASSERT_TRUE(r.has_value()) << std::string { eng.last_error() };
    EXPECT_TRUE(*eng.get_global_bool("dead_nil"));
    EXPECT_NE(eng.get_global_string("dead_msg")->find("not alive"),
              std::string::npos);
}

// ---- set_local_transform: non-table payload → (nil, message) --------------

TEST(LuaBindingsEdge, SetTransformWithNonTableReturnsNilMessage)
{
    // Arrange
    Engine eng;
    auto* st = cd::script::register_bindings(eng);
    ASSERT_NE(st, nullptr);
    cd::ecs::World world;
    const auto e = world.create();
    ASSERT_TRUE(cd::script::bind_world(st, &world));
    eng.set_global("ent_id", static_cast<double>(e.id));
    eng.set_global("ent_gen", static_cast<double>(e.generation));
    // Act — third arg is a number, not a transform table.
    const auto r = eng.run_string(R"(
        local e = { id = math.floor(ent_id), generation = math.floor(ent_gen) }
        local ok, err = world:set_local_transform(e, 123)
        set_nil = (ok == nil)
        set_msg = err
    )");
    // Assert
    ASSERT_TRUE(r.has_value()) << std::string { eng.last_error() };
    EXPECT_TRUE(*eng.get_global_bool("set_nil"));
    EXPECT_NE(eng.get_global_string("set_msg")->find("LocalTransform table"),
              std::string::npos);
}

// ---- Vec4f arithmetic + accessors (happy-path suite skipped these) --------

TEST(LuaBindingsEdge, Vec4fSubtractAndScalarMul)
{
    // Arrange
    Engine eng;
    auto* st = cd::script::register_bindings(eng);
    ASSERT_NE(st, nullptr);
    // Act
    const auto r = eng.run_string(R"(
        local a = cd.Vec4f(5, 6, 7, 8)
        local b = cd.Vec4f(1, 2, 3, 4)
        local d = a - b
        local s = b * 2.0
        sub_x = d.x; sub_w = d.w
        sc_x = s.x; sc_w = s.w
    )");
    // Assert
    ASSERT_TRUE(r.has_value()) << std::string { eng.last_error() };
    EXPECT_FLOAT_EQ(static_cast<float>(*eng.get_global_number("sub_x")), 4.0f);
    EXPECT_FLOAT_EQ(static_cast<float>(*eng.get_global_number("sub_w")), 4.0f);
    EXPECT_FLOAT_EQ(static_cast<float>(*eng.get_global_number("sc_x")), 2.0f);
    EXPECT_FLOAT_EQ(static_cast<float>(*eng.get_global_number("sc_w")), 8.0f);
}

TEST(LuaBindingsEdge, Vec4fNewIndexWritesField)
{
    // Arrange
    Engine eng;
    auto* st = cd::script::register_bindings(eng);
    ASSERT_NE(st, nullptr);
    // Act — write each component through __newindex then read it back.
    const auto r = eng.run_string(R"(
        local v = cd.Vec4f(0, 0, 0, 0)
        v.x = 1.0; v.y = 2.0; v.z = 3.0; v.w = 4.0
        rx = v.x; ry = v.y; rz = v.z; rw = v.w
    )");
    // Assert
    ASSERT_TRUE(r.has_value()) << std::string { eng.last_error() };
    EXPECT_FLOAT_EQ(static_cast<float>(*eng.get_global_number("rx")), 1.0f);
    EXPECT_FLOAT_EQ(static_cast<float>(*eng.get_global_number("rw")), 4.0f);
}

TEST(LuaBindingsEdge, Vec3fNewIndexUnknownFieldRaisesCaughtByPcall)
{
    // Arrange — writing an unknown field raises a Lua error; wrapping in
    // pcall confirms it surfaces as a catchable error, not a crash.
    Engine eng;
    auto* st = cd::script::register_bindings(eng);
    ASSERT_NE(st, nullptr);
    // Act
    const auto r = eng.run_string(R"(
        local v = cd.Vec3f(1, 2, 3)
        local ok, err = pcall(function() v.bogus = 9 end)
        unknown_ok = ok
        unknown_err = err
    )");
    // Assert
    ASSERT_TRUE(r.has_value()) << std::string { eng.last_error() };
    EXPECT_FALSE(*eng.get_global_bool("unknown_ok"));
    EXPECT_NE(eng.get_global_string("unknown_err")->find("unknown field"),
              std::string::npos);
}

TEST(LuaBindingsEdge, Vec3fIndexUnknownFieldReturnsNil)
{
    // Arrange — reading an unknown key returns nil (not an error).
    Engine eng;
    auto* st = cd::script::register_bindings(eng);
    ASSERT_NE(st, nullptr);
    // Act
    const auto r = eng.run_string(R"(
        local v = cd.Vec3f(1, 2, 3)
        missing_is_nil = (v.nope == nil)
    )");
    // Assert
    ASSERT_TRUE(r.has_value()) << std::string { eng.last_error() };
    EXPECT_TRUE(*eng.get_global_bool("missing_is_nil"));
}

TEST(LuaBindingsEdge, Vec3fTostringFormats)
{
    // Arrange
    Engine eng;
    auto* st = cd::script::register_bindings(eng);
    ASSERT_NE(st, nullptr);
    // Act
    const auto r = eng.run_string(R"(
        v3_str = tostring(cd.Vec3f(1, 2, 3))
    )");
    // Assert
    ASSERT_TRUE(r.has_value()) << std::string { eng.last_error() };
    EXPECT_NE(eng.get_global_string("v3_str")->find("Vec3f"), std::string::npos);
}

TEST(LuaBindingsEdge, Vec3fScalarMulCommutes)
{
    // Arrange — number * Vec3f hits the swapped-operand branch in __mul.
    Engine eng;
    auto* st = cd::script::register_bindings(eng);
    ASSERT_NE(st, nullptr);
    // Act
    const auto r = eng.run_string(R"(
        local v = 3.0 * cd.Vec3f(1, 2, 3)
        cm_x = v.x; cm_z = v.z
    )");
    // Assert
    ASSERT_TRUE(r.has_value()) << std::string { eng.last_error() };
    EXPECT_FLOAT_EQ(static_cast<float>(*eng.get_global_number("cm_x")), 3.0f);
    EXPECT_FLOAT_EQ(static_cast<float>(*eng.get_global_number("cm_z")), 9.0f);
}

TEST(LuaBindingsEdge, Vec3fMulInvalidOperandRaisesCaughtByPcall)
{
    // Arrange — Vec3f * string is neither vec*vec nor vec*number; the
    // __mul handler raises, caught here by pcall.
    Engine eng;
    auto* st = cd::script::register_bindings(eng);
    ASSERT_NE(st, nullptr);
    // Act
    const auto r = eng.run_string(R"(
        local v = cd.Vec3f(1, 2, 3)
        local ok, err = pcall(function() return v * "nope" end)
        mul_ok = ok
        mul_err = err
    )");
    // Assert
    ASSERT_TRUE(r.has_value()) << std::string { eng.last_error() };
    EXPECT_FALSE(*eng.get_global_bool("mul_ok"));
    EXPECT_NE(eng.get_global_string("mul_err")->find("Vec3f __mul"),
              std::string::npos);
}

// ---- Mat4f: Mat * Mat + invalid operand -----------------------------------

TEST(LuaBindingsEdge, Mat4fIdentityTimesIdentityIsIdentity)
{
    // Arrange
    Engine eng;
    auto* st = cd::script::register_bindings(eng);
    ASSERT_NE(st, nullptr);
    // Act — Mat * Mat path, then apply to a vector to confirm identity.
    const auto r = eng.run_string(R"(
        local m = cd.Mat4f.identity() * cd.Mat4f.identity()
        local v = m * cd.Vec4f(2, 3, 4, 1)
        mm_x = v.x; mm_y = v.y; mm_z = v.z; mm_w = v.w
    )");
    // Assert
    ASSERT_TRUE(r.has_value()) << std::string { eng.last_error() };
    EXPECT_FLOAT_EQ(static_cast<float>(*eng.get_global_number("mm_x")), 2.0f);
    EXPECT_FLOAT_EQ(static_cast<float>(*eng.get_global_number("mm_w")), 1.0f);
}

TEST(LuaBindingsEdge, Mat4fMulInvalidOperandRaisesCaughtByPcall)
{
    // Arrange
    Engine eng;
    auto* st = cd::script::register_bindings(eng);
    ASSERT_NE(st, nullptr);
    // Act — Mat4f * number is unsupported; the handler raises.
    const auto r = eng.run_string(R"(
        local m = cd.Mat4f.identity()
        local ok, err = pcall(function() return m * 5 end)
        m_ok = ok
        m_err = err
    )");
    // Assert
    ASSERT_TRUE(r.has_value()) << std::string { eng.last_error() };
    EXPECT_FALSE(*eng.get_global_bool("m_ok"));
    EXPECT_NE(eng.get_global_string("m_err")->find("Mat4f __mul"),
              std::string::npos);
}

// ---- Event handlers: unregister miss + erroring handler -------------------

TEST(LuaBindingsEdge, UnregisterUnknownEventReturnsFalse)
{
    // Arrange
    Engine eng;
    auto* st = cd::script::register_bindings(eng);
    ASSERT_NE(st, nullptr);
    // Act — no handler was ever registered for this event name.
    const auto r = eng.run_string(
        "removed = cd.unregister_handler('never_registered', 1)");
    // Assert
    ASSERT_TRUE(r.has_value()) << std::string { eng.last_error() };
    EXPECT_FALSE(*eng.get_global_bool("removed"));
}

TEST(LuaBindingsEdge, UnregisterUnknownIdReturnsFalse)
{
    // Arrange — register one handler, then try to unregister a bogus id.
    Engine eng;
    auto* st = cd::script::register_bindings(eng);
    ASSERT_NE(st, nullptr);
    const auto r = eng.run_string(R"(
        cd.register_handler("e", function() end)
        removed = cd.unregister_handler("e", 999999)
    )");
    // Assert
    ASSERT_TRUE(r.has_value()) << std::string { eng.last_error() };
    EXPECT_FALSE(*eng.get_global_bool("removed"));
    EXPECT_EQ(cd::script::handler_count(st, "e"), 1U);  // untouched
}

TEST(LuaBindingsEdge, ErroringHandlerCountedAsFailed)
{
    // Arrange — two handlers; the first raises. fire_event must run both,
    // count only the successful one, and not crash.
    Engine eng;
    auto* st = cd::script::register_bindings(eng);
    ASSERT_NE(st, nullptr);
    const auto reg = eng.run_string(R"(
        ran_b = 0
        cd.register_handler("tick", function() error("handler boom") end)
        cd.register_handler("tick", function() ran_b = ran_b + 1 end)
    )");
    ASSERT_TRUE(reg.has_value()) << std::string { eng.last_error() };
    // Act — 2 handlers, 1 succeeds.
    const auto fired = cd::script::fire_event(st, "tick");
    // Assert
    EXPECT_EQ(fired, 1U);
    EXPECT_DOUBLE_EQ(*eng.get_global_number("ran_b"), 1.0);
}

TEST(LuaBindingsEdge, FireUnknownEventFiresZero)
{
    // Arrange
    Engine eng;
    auto* st = cd::script::register_bindings(eng);
    ASSERT_NE(st, nullptr);
    // Act / Assert
    EXPECT_EQ(cd::script::fire_event(st, "no_such_event"), 0U);
    EXPECT_EQ(cd::script::handler_count(st, "no_such_event"), 0U);
}

// ---- register_bindings idempotency ----------------------------------------

TEST(LuaBindingsEdge, RegisterBindingsTwiceReturnsSameState)
{
    // Arrange
    Engine eng;
    // Act
    auto* first = cd::script::register_bindings(eng);
    auto* second = cd::script::register_bindings(eng);
    // Assert — second call is a no-op that returns the same state.
    ASSERT_NE(first, nullptr);
    EXPECT_EQ(first, second);
}

// ---- C++ side null-guards --------------------------------------------------

TEST(LuaBindingsEdge, BindWorldNullWorldReturnsFalse)
{
    // Arrange
    Engine eng;
    auto* st = cd::script::register_bindings(eng);
    ASSERT_NE(st, nullptr);
    // Act / Assert
    EXPECT_FALSE(cd::script::bind_world(st, nullptr));
}

TEST(LuaBindingsEdge, NullStateGuardsAreInert)
{
    // Arrange — every public entry must tolerate a null BindingState.
    cd::ecs::World world;
    // Act / Assert
    EXPECT_FALSE(cd::script::bind_world(nullptr, &world));
    EXPECT_EQ(cd::script::fire_event(nullptr, "x"), 0U);
    EXPECT_EQ(cd::script::handler_count(nullptr, "x"), 0U);
}

TEST(LuaBindingsEdge, RegisterBindingsOnInvalidEngineReturnsNull)
{
    // Arrange — a moved-from engine is invalid; register_bindings must
    // refuse rather than dereference a null state.
    Engine src;
    Engine dst { std::move(src) };
    ASSERT_TRUE(dst.valid());
    // Act / Assert
    // NOLINTNEXTLINE(bugprone-use-after-move) — verifying the invalid-engine guard.
    EXPECT_EQ(cd::script::register_bindings(src), nullptr);
}

// ---- cd.bind_world from Lua with a non-lightuserdata arg → (nil, message) -

TEST(LuaBindingsEdge, LuaBindWorldNonUserdataReturnsNilMessage)
{
    // Arrange — the Lua-side cd.bind_world expects a light userdata; any
    // other argument type must return (nil, "<message>") rather than raise.
    Engine eng;
    auto* st = cd::script::register_bindings(eng);
    ASSERT_NE(st, nullptr);
    // Act
    const auto r = eng.run_string(R"(
        local h, err = cd.bind_world("not light userdata")
        bw_nil = (h == nil)
        bw_msg = err
    )");
    // Assert
    ASSERT_TRUE(r.has_value()) << std::string { eng.last_error() };
    EXPECT_TRUE(*eng.get_global_bool("bw_nil"));
    EXPECT_NE(eng.get_global_string("bw_msg")->find("light userdata"),
              std::string::npos);
}

}  // namespace
