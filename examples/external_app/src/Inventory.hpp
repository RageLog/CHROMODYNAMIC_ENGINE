// =============================================================================
// examples/external_app/src/Inventory.hpp
//
// Downstream-app local types. Demonstrates the conventional pattern:
// downstream code defines its own components/structs and registers
// them with cd::ecs::World — engine-side ECS doesn't need to know
// anything about them.
// =============================================================================
#pragma once

#include <cd/ecs/Entity.hpp>
#include <cd/ecs/World.hpp>

#include <cstdint>
#include <string>
#include <utility>

namespace my_game {

/// Display name; component on every game object.
struct Name
{
    std::string text;
};

/// Pretend RPG stats. Three int counters — enough to demonstrate
/// ECS query + serialization, no game-design depth intended.
struct Stats
{
    std::int32_t hp { 100 };
    std::int32_t atk { 10 };
    std::int32_t def { 5 };
};

/// ECS-backed inventory manager. Holds a cd::ecs::World privately,
/// exposes a tiny API that the demo main() drives.
class InventoryDemo
{
public:
    [[nodiscard]] cd::ecs::Entity spawn(std::string name, Stats s);
    [[nodiscard]] std::size_t alive_count() const noexcept;
    [[nodiscard]] std::string serialize_to_json() const;

private:
    cd::ecs::World world_;
};

/// Build the demo scene: 12 game objects with deterministic
/// (name, stats) tuples. Returns the count of entities populated.
std::size_t populate_demo_inventory(InventoryDemo& demo);

}  // namespace my_game
