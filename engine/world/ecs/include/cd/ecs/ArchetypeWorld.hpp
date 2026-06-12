// =============================================================================
// CHROMODYNAMIC -- cd/ecs/ArchetypeWorld.hpp
//
// Phase 370 / Marathon Run 30 / X7B -- archetype-table SIDE LAYER on top
// of the engine sparse-set ECS.  ArchetypeWorld does NOT replace
// cd::ecs::World: it is an additional storage path the engine can mix
// in when a workload is dominated by full-row iteration over a small
// number of component combinations (Bevy / Flecs archetypal pattern).
//
// Per ADR-20260529-X7 follow-up: the rejected alternative "add
// archetype-style storage as a side layer" is now realised as a
// proof-of-concept storage path, not a primary refactor.  The
// sparse-set path remains load-bearing.
//
// SOTA references:
//   - Bevy ECS -- archetype graph + dense-row chunks (bevyengine.org).
//   - Flecs -- archetype tables + chunk sizing (flecs.dev).
//   - EnTT 3.x pool design (Skypjack) -- sparse-set primary.
//
// API summary:
//   ArchetypeWorld w;
//   const Entity e = w.emplace<Position, Velocity>(Position{1,2,3},
//                                                   Velocity{0,0,1});
//   w.each<Position, Velocity>([](Entity, Position&, Velocity&){...});
//   w.destroy(e);
//   w.add_component<Velocity>(e, Velocity{0,0,1});   // cross-archetype move
//   w.remove_component<Velocity>(e);                  // cross-archetype move
//
// Design notes:
//   - Archetype identity = sorted vector<type_index>.
//   - Each archetype owns chunks; chunk capacity targets 16 KiB
//     payload, clamped to 4 entities min for wide rows.
//   - Removal is row-level (swap-and-pop in the chunk).
//   - Cross-archetype moves (add_component / remove_component) copy
//     existing components to the target archetype then handle the old
//     slot manually (see migrate_shared_components_).
//     Single-threaded construction path; no lock needed.
//
// Thread safety: reads parallel, writes caller-serialised.
// =============================================================================
#pragma once

#include <cd/ecs/Entity.hpp>

#include <limits>
#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <type_traits>
#include <typeindex>
#include <typeinfo>
#include <utility>
#include <vector>

namespace cd::ecs
{

namespace detail
{
inline constexpr std::size_t kArchetypeChunkPayloadTargetBytes = static_cast<std::size_t>(16U) * 1024U;
inline constexpr std::size_t kArchetypeChunkMinCapacity = 4U;
}  // namespace detail

class ArchetypeWorld
{
public:
    ArchetypeWorld() = default;
    ~ArchetypeWorld();
    ArchetypeWorld(const ArchetypeWorld&) = delete;
    ArchetypeWorld& operator=(const ArchetypeWorld&) = delete;
    ArchetypeWorld(ArchetypeWorld&&) = default;
    ArchetypeWorld& operator=(ArchetypeWorld&&) = default;

    [[nodiscard]] std::size_t alive_count() const noexcept { return entities_.alive_count(); }
    [[nodiscard]] std::size_t archetype_count() const noexcept { return archetypes_.size(); }
    [[nodiscard]] std::size_t chunk_count() const noexcept;

    template <class... Ts>
    [[nodiscard]] std::size_t chunk_capacity_for() const;

    template <class... Ts>
    Entity emplace(Ts&&... values);

    [[nodiscard]] bool is_alive(Entity e) const noexcept;
    void destroy(Entity e);

    template <class... Ts, class Fn>
    void each(Fn&& fn);

    template <class... Ts, class Fn>
    void each(Fn&& fn) const;

    // -------------------------------------------------------------------------
    // Cross-archetype migration
    // -------------------------------------------------------------------------
    // add_component<T>(e, v):
    //   Entity must NOT already carry T (asserted).
    //   Moves entity to archetype {current types + T}; all existing components
    //   are move-constructed into the new row; T is constructed from v.
    //
    // remove_component<T>(e):
    //   Entity must carry T (asserted).
    //   Moves entity to archetype {current types - T}; surviving components are
    //   move-constructed into the new row; T is destructed.
    template <class T>
    void add_component(Entity e, T value);

    template <class T>
    void remove_component(Entity e);

private:
    struct ComponentTypeInfo
    {
        std::type_index type;
        std::size_t     size_bytes;
        void          (*destruct)(void* obj);
        void          (*move_construct)(void* dst, void* src);
    };

    template <class T>
    [[nodiscard]] static ComponentTypeInfo info_for() noexcept
    {
        return ComponentTypeInfo {
            std::type_index(typeid(T)),
            sizeof(T),
            +[](void* p) { static_cast<T*>(p)->~T(); },
            +[](void* dst, void* src) {
                ::new (dst) T(std::move(*static_cast<T*>(src)));
            }
        };
    }

    struct Chunk
    {
        std::size_t                          capacity { 0 };
        std::size_t                          size { 0 };
        std::vector<Entity>                  entities {};
        std::vector<std::vector<std::byte>>  columns {};
    };

    struct Archetype
    {
        std::vector<ComponentTypeInfo>      infos {};
        std::size_t                         chunk_capacity { 0 };
        std::vector<std::unique_ptr<Chunk>> chunks {};

        [[nodiscard]] std::size_t row_bytes() const noexcept
        {
            std::size_t s = 0;
            for (const auto& ci : infos) s += ci.size_bytes;
            return s;
        }

        [[nodiscard]] std::size_t column_for(std::type_index t) const noexcept
        {
            for (std::size_t k = 0; k < infos.size(); ++k)
                if (infos[k].type == t) return k;
            return static_cast<std::size_t>(-1);
        }
    };

    struct EntityLocation
    {
        Archetype*  archetype { nullptr };
        std::size_t chunk_index { 0 };
        std::size_t row_in_chunk { 0 };
    };

    [[nodiscard]] Archetype* find_or_create_archetype(
        std::vector<ComponentTypeInfo> infos);

    void chunk_swap_pop(Archetype& a, Chunk& c, std::size_t row);

    // Move-construct all components shared between src and dst archetypes
    // from (src_c, src_row) into a freshly appended row of dst.
    // Returns {destination_chunk*, destination_row_index}.
    // Does NOT construct brand-new types -- caller handles that.
    // Does NOT update entity lists or entity_locations_ -- caller handles that.
    // Does NOT destruct the source row -- caller handles that.
    [[nodiscard]] std::pair<Chunk*, std::size_t>
    migrate_shared_components(Archetype& dst, Archetype& src,
                               Chunk& src_c, std::size_t src_row);

    EntityManager entities_ {};
    std::vector<std::unique_ptr<Archetype>> archetypes_ {};
    std::vector<EntityLocation> entity_locations_ {};

    template <class... Ts>
    [[nodiscard]] static std::vector<ComponentTypeInfo> sorted_infos();

    template <class T0, class... Trest>
    void write_columns(Archetype& a, Chunk& c, std::size_t row,
                        T0&& v0, Trest&&... vrest);
    void write_columns(Archetype&, Chunk&, std::size_t) {}
};

// =============================================================================
// Inline / template implementations
// =============================================================================

template <class... Ts>
[[nodiscard]] inline std::vector<ArchetypeWorld::ComponentTypeInfo>
ArchetypeWorld::sorted_infos()
{
    std::vector<ComponentTypeInfo> v;
    v.reserve(sizeof...(Ts));
    (v.push_back(info_for<std::remove_cvref_t<Ts>>()), ...);
    std::sort(v.begin(), v.end(),
        [](const ComponentTypeInfo& a, const ComponentTypeInfo& b) {
            return a.type < b.type;
        });
    return v;
}

template <class... Ts>
[[nodiscard]] inline std::size_t ArchetypeWorld::chunk_capacity_for() const
{
    auto infos = sorted_infos<Ts...>();
    for (const auto& up : archetypes_) {
        if (up->infos.size() != infos.size()) continue;
        bool match = true;
        for (std::size_t k = 0; k < infos.size(); ++k) {
            if (up->infos[k].type != infos[k].type) { match = false; break; }
        }
        if (match) return up->chunk_capacity;
    }
    return 0;
}

template <class T0, class... Trest>
inline void ArchetypeWorld::write_columns(Archetype& a, Chunk& c, std::size_t row,
                                            T0&& v0, Trest&&... vrest)
{
    using U = std::remove_cvref_t<T0>;
    const auto col = a.column_for(std::type_index(typeid(U)));
    auto& column_bytes = c.columns[col];
    auto* slot = column_bytes.data() + row * sizeof(U);
    ::new (slot) U(std::forward<T0>(v0));
    write_columns(a, c, row, std::forward<Trest>(vrest)...);
}

template <class... Ts>
inline Entity ArchetypeWorld::emplace(Ts&&... values)
{
    static_assert(sizeof...(Ts) > 0, "ArchetypeWorld::emplace requires at least one component");
    auto infos = sorted_infos<Ts...>();
    Archetype* a = find_or_create_archetype(std::move(infos));

    Chunk* c = nullptr;
    if (!a->chunks.empty() && a->chunks.back()->size < a->chunks.back()->capacity) {
        c = a->chunks.back().get();
    } else {
        auto new_chunk = std::make_unique<Chunk>();
        new_chunk->capacity = a->chunk_capacity;
        new_chunk->size = 0;
        new_chunk->entities.reserve(a->chunk_capacity);
        new_chunk->columns.resize(a->infos.size());
        for (std::size_t k = 0; k < a->infos.size(); ++k) {
            new_chunk->columns[k].resize(a->chunk_capacity * a->infos[k].size_bytes);
        }
        a->chunks.push_back(std::move(new_chunk));
        c = a->chunks.back().get();
    }

    const Entity e = entities_.create();
    const std::size_t row = c->size;
    c->entities.push_back(e);
    ++c->size;
    write_columns(*a, *c, row, std::forward<Ts>(values)...);

    if (entity_locations_.size() <= e.id) {
        entity_locations_.resize(e.id + 1U);
    }
    entity_locations_[e.id] = EntityLocation {
        a,
        a->chunks.size() - 1U,
        row,
    };
    return e;
}

namespace detail
{
template <class T>
inline T& archetype_column_ref(std::byte* col_base, std::size_t row) noexcept
{
    return *reinterpret_cast<T*>(col_base + row * sizeof(T));
}
}  // namespace detail

template <class... Ts, class Fn>
inline void ArchetypeWorld::each(Fn&& fn)
{
    std::array<std::type_index, sizeof...(Ts)> req {
        std::type_index(typeid(std::remove_cvref_t<Ts>))...
    };
    std::sort(req.begin(), req.end());

    for (auto& up : archetypes_) {
        Archetype& a = *up;
        bool contains_all = true;
        std::size_t j = 0;
        for (const auto& want : req) {
            while (j < a.infos.size() && a.infos[j].type < want) ++j;
            if (j >= a.infos.size() || a.infos[j].type != want) {
                contains_all = false;
                break;
            }
        }
        if (!contains_all) continue;

        // Resolve column indices in declaration order of Ts...
        std::array<std::size_t, sizeof...(Ts)> cols {
            a.column_for(std::type_index(typeid(std::remove_cvref_t<Ts>)))...
        };

        // Cache column-base pointers once per chunk so the inner loop
        // is pure arithmetic.  Indexed by the Ts... pack position.
        for (auto& chunk_up : a.chunks) {
            Chunk& c = *chunk_up;
            std::array<std::byte*, sizeof...(Ts)> col_bases {};
            for (std::size_t k = 0; k < sizeof...(Ts); ++k) {
                col_bases[k] = c.columns[cols[k]].data();
            }
            for (std::size_t r = 0; r < c.size; ++r) {
                [&]<std::size_t... Is>(std::index_sequence<Is...>) {
                    fn(c.entities[r],
                       detail::archetype_column_ref<std::remove_cvref_t<Ts>>(col_bases[Is], r)...);
                }(std::index_sequence_for<Ts...>{});
            }
        }
    }
}

template <class... Ts, class Fn>
inline void ArchetypeWorld::each(Fn&& fn) const
{
    // const/non-const each() dedup; the non-const overload only iterates
    // (no structural mutation), so casting away const cannot write
    // through it.
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast)
    const_cast<ArchetypeWorld*>(this)->template each<Ts...>(std::forward<Fn>(fn));
}

// =============================================================================
// Cross-archetype migration -- inline member implementations
// =============================================================================

inline std::pair<ArchetypeWorld::Chunk*, std::size_t>
ArchetypeWorld::migrate_shared_components(
    Archetype& dst, Archetype& src,
    Chunk& src_c, std::size_t src_row)
{
    // Find or allocate a destination chunk with space.
    Chunk* dc = nullptr;
    if (!dst.chunks.empty() &&
        dst.chunks.back()->size < dst.chunks.back()->capacity)
    {
        dc = dst.chunks.back().get();
    } else {
        auto nc = std::make_unique<Chunk>();
        nc->capacity = dst.chunk_capacity;
        nc->size     = 0;
        nc->entities.reserve(dst.chunk_capacity);
        nc->columns.resize(dst.infos.size());
        for (std::size_t k = 0; k < dst.infos.size(); ++k)
            nc->columns[k].resize(dst.chunk_capacity * dst.infos[k].size_bytes);
        dst.chunks.push_back(std::move(nc));
        dc = dst.chunks.back().get();
    }

    const std::size_t dst_row = dc->size;
    // Move-construct each component present in BOTH src and dst.
    for (std::size_t dk = 0; dk < dst.infos.size(); ++dk) {
        const std::size_t sk = src.column_for(dst.infos[dk].type);
        if (sk == std::numeric_limits<std::size_t>::max()) continue;  // new type -- skip
        auto* dst_ptr = dc->columns[dk].data() + dst_row * dst.infos[dk].size_bytes;
        auto* src_ptr = src_c.columns[sk].data() + src_row * src.infos[sk].size_bytes;
        dst.infos[dk].move_construct(dst_ptr, src_ptr);
    }
    ++dc->size;
    return { dc, dst_row };
}

template <class T>
inline void ArchetypeWorld::add_component(Entity e, T value)
{
    assert(is_alive(e) && "add_component: entity is not alive");

    using U = std::remove_cvref_t<T>;
    const std::type_index new_type { typeid(U) };

    // Snapshot location BEFORE find_or_create_archetype_ may reallocate
    // archetypes_ (which would not invalidate entity_locations_ but the
    // pointer stored in old_loc.archetype could shift if archetypes_ were
    // a vector<Archetype> rather than vector<unique_ptr<Archetype>> --
    // safe here because unique_ptr ownership is stable).
    const EntityLocation old_loc = entity_locations_[e.id];
    Archetype& old_a = *old_loc.archetype;
    Chunk&     old_c = *old_a.chunks[old_loc.chunk_index];

    // Policy: duplicate add is a programming bug.
    assert(old_a.column_for(new_type) == static_cast<std::size_t>(-1)
           && "add_component: entity already carries this component type");
    if (old_a.column_for(new_type) != static_cast<std::size_t>(-1)) return;

    // Build the target infos: existing + new, re-sorted by type_index.
    std::vector<ComponentTypeInfo> new_infos = old_a.infos;
    new_infos.push_back(info_for<U>());
    std::sort(new_infos.begin(), new_infos.end(),
        [](const ComponentTypeInfo& a, const ComponentTypeInfo& b) {
            return a.type < b.type;
        });

    Archetype* new_a = find_or_create_archetype(std::move(new_infos));

    // Migrate shared components.  The source slots are move-from'd but still
    // occupy their memory; chunk_swap_pop_ will call destruct on them later,
    // which is well-defined (moved-from objects are destructible per C++).
    auto [dc, new_row] = migrate_shared_components(*new_a, old_a, old_c,
                                                    old_loc.row_in_chunk);

    // Construct the brand-new component in its column of the destination.
    const std::size_t new_col = new_a->column_for(new_type);
    assert(new_col != std::numeric_limits<std::size_t>::max());
    auto* new_slot = dc->columns[new_col].data() + new_row * sizeof(U);
    ::new (new_slot) U(std::move(value));

    // Register entity in the new chunk's entity list.
    dc->entities.push_back(e);

    // Swap-pop the old row.  new_type does not exist in old_a so
    // chunk_swap_pop_ never touches the new component's column.  The
    // shared-component slots are in moved-from (but destructible) state.
    chunk_swap_pop(old_a, old_c, old_loc.row_in_chunk);

    // Update entity location.
    const std::size_t new_chunk_idx = new_a->chunks.size() - 1U;
    entity_locations_[e.id] = EntityLocation { new_a, new_chunk_idx, new_row };
}

template <class T>
inline void ArchetypeWorld::remove_component(Entity e)
{
    assert(is_alive(e) && "remove_component: entity is not alive");

    using U = std::remove_cvref_t<T>;
    const std::type_index rem_type { typeid(U) };

    const EntityLocation old_loc = entity_locations_[e.id];
    Archetype& old_a = *old_loc.archetype;
    Chunk&     old_c = *old_a.chunks[old_loc.chunk_index];

    const std::size_t rem_col = old_a.column_for(rem_type);
    assert(rem_col != std::numeric_limits<std::size_t>::max()
           && "remove_component: entity does not carry this component type");
    if (rem_col == std::numeric_limits<std::size_t>::max()) return;

    // Build the target infos: existing - removed type.
    // old_a.infos is already sorted; filtering preserves order.
    std::vector<ComponentTypeInfo> new_infos;
    new_infos.reserve(old_a.infos.size() - 1U);
    for (const auto& ci : old_a.infos)
        if (ci.type != rem_type) new_infos.push_back(ci);

    Archetype* new_a = find_or_create_archetype(std::move(new_infos));

    // Migrate surviving components (rem_type is absent from new_a so
    // migrate_shared_components_ skips it automatically).
    auto [dc, new_row] = migrate_shared_components(*new_a, old_a, old_c,
                                                    old_loc.row_in_chunk);
    dc->entities.push_back(e);

    // Manual swap-pop of the old row.
    //
    // We cannot reuse chunk_swap_pop_ here because the removed component's
    // slot at old_row is still live (it was NOT move-from'd by migrate),
    // while the surviving components' slots were already move-from'd.
    //
    // Sequence for column k at old_row:
    //   k == rem_col : slot is LIVE -> destruct, then (if old_row != last)
    //                  move last slot over it and destruct last.
    //   k != rem_col : slot is MOVED-FROM (valid, destructible) ->
    //                  destruct it, then (if old_row != last) move last
    //                  slot over it and destruct last.
    {
        const std::size_t old_row = old_loc.row_in_chunk;
        const std::size_t last    = old_c.size - 1U;

        // Step 1: destruct the removed component's live value at old_row.
        {
            auto* p = old_c.columns[rem_col].data()
                      + old_row * old_a.infos[rem_col].size_bytes;
            old_a.infos[rem_col].destruct(p);
        }

        if (old_row != last) {
            for (std::size_t k = 0; k < old_a.infos.size(); ++k) {
                auto* dst_ptr = old_c.columns[k].data()
                                + old_row * old_a.infos[k].size_bytes;
                auto* src_ptr = old_c.columns[k].data()
                                + last    * old_a.infos[k].size_bytes;
                if (k == rem_col) {
                    // dst was already destructed (step 1); src is live.
                    old_a.infos[k].move_construct(dst_ptr, src_ptr);
                    old_a.infos[k].destruct(src_ptr);
                } else {
                    // dst holds a moved-from (but destructible) object.
                    old_a.infos[k].destruct(dst_ptr);
                    old_a.infos[k].move_construct(dst_ptr, src_ptr);
                    old_a.infos[k].destruct(src_ptr);
                }
            }
            // Patch location of the entity that just moved into old_row.
            const Entity moved_entity = old_c.entities[last];
            old_c.entities[old_row] = moved_entity;
            if (moved_entity.id < entity_locations_.size())
                entity_locations_[moved_entity.id].row_in_chunk = old_row;
        } else {
            // old_row IS the last row.
            // Removed component already destructed (step 1).
            // Surviving components hold moved-from objects; destruct them.
            for (std::size_t k = 0; k < old_a.infos.size(); ++k) {
                if (k == rem_col) continue;
                auto* p = old_c.columns[k].data()
                          + old_row * old_a.infos[k].size_bytes;
                old_a.infos[k].destruct(p);
            }
        }
        old_c.entities.pop_back();
        --old_c.size;
    }

    // Update entity location.
    const std::size_t new_chunk_idx = new_a->chunks.size() - 1U;
    entity_locations_[e.id] = EntityLocation { new_a, new_chunk_idx, new_row };
}

}  // namespace cd::ecs
