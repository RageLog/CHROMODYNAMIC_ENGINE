// =============================================================================
// CHROMODYNAMIC — engine/script/tests/test_lua_bindings.cpp
// Phase 467 — gtest coverage for the cd::math + cd::ecs + cd::scene Lua bindings.
// =============================================================================
#include <cd/script/Bindings.hpp>
#include <cd/script/Engine.hpp>

#include <cd/ecs/Entity.hpp>
#include <cd/ecs/World.hpp>

#include <cd/scene/Scene.hpp>

#include <gtest/gtest.h>

#include <cstdint>

namespace
{

// ---- Math: Vec3f arithmetic via __add ------------------------------------

TEST(LuaBindings, Vec3fAdditionViaMetamethod)
{
    cd::script::Engine eng;
    ASSERT_TRUE(eng.valid());
    auto* st = cd::script::register_bindings(eng);
    ASSERT_NE(st, nullptr);

    auto r = eng.run_string(R"(
        local a = cd.Vec3f(1.0, 2.0, 3.0)
        local b = cd.Vec3f(4.0, 5.0, 6.0)
        local c = a + b
        if c.x ~= 5.0 or c.y ~= 7.0 or c.z ~= 9.0 then
            error("Vec3f __add mismatch: got " .. tostring(c))
        end
        result_x = c.x; result_y = c.y; result_z = c.z
    )");
    ASSERT_TRUE(r.has_value()) << std::string { eng.last_error() };

    EXPECT_DOUBLE_EQ(*eng.get_global_number("result_x"), 5.0);
    EXPECT_DOUBLE_EQ(*eng.get_global_number("result_y"), 7.0);
    EXPECT_DOUBLE_EQ(*eng.get_global_number("result_z"), 9.0);
}

// ---- ECS: World.create returns Entity table ------------------------------

TEST(LuaBindings, WorldCreateReturnsLiveEntity)
{
    cd::script::Engine eng;
    ASSERT_TRUE(eng.valid());
    auto* st = cd::script::register_bindings(eng);
    ASSERT_NE(st, nullptr);

    cd::ecs::World world;
    ASSERT_TRUE(cd::script::bind_world(st, &world));

    auto r = eng.run_string(R"(
        local e = world:create()
        if type(e) ~= "table" then error("create did not return a table") end
        if e.generation == 0 then error("Entity has zero generation") end
        result_alive = world:is_alive(e)
        result_id = e.id
        result_gen = e.generation
    )");
    ASSERT_TRUE(r.has_value()) << std::string { eng.last_error() };

    auto alive = eng.get_global_bool("result_alive");
    ASSERT_TRUE(alive.has_value());
    EXPECT_TRUE(*alive);
    EXPECT_GE(*eng.get_global_number("result_gen"), 1.0);
    EXPECT_EQ(world.alive_count(), 1U);
}

// ---- LocalTransform read returns table -----------------------------------

TEST(LuaBindings, LocalTransformReadReturnsTable)
{
    cd::script::Engine eng;
    auto* st = cd::script::register_bindings(eng);
    ASSERT_NE(st, nullptr);

    cd::ecs::World world;
    const auto e = world.create();
    cd::scene::LocalTransform lt {};
    lt.value.position = cd::math::Vec3f { 10.0f, 20.0f, 30.0f };
    lt.value.scale    = cd::math::Vec3f { 2.0f, 3.0f, 4.0f };
    lt.value.rotation = cd::math::Quat<float> { 0.0f, 0.0f, 0.0f, 1.0f };
    world.emplace<cd::scene::LocalTransform>(e, lt);
    ASSERT_TRUE(cd::script::bind_world(st, &world));

    eng.set_global("ent_id", static_cast<double>(e.id));
    eng.set_global("ent_gen", static_cast<double>(e.generation));

    auto r = eng.run_string(R"(
        local e = { id = math.floor(ent_id), generation = math.floor(ent_gen) }
        local t = world:get_local_transform(e)
        if t == nil then error("get_local_transform returned nil") end
        if t.position == nil then error("missing .position") end
        if t.rotation == nil then error("missing .rotation") end
        if t.scale    == nil then error("missing .scale") end
        out_px = t.position.x
        out_py = t.position.y
        out_pz = t.position.z
        out_sx = t.scale.x
        out_rw = t.rotation.w
    )");
    ASSERT_TRUE(r.has_value()) << std::string { eng.last_error() };

    EXPECT_FLOAT_EQ(static_cast<float>(*eng.get_global_number("out_px")), 10.0f);
    EXPECT_FLOAT_EQ(static_cast<float>(*eng.get_global_number("out_py")), 20.0f);
    EXPECT_FLOAT_EQ(static_cast<float>(*eng.get_global_number("out_pz")), 30.0f);
    EXPECT_FLOAT_EQ(static_cast<float>(*eng.get_global_number("out_sx")), 2.0f);
    EXPECT_FLOAT_EQ(static_cast<float>(*eng.get_global_number("out_rw")), 1.0f);
}

// ---- Event handler fires on fire_event -----------------------------------

TEST(LuaBindings, EventHandlerFiresOnFrameStart)
{
    cd::script::Engine eng;
    auto* st = cd::script::register_bindings(eng);
    ASSERT_NE(st, nullptr);

    auto r = eng.run_string(R"(
        fire_count = 0
        cd.register_handler("frame_start", function()
            fire_count = fire_count + 1
        end)
    )");
    ASSERT_TRUE(r.has_value()) << std::string { eng.last_error() };

    EXPECT_EQ(cd::script::handler_count(st, "frame_start"), 1U);
    EXPECT_EQ(cd::script::fire_event(st, "frame_start"), 1U);
    EXPECT_EQ(cd::script::fire_event(st, "frame_start"), 1U);

    auto cnt = eng.get_global_number("fire_count");
    ASSERT_TRUE(cnt.has_value());
    EXPECT_DOUBLE_EQ(*cnt, 2.0);
}

// ---- Type error returns nil + error string (no crash) --------------------

TEST(LuaBindings, TypeErrorReturnsNilAndMessage)
{
    cd::script::Engine eng;
    auto* st = cd::script::register_bindings(eng);
    ASSERT_NE(st, nullptr);

    // Passing a non-function to register_handler should produce
    // (nil, "<message>") rather than crash. Inspect both return values.
    auto r = eng.run_string(R"(
        local ok, err = cd.register_handler("frame_start", 42)
        if ok ~= nil then error("expected nil first return") end
        if type(err) ~= "string" then error("expected string error message") end
        if #err == 0 then error("expected non-empty error message") end
        observed_err = err
    )");
    ASSERT_TRUE(r.has_value()) << std::string { eng.last_error() };
    auto msg = eng.get_global_string("observed_err");
    ASSERT_TRUE(msg.has_value());
    EXPECT_FALSE(msg->empty());
}

// ---- GC doesn't crash with held Entity table -----------------------------

TEST(LuaBindings, GcDoesNotCrashWithHeldEntity)
{
    cd::script::Engine eng;
    auto* st = cd::script::register_bindings(eng);
    ASSERT_NE(st, nullptr);

    cd::ecs::World world;
    ASSERT_TRUE(cd::script::bind_world(st, &world));

    // Create entities + Vec3f userdata, drop them, force a full GC.
    // Repeating the alloc-drop-collectgarbage cycle stress-tests the
    // metatable and userdata lifecycle.
    auto r = eng.run_string(R"(
        for i = 1, 200 do
            local e = world:create()
            local v = cd.Vec3f(i, i + 1, i + 2)
            v = v + cd.Vec3f(1, 1, 1)
            world:destroy(e)
            -- discard refs; no upvalues keep them alive
        end
        collectgarbage("collect")
        collectgarbage("collect")
        gc_ok = true
    )");
    ASSERT_TRUE(r.has_value()) << std::string { eng.last_error() };
    auto ok = eng.get_global_bool("gc_ok");
    ASSERT_TRUE(ok.has_value());
    EXPECT_TRUE(*ok);
    // World should report zero alive after all destroys.
    EXPECT_EQ(world.alive_count(), 0U);
}

// ---- Multiple handlers fire in registration order ------------------------

TEST(LuaBindings, MultipleHandlersFireInOrder)
{
    cd::script::Engine eng;
    auto* st = cd::script::register_bindings(eng);
    ASSERT_NE(st, nullptr);

    auto r = eng.run_string(R"(
        order = {}
        cd.register_handler("frame_start", function() table.insert(order, "A") end)
        cd.register_handler("frame_start", function() table.insert(order, "B") end)
        cd.register_handler("frame_start", function() table.insert(order, "C") end)
    )");
    ASSERT_TRUE(r.has_value()) << std::string { eng.last_error() };

    EXPECT_EQ(cd::script::handler_count(st, "frame_start"), 3U);
    EXPECT_EQ(cd::script::fire_event(st, "frame_start"), 3U);

    // Inspect order via a script-side join, then read back as a global.
    auto r2 = eng.run_string("order_str = table.concat(order, ',')");
    ASSERT_TRUE(r2.has_value()) << std::string { eng.last_error() };
    auto s = eng.get_global_string("order_str");
    ASSERT_TRUE(s.has_value());
    EXPECT_EQ(*s, "A,B,C");
}

// ---- Unregister stops firing for the matching id only --------------------

TEST(LuaBindings, UnregisterStopsFiring)
{
    cd::script::Engine eng;
    auto* st = cd::script::register_bindings(eng);
    ASSERT_NE(st, nullptr);

    auto r = eng.run_string(R"(
        fires_a = 0; fires_b = 0
        id_a = cd.register_handler("frame_start", function() fires_a = fires_a + 1 end)
        id_b = cd.register_handler("frame_start", function() fires_b = fires_b + 1 end)
    )");
    ASSERT_TRUE(r.has_value()) << std::string { eng.last_error() };

    EXPECT_EQ(cd::script::handler_count(st, "frame_start"), 2U);
    EXPECT_EQ(cd::script::fire_event(st, "frame_start"), 2U);

    auto r2 = eng.run_string("removed = cd.unregister_handler('frame_start', id_a)");
    ASSERT_TRUE(r2.has_value());
    auto removed = eng.get_global_bool("removed");
    ASSERT_TRUE(removed.has_value());
    EXPECT_TRUE(*removed);

    EXPECT_EQ(cd::script::handler_count(st, "frame_start"), 1U);
    EXPECT_EQ(cd::script::fire_event(st, "frame_start"), 1U);

    auto fa = eng.get_global_number("fires_a");
    auto fb = eng.get_global_number("fires_b");
    ASSERT_TRUE(fa.has_value() && fb.has_value());
    EXPECT_DOUBLE_EQ(*fa, 1.0);  // fired once before unregister
    EXPECT_DOUBLE_EQ(*fb, 2.0);  // fired both before and after
}

// ---- Additional case: Vec3f * scalar via __mul ---------------------------

TEST(LuaBindings, Vec3fScalarMultiplication)
{
    cd::script::Engine eng;
    auto* st = cd::script::register_bindings(eng);
    ASSERT_NE(st, nullptr);

    auto r = eng.run_string(R"(
        local v = cd.Vec3f(1.0, 2.0, 3.0) * 2.5
        out_x = v.x; out_y = v.y; out_z = v.z
    )");
    ASSERT_TRUE(r.has_value()) << std::string { eng.last_error() };
    EXPECT_FLOAT_EQ(static_cast<float>(*eng.get_global_number("out_x")), 2.5f);
    EXPECT_FLOAT_EQ(static_cast<float>(*eng.get_global_number("out_y")), 5.0f);
    EXPECT_FLOAT_EQ(static_cast<float>(*eng.get_global_number("out_z")), 7.5f);
}

// ---- Additional case: Mat4f.identity * Vec4f keeps the vector ------------

TEST(LuaBindings, Mat4fIdentityMultipliesVec4fUnchanged)
{
    cd::script::Engine eng;
    auto* st = cd::script::register_bindings(eng);
    ASSERT_NE(st, nullptr);

    auto r = eng.run_string(R"(
        local m = cd.Mat4f.identity()
        local v = cd.Vec4f(1.0, 2.0, 3.0, 1.0)
        local rv = m * v
        out_x = rv.x; out_y = rv.y; out_z = rv.z; out_w = rv.w
    )");
    ASSERT_TRUE(r.has_value()) << std::string { eng.last_error() };
    EXPECT_FLOAT_EQ(static_cast<float>(*eng.get_global_number("out_x")), 1.0f);
    EXPECT_FLOAT_EQ(static_cast<float>(*eng.get_global_number("out_y")), 2.0f);
    EXPECT_FLOAT_EQ(static_cast<float>(*eng.get_global_number("out_z")), 3.0f);
    EXPECT_FLOAT_EQ(static_cast<float>(*eng.get_global_number("out_w")), 1.0f);
}

// ---- Additional case: set_local_transform writes through to ECS ---------

TEST(LuaBindings, SetLocalTransformWritesThroughEcs)
{
    cd::script::Engine eng;
    auto* st = cd::script::register_bindings(eng);
    ASSERT_NE(st, nullptr);

    cd::ecs::World world;
    const auto e = world.create();
    world.emplace<cd::scene::LocalTransform>(e);
    ASSERT_TRUE(cd::script::bind_world(st, &world));

    eng.set_global("ent_id", static_cast<double>(e.id));
    eng.set_global("ent_gen", static_cast<double>(e.generation));

    auto r = eng.run_string(R"(
        local e = { id = math.floor(ent_id), generation = math.floor(ent_gen) }
        local t = {
            position = cd.Vec3f(7.0, 8.0, 9.0),
            rotation = { x = 0.0, y = 0.0, z = 0.0, w = 1.0 },
            scale    = cd.Vec3f(2.0, 2.0, 2.0),
        }
        world:set_local_transform(e, t)
    )");
    ASSERT_TRUE(r.has_value()) << std::string { eng.last_error() };

    const auto* lt = world.get<cd::scene::LocalTransform>(e);
    ASSERT_NE(lt, nullptr);
    EXPECT_FLOAT_EQ(lt->value.position.x, 7.0f);
    EXPECT_FLOAT_EQ(lt->value.position.y, 8.0f);
    EXPECT_FLOAT_EQ(lt->value.position.z, 9.0f);
    EXPECT_FLOAT_EQ(lt->value.scale.x,    2.0f);
}

}  // namespace
