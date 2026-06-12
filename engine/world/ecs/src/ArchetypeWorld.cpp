// =============================================================================
// CHROMODYNAMIC -- engine/world/ecs/src/ArchetypeWorld.cpp
// Phase 370 / Marathon Run 30 / X7B -- archetype-table side layer.
// Phase 408 / D-F4 -- cross-archetype migration (add_component / remove_component).
// =============================================================================
#include <cd/ecs/ArchetypeWorld.hpp>

#include <algorithm>
#include <cstring>
#include <utility>

namespace cd::ecs
{

namespace
{
// Pick a chunk capacity targeting ~16 KiB of per-row payload, clamped
// at a minimum of 4 rows so wide rows make progress.
[[nodiscard]] std::size_t pick_chunk_capacity(std::size_t row_bytes) noexcept
{
    if (row_bytes == 0) return detail::kArchetypeChunkMinCapacity;
    const std::size_t cap = detail::kArchetypeChunkPayloadTargetBytes / row_bytes;
    return std::max(cap, detail::kArchetypeChunkMinCapacity);
}
}  // namespace

ArchetypeWorld::~ArchetypeWorld()
{
    // Destroy every live component value before the chunk byte storage
    // is reclaimed.  We walk archetypes in unspecified order; component
    // destructors must not rely on cross-archetype ordering.
    for (auto& up : archetypes_) {
        Archetype& a = *up;
        for (auto& chunk_up : a.chunks) {
            Chunk& c = *chunk_up;
            for (std::size_t r = 0; r < c.size; ++r) {
                for (std::size_t k = 0; k < a.infos.size(); ++k) {
                    auto* base = c.columns[k].data();
                    a.infos[k].destruct(base + r * a.infos[k].size_bytes);
                }
            }
            c.size = 0;
            c.entities.clear();
        }
    }
}

std::size_t ArchetypeWorld::chunk_count() const noexcept
{
    std::size_t n = 0;
    for (const auto& up : archetypes_) n += up->chunks.size();
    return n;
}

bool ArchetypeWorld::is_alive(Entity e) const noexcept
{
    if (!entities_.is_alive(e)) return false;
    if (e.id >= entity_locations_.size()) return false;
    return entity_locations_[e.id].archetype != nullptr;
}

void ArchetypeWorld::destroy(Entity e)
{
    if (!is_alive(e)) return;
    EntityLocation loc = entity_locations_[e.id];
    Archetype& a = *loc.archetype;
    Chunk& c = *a.chunks[loc.chunk_index];
    chunk_swap_pop(a, c, loc.row_in_chunk);
    entity_locations_[e.id] = EntityLocation {};
    entities_.destroy(e);
}

ArchetypeWorld::Archetype* ArchetypeWorld::find_or_create_archetype(
    std::vector<ComponentTypeInfo> infos)
{
    // Linear search for an existing archetype with the same type set.
    // Expected N (distinct combos) is small (< 100); a hash would only
    // pay off beyond that.
    for (auto& up : archetypes_) {
        if (up->infos.size() != infos.size()) continue;
        bool match = true;
        for (std::size_t k = 0; k < infos.size(); ++k) {
            if (up->infos[k].type != infos[k].type) { match = false; break; }
        }
        if (match) return up.get();
    }
    auto a = std::make_unique<Archetype>();
    a->infos = std::move(infos);
    a->chunk_capacity = pick_chunk_capacity(a->row_bytes());
    archetypes_.push_back(std::move(a));
    return archetypes_.back().get();
}

void ArchetypeWorld::chunk_swap_pop(Archetype& a, Chunk& c, std::size_t row)
{
    const std::size_t last = c.size - 1U;
    if (row != last) {
        // Move every column from last -> row, then destroy the old row,
        // then destroy the (now-vacated) last slot.
        for (std::size_t k = 0; k < a.infos.size(); ++k) {
            auto* col_base = c.columns[k].data();
            auto* dst = col_base + row  * a.infos[k].size_bytes;
            auto* src = col_base + last * a.infos[k].size_bytes;
            // Destroy old occupant of `row`, then move-construct from
            // `last` over the freed slot.  We cannot destruct dst before
            // src because info.move_construct expects a fresh slot.
            a.infos[k].destruct(dst);
            a.infos[k].move_construct(dst, src);
            // The original `src` value was moved-from; run the
            // destructor on the moved-from object too.
            a.infos[k].destruct(src);
        }
        // Update the entity location for the row that just moved.
        const Entity moved_entity = c.entities[last];
        c.entities[row] = moved_entity;
        if (moved_entity.id < entity_locations_.size()) {
            entity_locations_[moved_entity.id].row_in_chunk = row;
        }
    } else {
        // No move needed; just destruct the last row.
        for (std::size_t k = 0; k < a.infos.size(); ++k) {
            auto* col_base = c.columns[k].data();
            a.infos[k].destruct(col_base + last * a.infos[k].size_bytes);
        }
    }
    c.entities.pop_back();
    --c.size;
}

}  // namespace cd::ecs
