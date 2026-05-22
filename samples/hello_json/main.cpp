// =============================================================================
// CHROMODYNAMIC — samples/hello_json
// Shows cd::asset_json parse + edit + serialize round-trip. Headless, no GPU.
// =============================================================================
#include <cd/asset_json/Json.hpp>

#include <cstdio>
#include <string>
#include <string_view>

namespace
{

constexpr std::string_view kInput = R"({
  "name": "demo scene",
  "version": 1,
  "entities": [
    { "id": 1, "type": "camera",  "pos": [0.0, 1.7, -5.0] },
    { "id": 2, "type": "light",   "pos": [3.0, 4.0,  3.0], "intensity": 800.0 },
    { "id": 3, "type": "mesh",    "asset": "cube.cdmesh" }
  ]
})";

}  // namespace

int main()
{
    std::printf("=== hello_json — cd::asset_json round-trip ===\n");

    // 1. Parse.
    auto parsed = cd::asset_json::parse(kInput);
    if (!parsed)
    {
        std::printf("parse failed: %.*s\n",
            static_cast<int>(parsed.error().message.size()), parsed.error().message.data());
        return 1;
    }
    auto& root = *parsed;

    // 2. Probe — read a few fields via the typed accessors.
    auto name = root.at("name");
    if (name && (*name)->is_string())
        std::printf("scene name : %s\n", (*name)->as_string().c_str());
    auto ents = root.at("entities");
    if (ents && (*ents)->is_array())
        std::printf("entity count: %zu\n", (*ents)->as_array().size());

    // 3. Mutate — append a fourth entity.
    if (ents && (*ents)->is_array())
    {
        cd::asset_json::Object extra;
        extra["id"]    = cd::asset_json::Value { 4 };
        extra["type"]  = cd::asset_json::Value { std::string { "sound" } };
        extra["asset"] = cd::asset_json::Value { std::string { "kick.wav" } };
        // Casting through const_cast is acceptable here because at() returns
        // const Value*; the const-ness lets the lookup not perturb the
        // tree. The mutation path goes through the mutable root.
        auto& root_obj = root.as_object_mut();
        root_obj["entities"].as_array_mut().push_back(cd::asset_json::Value { std::move(extra) });
    }

    // 4. Serialize pretty + compact.
    const auto pretty  = cd::asset_json::serialize(root, true);
    const auto compact = cd::asset_json::serialize(root, false);
    std::printf("\n--- pretty ---\n%s\n", pretty.c_str());
    std::printf("\n--- compact (%zu chars) ---\n%s\n", compact.size(), compact.c_str());

    // 5. Round-trip — parse the compact serialization and verify equal.
    auto reparsed = cd::asset_json::parse(compact);
    if (!reparsed || !(*reparsed == root))
    {
        std::printf("round-trip mismatch\n");
        return 2;
    }

    std::printf("\n[hello_json] round-trip OK\n");
    return 0;
}
