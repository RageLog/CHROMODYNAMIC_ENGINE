// =============================================================================
// CHROMODYNAMIC — cd/core/HandleStore.hpp
// ADR-001 §A (Strongly-typed handles) + ADR-005 §B (handle-based hot path)
// + ADR-017 P0 (DtForHil store/table.hpp salvage)
//
// Generation-counter slot table:
//   - O(1) insert / lookup / erase
//   - Free-list embedded in unused slots (no separate free-index vector after
//     the first compaction sprint; v1 uses a simple side vector for clarity)
//   - Generation increment on every insert+erase to detect stale handles
//   - Single-threaded (S2.1.a). Concurrent variant lands in S2.1.c with
//     cd::concurrency primitives.
//   - Storage is std::vector for v1; swap-in for cd::mem::PoolAllocator after
//     S2.1.b.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <cd/core/Handle.hpp>
#include <cd/core/Result.hpp>

#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

namespace cd::core
{

namespace handle_store_errors
{
inline constexpr std::uint32_t kDomain = 0x0003;
enum class Code : std::uint32_t
{
    kCapacityExceeded = 1,    // max-index overflow
    kGenerationOverflow = 2,  // 16-bit generation rolled over (extremely rare)
    kStaleHandle = 3,
    kTypeMismatch = 4,
};

[[nodiscard]] inline ErrorCode make(Code c, std::string_view message = {}) noexcept
{
    return ErrorCode { kDomain, static_cast<std::uint32_t>(c), message };
}
}  // namespace handle_store_errors

/// Slot-based generation-counter store.
///
/// Template parameters:
///   T   — payload type (must be MoveConstructible)
///   Tag — phantom tag for type-safe handles (matches Handle<Tag>)
///
/// Thread safety: NOT thread-safe in v1. External synchronization required for
/// concurrent insert/erase. Single-writer / multi-reader is also unsafe (lookups
/// can race with erase). The S2.1.c concurrent variant will lift this.
template <class T, class Tag = AnyTag>
class HandleStore
{
public:
    using handle_type = Handle<Tag>;
    using value_type = T;
    using size_type = std::uint32_t;

    HandleStore() = default;
    ~HandleStore() = default;

    // Non-copyable (payload may be non-copyable; copy semantics rarely make
    // sense for an identity-bearing store). Movable.
    HandleStore(const HandleStore&) = delete;
    HandleStore& operator=(const HandleStore&) = delete;
    HandleStore(HandleStore&&) noexcept = default;
    HandleStore& operator=(HandleStore&&) noexcept = default;

    /// Reserve storage for at least `capacity` simultaneously-live items.
    /// Optional optimization — not required for correctness.
    void reserve(size_type capacity)
    {
        slots_.reserve(capacity);
        generations_.reserve(capacity);
    }

    /// Insert a payload and return a fresh handle. The handle remains valid
    /// until the corresponding `erase` call.
    [[nodiscard]] Result<handle_type> insert(T value)
    {
        size_type index = 0;
        if (!free_indices_.empty())
        {
            index = free_indices_.back();
            free_indices_.pop_back();
            slots_[index].emplace(std::move(value));
        }
        else
        {
            if (slots_.size() >= handle_type::kMaxIndex)
            {
                return std::unexpected(
                    handle_store_errors::make(
                        handle_store_errors::Code::kCapacityExceeded,
                        "HandleStore index space exhausted"
                    )
                );
            }
            index = static_cast<size_type>(slots_.size());
            slots_.emplace_back(std::in_place, std::move(value));
            generations_.push_back(kInitialGeneration);
        }
        // Live-slot generation parity: live slots use odd; freed slots use even.
        // On insert we bump from "freed-even" -> "live-odd" (or start at 1).
        if ((generations_[index] & 1u) == 0u)
        {
            generations_[index] = static_cast<std::uint16_t>(generations_[index] + 1u);
        }
        return handle_type { index, generations_[index], type_id_ };
    }

    /// Returns a pointer to the payload if the handle is live, otherwise nullptr.
    [[nodiscard]] T* get(handle_type h) noexcept
    {
        if (!is_live(h))
        {
            return nullptr;
        }
        return &slots_[h.index()].value();
    }

    [[nodiscard]] const T* get(handle_type h) const noexcept
    {
        if (!is_live(h))
        {
            return nullptr;
        }
        return &slots_[h.index()].value();
    }

    /// Returns true iff the handle still refers to a live slot in this store.
    [[nodiscard]] bool contains(handle_type h) const noexcept
    {
        return is_live(h);
    }

    /// Invalidate the handle and destroy the payload. Returns true if a live
    /// item was destroyed, false if the handle was already stale.
    bool erase(handle_type h) noexcept
    {
        if (!is_live(h))
        {
            return false;
        }
        const auto i = h.index();
        slots_[i].reset();
        // Bump generation (live-odd -> freed-even). At overflow we skip 0 to keep
        // the null sentinel reserved.
        auto next = static_cast<std::uint16_t>(generations_[i] + 1u);
        if (next == 0u)
        {
            next = 2u;  // Skip 0 (null) and 1 (start of live cycle for fresh slots).
        }
        generations_[i] = next;
        free_indices_.push_back(i);
        return true;
    }

    /// Number of live items.
    [[nodiscard]] size_type size() const noexcept
    {
        return static_cast<size_type>(slots_.size() - free_indices_.size());
    }

    [[nodiscard]] bool empty() const noexcept
    {
        return size() == 0u;
    }

    /// Capacity reserved (live + freed slots).
    [[nodiscard]] size_type capacity() const noexcept
    {
        return static_cast<size_type>(slots_.size());
    }

    /// Set the runtime type-id stamped into newly-created handles. Useful when
    /// this store backs a type-erased registry; defaults to 0.
    void set_type_id(typename handle_type::type_id_type id) noexcept
    {
        type_id_ = id;
    }

    [[nodiscard]] typename handle_type::type_id_type type_id() const noexcept
    {
        return type_id_;
    }

    /// Iterate live items. Predicate signature: void(handle, T&) or void(handle, const T&).
    template <class F>
    void for_each(F&& fn)
    {
        for (size_type i = 0; i < slots_.size(); ++i)
        {
            if (slots_[i].has_value())
            {
                std::forward<F>(fn)(handle_type { i, generations_[i], type_id_ }, slots_[i].value());
            }
        }
    }

    template <class F>
    void for_each(F&& fn) const
    {
        for (size_type i = 0; i < slots_.size(); ++i)
        {
            if (slots_[i].has_value())
            {
                std::forward<F>(fn)(handle_type { i, generations_[i], type_id_ }, slots_[i].value());
            }
        }
    }

private:
    static constexpr std::uint16_t kInitialGeneration = 0u;

    [[nodiscard]] bool is_live(handle_type h) const noexcept
    {
        if (!h.is_valid())
        {
            return false;
        }
        const auto i = h.index();
        if (i >= slots_.size())
        {
            return false;
        }
        if (generations_[i] != h.generation())
        {
            return false;
        }
        return slots_[i].has_value();
    }

    std::vector<std::optional<T>> slots_ {};
    std::vector<std::uint16_t> generations_ {};
    std::vector<size_type> free_indices_ {};
    typename handle_type::type_id_type type_id_ { 0 };
};

}  // namespace cd::core
