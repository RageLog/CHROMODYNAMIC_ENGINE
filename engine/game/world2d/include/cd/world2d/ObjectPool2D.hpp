// =============================================================================
// CHROMODYNAMIC — cd/world2d/ObjectPool2D.hpp
// Milestone 3 — Lightweight Zero-Allocation Templated Object Pool with
// Generational Index Recycling and ABA Safety.
// =============================================================================
#pragma once

#include <cd/core/Handle.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <optional>
#include <span>
#include <type_traits>
#include <utility>
#include <vector>

namespace cd::world2d
{

struct IndexField
{
    std::uint32_t value { 0U };
    constexpr operator std::uint32_t() const noexcept { return value; }
    constexpr std::uint32_t operator()() const noexcept { return value; }
    constexpr IndexField& operator=(std::uint32_t v) noexcept { value = v; return *this; }
    friend constexpr bool operator==(const IndexField& a, const IndexField& b) noexcept { return a.value == b.value; }
    friend constexpr bool operator==(const IndexField& a, std::uint32_t b) noexcept { return a.value == b; }
    friend constexpr auto operator<=>(const IndexField& a, const IndexField& b) noexcept = default;
};

struct GenerationField
{
    std::uint16_t value { 0U };
    constexpr operator std::uint16_t() const noexcept { return value; }
    constexpr std::uint16_t operator()() const noexcept { return value; }
    constexpr GenerationField& operator=(std::uint16_t v) noexcept { value = v; return *this; }
    friend constexpr bool operator==(const GenerationField& a, const GenerationField& b) noexcept { return a.value == b.value; }
    friend constexpr bool operator==(const GenerationField& a, std::uint16_t b) noexcept { return a.value == b; }
    friend constexpr auto operator<=>(const GenerationField& a, const GenerationField& b) noexcept = default;
};

template <typename Tag>
struct PoolHandle
{
    IndexField      index { 0U };
    GenerationField generation { 0U };

    constexpr PoolHandle() = default;
    constexpr PoolHandle(std::uint32_t idx, std::uint16_t gen) noexcept
        : index { idx }, generation { gen }
    {
    }

    constexpr PoolHandle(cd::core::Handle<Tag> h) noexcept
        : index { h.index() }, generation { h.generation() }
    {
    }

    constexpr operator cd::core::Handle<Tag>() const noexcept
    {
        return cd::core::Handle<Tag>(index.value, generation.value);
    }

    [[nodiscard]] constexpr bool is_null() const noexcept { return generation.value == 0U; }
    [[nodiscard]] constexpr bool is_valid() const noexcept { return generation.value != 0U; }

    friend constexpr auto operator<=>(const PoolHandle&, const PoolHandle&) noexcept = default;
};

template <typename T, std::size_t StaticCapacity = 0U>
class ObjectPool2D
{
public:
    using HandleType = PoolHandle<T>;

    explicit ObjectPool2D(
        std::size_t initial_capacity = (StaticCapacity > 0U ? StaticCapacity : 256U),
        bool        allow_grow       = (StaticCapacity == 0U))
        : allow_grow_(allow_grow)
    {
        reserve(initial_capacity);
    }

    ~ObjectPool2D()
    {
        clear();
    }

    ObjectPool2D(const ObjectPool2D&) = delete;
    ObjectPool2D& operator=(const ObjectPool2D&) = delete;

    ObjectPool2D(ObjectPool2D&& other) noexcept
        : slots_(std::move(other.slots_))
        , free_indices_(std::move(other.free_indices_))
        , active_count_(other.active_count_)
        , allow_grow_(other.allow_grow_)
    {
        other.active_count_ = 0U;
    }

    ObjectPool2D& operator=(ObjectPool2D&& other) noexcept
    {
        if (this != &other)
        {
            clear();
            slots_        = std::move(other.slots_);
            free_indices_ = std::move(other.free_indices_);
            active_count_ = other.active_count_;
            allow_grow_   = other.allow_grow_;
            other.active_count_ = 0U;
        }
        return *this;
    }

    template <typename... Args>
    [[nodiscard]] std::optional<HandleType> emplace(Args&&... args)
    {
        if (free_indices_.empty())
        {
            if (!allow_grow_)
            {
                return std::nullopt;
            }
            grow(slots_.empty() ? 64U : slots_.size() * 2U);
        }

        const std::uint32_t idx = free_indices_.back();
        free_indices_.pop_back();

        auto& slot = slots_[idx];
        ::new (static_cast<void*>(slot.storage)) T(std::forward<Args>(args)...);
        slot.active = true;
        ++active_count_;

        return HandleType(idx, slot.generation);
    }

    [[nodiscard]] std::optional<HandleType> acquire()
    {
        return emplace();
    }

    bool release(HandleType handle)
    {
        if (!is_valid(handle))
        {
            return false;
        }
        const std::uint32_t idx = handle.index.value;
        auto& slot = slots_[idx];

        std::destroy_at(reinterpret_cast<T*>(slot.storage));
        slot.active = false;
        slot.generation = (slot.generation == 0xFFFFU) ? 1U : static_cast<std::uint16_t>(slot.generation + 1U);
        --active_count_;
        free_indices_.push_back(idx);
        return true;
    }

    bool release(cd::core::Handle<T> handle)
    {
        return release(HandleType(handle));
    }

    [[nodiscard]] T* get(HandleType handle) noexcept
    {
        if (!is_valid(handle))
        {
            return nullptr;
        }
        return reinterpret_cast<T*>(slots_[handle.index.value].storage);
    }

    [[nodiscard]] const T* get(HandleType handle) const noexcept
    {
        if (!is_valid(handle))
        {
            return nullptr;
        }
        return reinterpret_cast<const T*>(slots_[handle.index.value].storage);
    }

    [[nodiscard]] T* get(cd::core::Handle<T> handle) noexcept
    {
        return get(HandleType(handle));
    }

    [[nodiscard]] const T* get(cd::core::Handle<T> handle) const noexcept
    {
        return get(HandleType(handle));
    }

    [[nodiscard]] bool is_valid(HandleType handle) const noexcept
    {
        if (handle.is_null() || handle.index.value >= slots_.size())
        {
            return false;
        }
        const auto& slot = slots_[handle.index.value];
        return slot.active && (slot.generation == handle.generation.value);
    }

    [[nodiscard]] bool is_valid(cd::core::Handle<T> handle) const noexcept
    {
        return is_valid(HandleType(handle));
    }

    void clear() noexcept
    {
        for (std::size_t i = 0U; i < slots_.size(); ++i)
        {
            auto& slot = slots_[i];
            if (slot.active)
            {
                std::destroy_at(reinterpret_cast<T*>(slot.storage));
                slot.active = false;
                slot.generation = (slot.generation == 0xFFFFU) ? 1U : static_cast<std::uint16_t>(slot.generation + 1U);
            }
        }
        active_count_ = 0U;
        free_indices_.clear();
        for (std::size_t i = slots_.size(); i > 0U; --i)
        {
            free_indices_.push_back(static_cast<std::uint32_t>(i - 1U));
        }
    }

    [[nodiscard]] std::size_t active_count() const noexcept { return active_count_; }
    [[nodiscard]] std::size_t capacity() const noexcept { return slots_.size(); }
    [[nodiscard]] std::size_t available_count() const noexcept { return free_indices_.size(); }
    [[nodiscard]] bool empty() const noexcept { return active_count_ == 0U; }
    [[nodiscard]] bool full() const noexcept { return free_indices_.empty(); }

    template <typename Fn>
    void for_each_active(Fn&& fn)
    {
        for (std::size_t i = 0U; i < slots_.size(); ++i)
        {
            auto& slot = slots_[i];
            if (slot.active)
            {
                fn(*reinterpret_cast<T*>(slot.storage), HandleType(static_cast<std::uint32_t>(i), slot.generation));
            }
        }
    }

    template <typename Fn>
    void for_each_active(Fn&& fn) const
    {
        for (std::size_t i = 0U; i < slots_.size(); ++i)
        {
            const auto& slot = slots_[i];
            if (slot.active)
            {
                fn(*reinterpret_cast<const T*>(slot.storage), HandleType(static_cast<std::uint32_t>(i), slot.generation));
            }
        }
    }

private:
    struct Slot
    {
        alignas(alignof(T)) std::byte storage[sizeof(T)];
        std::uint16_t generation { 1U };
        bool          active { false };
    };

    void reserve(std::size_t count)
    {
        slots_.resize(count);
        free_indices_.reserve(count);
        free_indices_.clear();
        for (std::size_t i = count; i > 0U; --i)
        {
            free_indices_.push_back(static_cast<std::uint32_t>(i - 1U));
        }
    }

    void grow(std::size_t new_cap)
    {
        const std::size_t old_cap = slots_.size();
        if (new_cap <= old_cap)
        {
            return;
        }
        slots_.resize(new_cap);
        for (std::size_t i = new_cap; i > old_cap; --i)
        {
            free_indices_.push_back(static_cast<std::uint32_t>(i - 1U));
        }
    }

    std::vector<Slot>          slots_ {};
    std::vector<std::uint32_t> free_indices_ {};
    std::size_t                active_count_ { 0U };
    bool                       allow_grow_ { false };
};

}  // namespace cd::world2d
