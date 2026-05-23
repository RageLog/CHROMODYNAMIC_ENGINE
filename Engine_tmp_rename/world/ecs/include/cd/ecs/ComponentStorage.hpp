// =============================================================================
// CHROMODYNAMIC — cd/ecs/ComponentStorage.hpp
//
// Sparse-set component storage. Two-array layout (parallel `dense` +
// `components` plus a `sparse` lookup table indexed by entity-id) gives
// O(1) add / remove / lookup and O(N) iteration over the entities that
// actually carry the component — the SOTA primitive popularized by EnTT
// [Skypjack 2018] and adopted by every modern data-oriented ECS.
//
// Per-component-type instances of `SparseSet<T>` live behind a type-erased
// `IComponentStorage` interface so the World can hold a homogeneous map of
// them keyed on `std::type_index`.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <cd/ecs/Entity.hpp>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <utility>
#include <vector>

namespace cd::ecs
{

namespace detail
{
inline constexpr std::uint32_t kInvalidDenseIndex = std::numeric_limits<std::uint32_t>::max();
}  // namespace detail

/// Polymorphic erasure over `SparseSet<T>`. The World keeps a
/// map<type_index, unique_ptr<IComponentStorage>> and dispatches removal
/// through this interface — `remove()` is the only operation the World
/// ever needs at the erased level (typed access goes through the concrete
/// SparseSet pointer via type-aware downcasts).
class IComponentStorage
{
public:
    IComponentStorage() noexcept = default;
    virtual ~IComponentStorage() = default;
    IComponentStorage(const IComponentStorage&) = delete;
    IComponentStorage& operator=(const IComponentStorage&) = delete;
    IComponentStorage(IComponentStorage&&) = delete;
    IComponentStorage& operator=(IComponentStorage&&) = delete;

    /// Remove the component bound to `e` if present (no-op otherwise).
    virtual void remove(Entity e) = 0;
    /// Number of (entity, component) entries.
    [[nodiscard]] virtual std::size_t size() const noexcept = 0;
    /// True if entity has the component.
    [[nodiscard]] virtual bool contains(Entity e) const noexcept = 0;
};

template <class T>
class SparseSet final : public IComponentStorage
{
public:
    // ---- Mutators -------------------------------------------------------

    /// Insert or overwrite the component for `e`. Returns a reference to
    /// the stored component (dense storage; never null).
    template <class... Args>
    T& emplace(Entity e, Args&&... args)
    {
        ensure_sparse_(e.id);
        auto& slot = sparse_[e.id];
        if (slot != detail::kInvalidDenseIndex && dense_[slot] == e)
        {
            // Overwrite in place: copy/move-construct over the existing value
            // without changing the dense layout.
            components_[slot] = T(std::forward<Args>(args)...);
            return components_[slot];
        }
        const auto idx = static_cast<std::uint32_t>(dense_.size());
        dense_.push_back(e);
        components_.emplace_back(std::forward<Args>(args)...);
        slot = idx;
        return components_.back();
    }

    void remove(Entity e) override
    {
        if (!contains(e))
            return;
        const auto idx = sparse_[e.id];
        const auto last = static_cast<std::uint32_t>(dense_.size() - 1U);
        if (idx != last)
        {
            // Swap-and-pop: move the last entry into the freed slot.
            dense_[idx] = dense_[last];
            components_[idx] = std::move(components_[last]);
            sparse_[dense_[idx].id] = idx;
        }
        dense_.pop_back();
        components_.pop_back();
        sparse_[e.id] = detail::kInvalidDenseIndex;
    }

    // ---- Accessors ------------------------------------------------------

    [[nodiscard]] bool contains(Entity e) const noexcept override
    {
        if (e.id >= sparse_.size())
            return false;
        const auto idx = sparse_[e.id];
        if (idx == detail::kInvalidDenseIndex)
            return false;
        // Slot is reused across generations — confirm it's the same entity.
        return dense_[idx] == e;
    }

    [[nodiscard]] T* try_get(Entity e) noexcept
    {
        if (!contains(e))
            return nullptr;
        return &components_[sparse_[e.id]];
    }

    [[nodiscard]] const T* try_get(Entity e) const noexcept
    {
        if (!contains(e))
            return nullptr;
        return &components_[sparse_[e.id]];
    }

    [[nodiscard]] std::size_t size() const noexcept override
    {
        return dense_.size();
    }

    [[nodiscard]] bool empty() const noexcept
    {
        return dense_.empty();
    }

    /// O(N) iteration over (entity, component) pairs in insertion / swap-
    /// and-pop order. Stable enough for tests; do not assume ordering for
    /// gameplay logic.
    template <class Fn>
    void for_each(Fn&& fn)
    {
        for (std::size_t i = 0; i < dense_.size(); ++i)
        {
            fn(dense_[i], components_[i]);
        }
    }

    template <class Fn>
    void for_each(Fn&& fn) const
    {
        for (std::size_t i = 0; i < dense_.size(); ++i)
        {
            fn(dense_[i], components_[i]);
        }
    }

private:
    void ensure_sparse_(std::uint32_t id)
    {
        if (id >= sparse_.size())
        {
            sparse_.resize(id + 1U, detail::kInvalidDenseIndex);
        }
    }

    /// `sparse_[entity.id]` → dense-array index, or kInvalidDenseIndex.
    std::vector<std::uint32_t> sparse_ {};
    /// Tight packed list of entities that have this component.
    std::vector<Entity> dense_ {};
    /// Component values; parallel to `dense_`.
    std::vector<T> components_ {};
};

}  // namespace cd::ecs
