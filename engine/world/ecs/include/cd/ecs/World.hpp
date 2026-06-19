// =============================================================================
// CHROMODYNAMIC — cd/ecs/World.hpp
// Phase 4 / Sprint S4.1 — entity-component-system World.
//
// World is the engine's authoritative entity store. It owns:
//   * an EntityManager (generational slot allocator),
//   * per-component-type SparseSet<T> storages, keyed on std::type_index.
//
// API summary:
//   auto e = world.create();
//   world.emplace<Position>(e, 1.0f, 2.0f, 3.0f);
//   if (auto* p = world.get<Position>(e)) { ... }
//   world.each<Position, Velocity>([](Entity, Position&, Velocity&){...});
//
// Query semantics:
//   * `for_each<T>` iterates every entity that has T (single-pool walk).
//   * `each<Ts...>` iterates the intersection — picks the smallest pool
//     as the driver and gates on `has<U>` for the rest.
//
// Type identification uses `std::type_index` (RTTI). The engine codebase
// builds with default RTTI on; switching to a custom type-id mechanism is
// a follow-up if any back-end needs to strip RTTI.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <cd/ecs/ComponentStorage.hpp>
#include <cd/ecs/Entity.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <tuple>
#include <type_traits>
#include <typeindex>
#include <unordered_map>
#include <utility>
#include <vector>

namespace cd::ecs
{

class World
{
public:
    World() noexcept = default;
    ~World() = default;
    World(const World&) = delete;
    World& operator=(const World&) = delete;
    World(World&&) noexcept = default;
    World& operator=(World&&) noexcept = default;

    // ---- Entity lifecycle ----------------------------------------------

    [[nodiscard]] Entity create()
    {
        return entities_.create();
    }

    void destroy(Entity e)
    {
        if (!entities_.is_alive(e))
            return;
        // Strip every component before retiring the slot — otherwise stale
        // (entity-id, generation+1) reuse could collide with leftover storage
        // entries (slots match across generations in SparseSet<T>::contains).
        for (auto& [_, store] : storages_)
        {
            store->remove(e);
        }
        entities_.destroy(e);
    }

    [[nodiscard]] bool is_alive(Entity e) const noexcept
    {
        return entities_.is_alive(e);
    }

    [[nodiscard]] std::size_t alive_count() const noexcept
    {
        return entities_.alive_count();
    }

    // ---- Component lifecycle -------------------------------------------

    /// Construct a component in-place for `e`. Returns a reference to it.
    /// Calling emplace on an entity that already has T replaces the value.
    template <class T, class... Args>
    T& emplace(Entity e, Args&&... args)
    {
        static_assert(!std::is_reference_v<T>, "T must be a value type");
        return storage<T>().emplace(e, std::forward<Args>(args)...);
    }

    template <class T>
    void remove(Entity e)
    {
        if (auto* s = try_storage<T>())
            s->remove(e);
    }

    /// O(1) nullable accessor. Returns nullptr when `e` does not have T.
    template <class T>
    [[nodiscard]] T* get(Entity e)
    {
        auto* s = try_storage<T>();
        return s != nullptr ? s->try_get(e) : nullptr;
    }

    template <class T>
    [[nodiscard]] const T* get(Entity e) const
    {
        auto* s = try_storage<T>();
        return s != nullptr ? s->try_get(e) : nullptr;
    }

    template <class T>
    [[nodiscard]] bool has(Entity e) const
    {
        auto* s = try_storage<T>();
        return s != nullptr && s->contains(e);
    }

    template <class T>
    [[nodiscard]] std::size_t component_count() const noexcept
    {
        auto* s = try_storage<T>();
        return s != nullptr ? s->size() : 0U;
    }

    // ---- Queries -------------------------------------------------------

    /// Visit every (Entity, T&) pair. O(component_count<T>()).
    template <class T, class Fn>
    void for_each(Fn&& fn)
    {
        if (auto* s = try_storage<T>())
            s->for_each(std::forward<Fn>(fn));
    }

    template <class T, class Fn>
    void for_each(Fn&& fn) const
    {
        if (auto* s = try_storage<T>())
            s->for_each(std::forward<Fn>(fn));
    }

    /// Multi-component view. `fn(Entity, Ts&...)` is invoked for every
    /// entity that carries every Ts. We drive iteration off the SMALLEST
    /// of the requested pools so the per-entity `get<U>` intersection
    /// probes scale with `min(pool sizes)` instead of `size<T>()` — the
    /// same heuristic EnTT uses for its non-grouped views.
    ///
    /// The callback signature is fixed as `fn(Entity, T&, Rest&...)`; the
    /// driver pool is chosen purely for traversal cost, so the strongly-
    /// typed references are always re-resolved through `get<>` per entity.
    /// Iteration order is unspecified (it follows whichever pool drove the
    /// walk) — do not rely on it for gameplay logic.
    ///
    /// Mutation-safety: the driver pool's entity list is SNAPSHOT before
    /// the walk, so a callback that adds/removes components (which can
    /// swap-and-pop the live dense arrays) cannot invalidate the
    /// iteration. Entities removed mid-walk are skipped via a re-check.
    template <class T, class... Rest, class Fn>
    void each(Fn&& fn)
    {
        static_assert((!std::is_same_v<T, Rest> && ...), "each<>: duplicate component type in query");

        auto* driver = try_storage<T>();
        if (driver == nullptr)
            return;

        // Probe the rest of the requested storages — if any is missing, the
        // intersection is empty so the lambda is never invoked.
        if (((try_storage<Rest>() == nullptr) || ...))
            return;

        // Pick the smallest pool's entity list to drive iteration. We
        // compute the minimum size across T + every Rest, then snapshot
        // the matching pool's dense entity list. The driver carries no
        // type information (it is just a list of entity handles) — all
        // typed references are resolved per entity below.
        std::size_t min_size = driver->size();
        const auto probe = [&]<class U>()
        {
            const auto* s = try_storage<U>();
            min_size = std::min(min_size, s->size());
        };
        (probe.template operator()<Rest>(), ...);

        std::vector<Entity> snapshot;
        snapshot.reserve(min_size);
        if (driver->size() == min_size)
        {
            driver->for_each([&](Entity e, const T&) { snapshot.push_back(e); });
        }
        else
        {
            // The smallest pool is one of Rest; snapshot whichever Rest
            // pool has exactly min_size entries (first match wins).
            bool taken = false;
            const auto take = [&]<class U>()
            {
                auto* s = try_storage<U>();
                if (!taken && s->size() == min_size)
                {
                    taken = true;
                    s->for_each([&](Entity e, const U&) { snapshot.push_back(e); });
                }
            };
            (take.template operator()<Rest>(), ...);
        }

        for (const Entity e : snapshot)
        {
            // Re-resolve T and every Rest. Any nullptr means the entity
            // does not carry the full set (or was removed mid-walk) — skip.
            T* primary = get<T>(e);
            if (primary == nullptr)
                continue;
            auto rest_ptrs = std::make_tuple(get<Rest>(e)...);
            if (((std::get<Rest*>(rest_ptrs) == nullptr) || ...))
                continue;
            fn(e, *primary, *std::get<Rest*>(rest_ptrs)...);
        }
    }

    // ---- Cached queries -------------------------------------------------
    //
    // `each<T, Rest...>` re-resolves every component pool through a
    // std::type_index hash lookup per call. For systems that run every
    // frame the lookup cost dominates short bodies (Move, ApplyDrag, …).
    // A `Query` object snapshots the resolved pointers ONCE and reuses
    // them across calls. Storage pointers are stable for the lifetime
    // of a World (unique_ptr owns the SparseSet, the unordered_map only
    // owns the unique_ptr; rehashing the map cannot move the inner
    // object) so this caching is safe.
    //
    // Caveat: a Query built BEFORE its component type was first
    // emplace()d resolves to nullptr and skips iteration. Re-build the
    // query, or call `Query::refresh(world)`, after the type appears.

    template <class T, class... Rest>
    class Query
    {
    public:
        explicit Query(const World& w) noexcept
        {
            refresh(w);
        }

        /// Re-snapshot pointers. Call after a component type is first
        /// emplaced if the Query was built before that emplace.
        void refresh(const World& w) noexcept
        {
            driver_ = w.try_storage<T>();
            rest_present_ = (... && (w.try_storage<Rest>() != nullptr));
            last_version_ = w.structural_version();
        }

        /// True when every component pool referenced by the Query exists.
        [[nodiscard]] bool ready() const noexcept
        {
            return driver_ != nullptr && rest_present_;
        }

        /// True when the World has registered new component-type storages
        /// since this Query last refreshed. Pair with `auto_refresh()` to
        /// keep the cached pointers correct without paying for a refresh
        /// on every call.
        [[nodiscard]] bool is_stale(const World& w) const noexcept
        {
            return last_version_ != w.structural_version();
        }

        /// Refresh only if the structural version has advanced. Cheap
        /// (one u64 compare) on the no-change happy path; rebuilds the
        /// pointer snapshot otherwise.
        void auto_refresh(const World& w) noexcept
        {
            if (is_stale(w))
                refresh(w);
        }

        /// Iterate over every entity that has T and all of Rest. The
        /// callable signature is `void(Entity, T&, Rest&...)`. Iteration
        /// is silently skipped when any required pool is missing.
        ///
        /// Hot-path contract: this walks the cached `T` pool LIVE (no
        /// per-call snapshot) so the body MUST NOT add/remove the driver
        /// component `T` for entities it has not yet visited — doing so
        /// swap-and-pops the dense array under the cursor. Adding/removing
        /// `Rest` components, or removing `T` from the CURRENT entity, is
        /// safe. When structural mutation of `T` mid-walk is required, use
        /// the uncached `World::each<T, Rest...>` overload, which snapshots.
        template <class Fn>
        void each(World& w, Fn&& fn)
        {
            if (driver_ == nullptr || !rest_present_)
                return;
            driver_->for_each(
                [&](Entity e, T& primary)
                {
                    // `[[maybe_unused]]` silences GCC's spurious "set but not
                    // used" warning — the fold expressions below DO read the
                    // tuple but GCC's analyser misses them through the pack
                    // expansion.
                    [[maybe_unused]] auto rest_ptrs = std::make_tuple(w.template get<Rest>(e)...);
                    if (((std::get<Rest*>(rest_ptrs) == nullptr) || ...))
                        return;
                    fn(e, primary, *std::get<Rest*>(rest_ptrs)...);
                }
            );
        }

        [[nodiscard]] std::uint64_t version() const noexcept { return last_version_; }

    private:
        SparseSet<T>* driver_ { nullptr };
        bool rest_present_ { false };
        std::uint64_t last_version_ { 0 };
    };

    /// Factory that constructs a cached Query bound to this world.
    template <class T, class... Rest>
    [[nodiscard]] Query<T, Rest...> query() const
    {
        return Query<T, Rest...> { *this };
    }

    // ---- Introspection (for tests) -------------------------------------

    [[nodiscard]] std::size_t storage_type_count() const noexcept
    {
        return storages_.size();
    }

    /// Monotonically-increasing counter that bumps every time the set of
    /// registered component-type storages changes (i.e. the first
    /// `emplace<T>()` for a new T). Stable storage pointers held by a
    /// `Query` remain valid as long as the version they snapshotted is
    /// still current; if the version has advanced, the query MUST be
    /// refreshed before iterating again — otherwise a pool that didn't
    /// exist at construction time will be silently skipped.
    [[nodiscard]] std::uint64_t structural_version() const noexcept
    {
        return structural_version_;
    }

private:
    template <class T>
    SparseSet<T>& storage()
    {
        const std::type_index key { typeid(T) };
        auto it = storages_.find(key);
        if (it == storages_.end())
        {
            auto store = std::make_unique<SparseSet<T>>();
            auto* raw = store.get();
            storages_.emplace(key, std::move(store));
            ++structural_version_;
            return *raw;
        }
        return *static_cast<SparseSet<T>*>(it->second.get());
    }

    template <class T>
    SparseSet<T>* try_storage() const
    {
        const std::type_index key { typeid(T) };
        auto it = storages_.find(key);
        if (it == storages_.end())
            return nullptr;
        return static_cast<SparseSet<T>*>(it->second.get());
    }

    EntityManager entities_ {};
    std::unordered_map<std::type_index, std::unique_ptr<IComponentStorage>> storages_ {};
    std::uint64_t structural_version_ { 0 };
};

}  // namespace cd::ecs
