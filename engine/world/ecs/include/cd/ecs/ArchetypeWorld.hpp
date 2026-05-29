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
//
// Design notes:
//   - Archetype identity = sorted vector<type_index>.
//   - Each archetype owns chunks; chunk capacity targets 16 KiB
//     payload, clamped to 4 entities min for wide rows.
//   - Removal is row-level (swap-and-pop in the chunk).
//   - Cross-archetype moves (add/remove a single component on a live
//     entity) NOT supported in this side layer.  Use sparse-set
//     cd::ecs::World for that.
//
// Thread safety: reads parallel, writes caller-serialised.
// =============================================================================
#pragma once

#include <cd/ecs/Entity.hpp>

#include <algorithm>
#include <array>
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
inline constexpr std::size_t kArchetypeChunkPayloadTargetBytes = 16U * 1024U;
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

private:
    struct ComponentTypeInfo
    {
        std::type_index type;
        std::size_t     size_bytes;
        void          (*destruct)(void* obj);
        void          (*move_construct)(void* dst, void* src);
    };

    template <class T>
    [[nodiscard]] static ComponentTypeInfo info_for_() noexcept
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

    [[nodiscard]] Archetype* find_or_create_archetype_(
        std::vector<ComponentTypeInfo> infos);

    void chunk_swap_pop_(Archetype& a, Chunk& c, std::size_t row);

    EntityManager entities_ {};
    std::vector<std::unique_ptr<Archetype>> archetypes_ {};
    std::vector<EntityLocation> entity_locations_ {};

    template <class... Ts>
    [[nodiscard]] static std::vector<ComponentTypeInfo> sorted_infos_();

    template <class T0, class... Trest>
    void write_columns_(Archetype& a, Chunk& c, std::size_t row,
                        T0&& v0, Trest&&... vrest);
    void write_columns_(Archetype&, Chunk&, std::size_t) {}
};

// =============================================================================
// Inline / template implementations
// =============================================================================

template <class... Ts>
[[nodiscard]] inline std::vector<ArchetypeWorld::ComponentTypeInfo>
ArchetypeWorld::sorted_infos_()
{
    std::vector<ComponentTypeInfo> v;
    v.reserve(sizeof...(Ts));
    (v.push_back(info_for_<std::remove_cvref_t<Ts>>()), ...);
    std::sort(v.begin(), v.end(),
        [](const ComponentTypeInfo& a, const ComponentTypeInfo& b) {
            return a.type < b.type;
        });
    return v;
}

template <class... Ts>
[[nodiscard]] inline std::size_t ArchetypeWorld::chunk_capacity_for() const
{
    auto infos = sorted_infos_<Ts...>();
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
inline void ArchetypeWorld::write_columns_(Archetype& a, Chunk& c, std::size_t row,
                                            T0&& v0, Trest&&... vrest)
{
    using U = std::remove_cvref_t<T0>;
    const auto col = a.column_for(std::type_index(typeid(U)));
    auto& column_bytes = c.columns[col];
    auto* slot = column_bytes.data() + row * sizeof(U);
    ::new (slot) U(std::forward<T0>(v0));
    write_columns_(a, c, row, std::forward<Trest>(vrest)...);
}

template <class... Ts>
inline Entity ArchetypeWorld::emplace(Ts&&... values)
{
    static_assert(sizeof...(Ts) > 0, "ArchetypeWorld::emplace requires at least one component");
    auto infos = sorted_infos_<Ts...>();
    Archetype* a = find_or_create_archetype_(std::move(infos));

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
    write_columns_(*a, *c, row, std::forward<Ts>(values)...);

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
inline T& archetype_column_ref_(std::byte* col_base, std::size_t row) noexcept
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
                       detail::archetype_column_ref_<std::remove_cvref_t<Ts>>(col_bases[Is], r)...);
                }(std::index_sequence_for<Ts...>{});
            }
        }
    }
}

template <class... Ts, class Fn>
inline void ArchetypeWorld::each(Fn&& fn) const
{
    const_cast<ArchetypeWorld*>(this)->template each<Ts...>(std::forward<Fn>(fn));
}

}  // namespace cd::ecs
