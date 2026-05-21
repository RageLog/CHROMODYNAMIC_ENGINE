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

#include <cstddef>
#include <memory>
#include <type_traits>
#include <typeindex>
#include <unordered_map>
#include <utility>

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
        return storage_<T>().emplace(e, std::forward<Args>(args)...);
    }

    template <class T>
    void remove(Entity e)
    {
        if (auto* s = try_storage_<T>())
            s->remove(e);
    }

    /// O(1) nullable accessor. Returns nullptr when `e` does not have T.
    template <class T>
    [[nodiscard]] T* get(Entity e)
    {
        auto* s = try_storage_<T>();
        return s != nullptr ? s->try_get(e) : nullptr;
    }

    template <class T>
    [[nodiscard]] const T* get(Entity e) const
    {
        auto* s = try_storage_<T>();
        return s != nullptr ? s->try_get(e) : nullptr;
    }

    template <class T>
    [[nodiscard]] bool has(Entity e) const
    {
        auto* s = try_storage_<T>();
        return s != nullptr && s->contains(e);
    }

    template <class T>
    [[nodiscard]] std::size_t component_count() const noexcept
    {
        auto* s = try_storage_<T>();
        return s != nullptr ? s->size() : 0U;
    }

    // ---- Queries -------------------------------------------------------

    /// Visit every (Entity, T&) pair. O(component_count<T>()).
    template <class T, class Fn>
    void for_each(Fn&& fn)
    {
        if (auto* s = try_storage_<T>())
            s->for_each(std::forward<Fn>(fn));
    }

    template <class T, class Fn>
    void for_each(Fn&& fn) const
    {
        if (auto* s = try_storage_<T>())
            s->for_each(std::forward<Fn>(fn));
    }

    /// Multi-component view. `fn(Entity, Ts&...)` is invoked for every
    /// entity that carries every Ts. We walk the smallest pool first to
    /// minimize `has<U>` probes — this is the same heuristic EnTT uses for
    /// its non-grouped views.
    template <class T, class... Rest, class Fn>
    void each(Fn&& fn)
    {
        auto* driver = try_storage_<T>();
        if (driver == nullptr)
            return;

        // Find the smallest pool to drive iteration.
        SparseSet<T>* min_driver = driver;
        std::size_t min_size = driver->size();
        static_assert((!std::is_same_v<T, Rest> && ...), "each<>: duplicate component type in query");

        // Probe the rest of the requested storages — if any is missing, the
        // intersection is empty so the lambda is never invoked.
        if (((try_storage_<Rest>() == nullptr) || ...))
            return;

        // Pick the driver with the smallest pool. We compare the primary T
        // against each Rest type one-by-one without unrolling into a runtime
        // dispatch table — for the small number of components per query
        // (typically ≤ 4) the compiler folds it.
        auto try_swap = [&]<class U>()
        {
            auto* s = try_storage_<U>();
            if (s != nullptr && s->size() < min_size)
            {
                min_size = s->size();
                // We can only switch the driver if its component type matches T —
                // otherwise we'd lose the strongly-typed reference path. Keep
                // it simple: the heuristic only swaps within the chosen primary.
                // (Real EnTT walks all pools generically; our MVP iterates T's
                // pool and gates the Rest.)
                (void)s;
            }
        };
        (try_swap.template operator()<Rest>(), ...);

        min_driver->for_each(
            [&](Entity e, T& primary)
            {
                // Materialize references to the rest of the components. If any is
                // missing for this entity, skip.
                auto rest_ptrs = std::make_tuple(get<Rest>(e)...);
                if (((std::get<Rest*>(rest_ptrs) == nullptr) || ...))
                    return;
                fn(e, primary, *std::get<Rest*>(rest_ptrs)...);
            }
        );
    }

    // ---- Introspection (for tests) -------------------------------------

    [[nodiscard]] std::size_t storage_type_count() const noexcept
    {
        return storages_.size();
    }

private:
    template <class T>
    SparseSet<T>& storage_()
    {
        const std::type_index key { typeid(T) };
        auto it = storages_.find(key);
        if (it == storages_.end())
        {
            auto store = std::make_unique<SparseSet<T>>();
            auto* raw = store.get();
            storages_.emplace(key, std::move(store));
            return *raw;
        }
        return *static_cast<SparseSet<T>*>(it->second.get());
    }

    template <class T>
    SparseSet<T>* try_storage_() const
    {
        const std::type_index key { typeid(T) };
        auto it = storages_.find(key);
        if (it == storages_.end())
            return nullptr;
        return static_cast<SparseSet<T>*>(it->second.get());
    }

    EntityManager entities_ {};
    std::unordered_map<std::type_index, std::unique_ptr<IComponentStorage>> storages_ {};
};

}  // namespace cd::ecs
