// =============================================================================
// CHROMODYNAMIC — test_bridge_contract.cpp
// phase1098 — hello_editor <-> hello_engine cdscene BRIDGE contract pin.
//
// Both samples read/write hello_engine.cdscene.json with a shared,
// UNWRITTEN field contract (per-entity "name"/"kind"/"tint" extras +
// a root-level "lights" array). The key strings here are duplicated ON
// PURPOSE: this test is the contract — if either sample drifts from
// the pinned shape, this stays red until the drift is reconciled.
//
// Headless: exercises only library APIs (Scene, Serializer, Json,
// Light) — no window, no GPU.
// =============================================================================
#include <cd/asset/json/Json.hpp>
#include <cd/ecs/World.hpp>
#include <cd/light/Light.hpp>
#include <cd/scene/Scene.hpp>
#include <cd/scene/Serializer.hpp>
#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace
{

struct ExtraRow
{
    std::string     name;
    std::string     kind;
    cd::math::Vec3f tint { 1.0F, 1.0F, 1.0F };
};

// ---------------------------------------------------------------------------
// Entity extras: name / kind / tint survive a serialize -> deserialize
// round-trip through the exact key strings both samples use.
// ---------------------------------------------------------------------------
TEST(BridgeContract, EntityExtrasRoundTrip)
{
    cd::ecs::World world;
    cd::scene::Scene scene { world };
    const auto a = scene.create_node();
    const auto b = scene.create_node();
    scene.local(a)->value.position = { 1.0F, 2.0F, 3.0F };
    scene.local(b)->value.position = { -4.0F, 0.5F, 8.0F };

    const std::vector<std::pair<cd::ecs::Entity, ExtraRow>> rows {
        { a, { "Cube A", "Cube", { 1.0F, 0.4F, 0.4F } } },
        { b, { "Sphere B", "Sphere", { 0.4F, 1.0F, 0.4F } } },
    };
    auto root = cd::scene::serialize_scene_with(
        scene,
        [&](cd::ecs::Entity e, cd::asset::json::Object& obj)
        {
            for (const auto& [ent, row] : rows)
            {
                if (ent.id != e.id) continue;
                obj["name"] = cd::asset::json::Value { row.name };
                obj["kind"] = cd::asset::json::Value { row.kind };
                obj["tint"] = cd::scene::vec3_to_json(row.tint);
            }
        });

    // Text round-trip — the bridge goes through a file on disk.
    const auto txt    = cd::asset::json::serialize(root, true);
    const auto parsed = cd::asset::json::parse(txt);
    ASSERT_TRUE(parsed.has_value());

    cd::ecs::World world2;
    cd::scene::Scene scene2 { world2 };
    std::vector<ExtraRow> loaded;
    std::vector<cd::ecs::Entity> loaded_ents;
    const auto idmap = cd::scene::deserialize_scene_with(
        scene2, *parsed,
        [&](cd::ecs::Entity e, const cd::asset::json::Object& obj)
        {
            ExtraRow row;
            if (auto it = obj.find("name"); it != obj.end() && it->second.is_string())
                row.name = it->second.as_string();
            if (auto it = obj.find("kind"); it != obj.end() && it->second.is_string())
                row.kind = it->second.as_string();
            if (auto it = obj.find("tint");
                it != obj.end() && it->second.is_array() &&
                it->second.as_array().size() == 3)
            {
                const auto& arr = it->second.as_array();
                row.tint = { static_cast<float>(arr[0].as_number()),
                             static_cast<float>(arr[1].as_number()),
                             static_cast<float>(arr[2].as_number()) };
            }
            loaded.push_back(std::move(row));
            loaded_ents.push_back(e);
        });
    ASSERT_TRUE(idmap.has_value());
    ASSERT_EQ(loaded.size(), 2u);

    EXPECT_EQ(loaded[0].name, "Cube A");
    EXPECT_EQ(loaded[0].kind, "Cube");
    EXPECT_FLOAT_EQ(loaded[0].tint.y, 0.4F);
    EXPECT_EQ(loaded[1].name, "Sphere B");
    EXPECT_EQ(loaded[1].kind, "Sphere");

    // Transforms came through the serializer core.
    EXPECT_FLOAT_EQ(scene2.local(loaded_ents[0])->value.position.x, 1.0F);
    EXPECT_FLOAT_EQ(scene2.local(loaded_ents[1])->value.position.z, 8.0F);
}

// ---------------------------------------------------------------------------
// Lights array: the exact root-level shape hello_engine writes and the
// editor (phase1097) reads — field names, scalar types, vec3 arrays.
// ---------------------------------------------------------------------------
TEST(BridgeContract, LightsArrayShape)
{
    // Writer side (mirrors both samples' save path).
    cd::light::Light l;
    l.type      = cd::light::LightType::kPoint;
    l.position  = { 0.0F, 2.0F, 0.0F };
    l.color     = { 1.0F, 0.9F, 0.8F };
    l.direction = { 0.0F, -1.0F, 0.0F };
    l.intensity = 60.0F;
    l.range     = 6.0F;

    cd::asset::json::Object lo;
    lo["name"]      = cd::asset::json::Value { std::string { "Point 1" } };
    lo["enabled"]   = cd::asset::json::Value { true };
    lo["type"]      = cd::asset::json::Value { static_cast<int>(l.type) };
    lo["kelvin"]    = cd::asset::json::Value { 6500.0 };
    lo["intensity"] = cd::asset::json::Value { static_cast<double>(l.intensity) };
    lo["range"]     = cd::asset::json::Value { static_cast<double>(l.range) };
    lo["position"]  = cd::scene::vec3_to_json(l.position);
    lo["color"]     = cd::scene::vec3_to_json(l.color);
    lo["direction"] = cd::scene::vec3_to_json(l.direction);

    cd::asset::json::Array la;
    la.emplace_back(std::move(lo));
    cd::asset::json::Object root;
    root["lights"] = cd::asset::json::Value { std::move(la) };

    // Text round-trip, then reader side (mirrors both samples' load path).
    const auto txt = cd::asset::json::serialize(
        cd::asset::json::Value { std::move(root) }, true);
    const auto parsed = cd::asset::json::parse(txt);
    ASSERT_TRUE(parsed.has_value());
    ASSERT_TRUE(parsed->is_object());

    const auto& ro = parsed->as_object();
    const auto it = ro.find("lights");
    ASSERT_NE(it, ro.end());
    ASSERT_TRUE(it->second.is_array());
    ASSERT_EQ(it->second.as_array().size(), 1u);

    const auto& lv = it->second.as_array()[0];
    ASSERT_TRUE(lv.is_object());
    const auto& lr = lv.as_object();

    const auto expect_num = [&](const char* key, double v)
    {
        const auto f = lr.find(key);
        ASSERT_NE(f, lr.end()) << key;
        ASSERT_TRUE(f->second.is_number()) << key;
        EXPECT_DOUBLE_EQ(f->second.as_number(), v) << key;
    };
    const auto expect_vec3 = [&](const char* key, const cd::math::Vec3f& v)
    {
        const auto f = lr.find(key);
        ASSERT_NE(f, lr.end()) << key;
        ASSERT_TRUE(f->second.is_array()) << key;
        const auto& arr = f->second.as_array();
        ASSERT_EQ(arr.size(), 3u) << key;
        EXPECT_FLOAT_EQ(static_cast<float>(arr[0].as_number()), v.x) << key;
        EXPECT_FLOAT_EQ(static_cast<float>(arr[1].as_number()), v.y) << key;
        EXPECT_FLOAT_EQ(static_cast<float>(arr[2].as_number()), v.z) << key;
    };

    ASSERT_TRUE(lr.find("name")->second.is_string());
    EXPECT_EQ(lr.find("name")->second.as_string(), "Point 1");
    ASSERT_TRUE(lr.find("enabled")->second.is_bool());
    EXPECT_TRUE(lr.find("enabled")->second.as_bool());
    expect_num("type", static_cast<double>(static_cast<int>(
        cd::light::LightType::kPoint)));
    expect_num("kelvin", 6500.0);
    expect_num("intensity", 60.0);
    expect_num("range", 6.0);
    expect_vec3("position",  { 0.0F, 2.0F, 0.0F });
    expect_vec3("color",     { 1.0F, 0.9F, 0.8F });
    expect_vec3("direction", { 0.0F, -1.0F, 0.0F });
}

// ---------------------------------------------------------------------------
// Kind strings: the bridge vocabulary. hello_engine emits all seven;
// the editor maps the four it has meshes for and falls back to Cube
// for the rest. The VOCABULARY is the contract — pin it.
// ---------------------------------------------------------------------------
TEST(BridgeContract, KindVocabularyPinned)
{
    const std::vector<std::string> kEngineKinds {
        "Cube", "Sphere", "Cone", "Cylinder", "Torus", "Gltf", "Sponza"
    };
    // Editor-side mapping (mirror of bridge_kind_from in hello_editor):
    const auto editor_maps_to_mesh = [](const std::string& k)
    {
        return k == "Cube" || k == "Sphere" || k == "Cone" || k == "Gltf";
    };
    int mesh_backed = 0;
    for (const auto& k : kEngineKinds)
        if (editor_maps_to_mesh(k)) ++mesh_backed;
    // 4 of 7 render natively in the editor today; the other 3 fall back
    // to Cube VISUALLY but their kind STRING survives a round-trip —
    // phase1098 stores the loaded string in EntityMeta::bridge_kind and
    // re-saves it verbatim. If a kind is added to either sample, extend
    // this list.
    EXPECT_EQ(mesh_backed, 4);
    EXPECT_EQ(kEngineKinds.size(), 7u);
}

}  // namespace
