// =============================================================================
// examples/external_app/src/Inventory.cpp
//
// Implementation for the downstream demo's local types. Shows how
// downstream code declares an ECS-backed manager class with
// cd::ecs::World as a private member, plus a small "save the
// inventory to a JSON string" helper using cd::asset_json.
// =============================================================================
#include "Inventory.hpp"

#include <cd/asset_json/Json.hpp>
#include <cd/ecs/World.hpp>

#include <array>
#include <string>
#include <utility>

namespace my_game {

cd::ecs::Entity InventoryDemo::spawn(std::string name, Stats s)
{
    const auto e = world_.create();
    world_.emplace<Name>(e, Name { std::move(name) });
    world_.emplace<Stats>(e, std::move(s));
    return e;
}

std::size_t InventoryDemo::alive_count() const noexcept
{
    std::size_t n = 0;
    world_.for_each<Name>([&](cd::ecs::Entity, const Name&) { ++n; });
    return n;
}

std::string InventoryDemo::serialize_to_json() const
{
    cd::asset_json::Array items;
    world_.for_each<Name>([&](cd::ecs::Entity e, const Name& n) {
        cd::asset_json::Object item;
        item["id"] = cd::asset_json::Value { static_cast<double>(e.id) };
        item["name"] = cd::asset_json::Value { n.text };
        if (const auto* s = world_.get<Stats>(e); s != nullptr)
        {
            cd::asset_json::Object stat_obj;
            stat_obj["hp"] = cd::asset_json::Value { static_cast<double>(s->hp) };
            stat_obj["atk"] = cd::asset_json::Value { static_cast<double>(s->atk) };
            stat_obj["def"] = cd::asset_json::Value { static_cast<double>(s->def) };
            item["stats"] = cd::asset_json::Value { std::move(stat_obj) };
        }
        items.push_back(cd::asset_json::Value { std::move(item) });
    });
    cd::asset_json::Object root;
    root["inventory"] = cd::asset_json::Value { std::move(items) };
    return cd::asset_json::serialize(cd::asset_json::Value { std::move(root) }, /*pretty=*/true);
}

std::size_t populate_demo_inventory(InventoryDemo& demo)
{
    // Deterministic 12-row dataset. Real game would load from disk.
    struct Row
    {
        const char* name;
        std::int32_t hp;
        std::int32_t atk;
        std::int32_t def;
    };
    constexpr std::array<Row, 12> kRoster {{
        {"Apprentice Sword",      80, 12,  4},
        {"Tin Shield",            70,  4, 12},
        {"Healing Tonic",         30,  0,  0},
        {"Iron Helm",             60,  2, 18},
        {"Wooden Bow",            55, 16,  2},
        {"Quiver of Arrows",      40,  0,  0},
        {"Steel Sword",          110, 24,  6},
        {"Mithril Plate",        130,  3, 28},
        {"Ring of Resilience",    40,  0,  8},
        {"Mage's Cloak",          75,  9, 10},
        {"Tome of Sparks",        20, 28,  0},
        {"Boots of Swiftness",    35,  6,  7},
    }};
    for (const auto& r : kRoster)
    {
        (void)demo.spawn(r.name, Stats { r.hp, r.atk, r.def });
    }
    return kRoster.size();
}

}  // namespace my_game
