// =============================================================================
// CHROMODYNAMIC — cd/ecs/ComponentMask.hpp
// Phase 84.B / Wave 252 — fixed-bit component-set archetype mask.
//
// Unlike `QuerySig` (Phase 75 XOR-set hash, good for equality but
// can't detect overlap), `ComponentMask` assigns each component type
// a unique bit and uses bitwise AND for true overlap detection.
//
// Bit assignment requires a registry phase: `register_component<T>()`
// hands out the next bit; later `mask_of<T>()` returns the assigned
// bit. Two masks `a & b != 0` iff they share at least one component.
//
// 64-bit storage = 64 component types per process. Tight for indie
// engines; commercial engines may scale to 256+ via std::bitset.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>

#include <atomic>
#include <cstdint>
#include <typeindex>
#include <typeinfo>

namespace cd::ecs
{

namespace detail
{

[[nodiscard]] inline std::uint8_t next_component_bit() noexcept
{
    static std::atomic<std::uint8_t> next { 0 };
    return next.fetch_add(1, std::memory_order_relaxed);
}

template <class T>
[[nodiscard]] inline std::uint64_t component_bit() noexcept
{
    static const std::uint8_t b = next_component_bit();
    return std::uint64_t { 1 } << b;
}

}  // namespace detail

template <class T>
[[nodiscard]] inline std::uint64_t mask_of() noexcept
{
    return detail::component_bit<T>();
}

template <class... Ts>
[[nodiscard]] inline std::uint64_t make_mask() noexcept
{
    std::uint64_t m = 0;
    ((m |= mask_of<Ts>()), ...);
    return m;
}

[[nodiscard]] constexpr bool masks_overlap(std::uint64_t a, std::uint64_t b) noexcept
{
    return (a & b) != 0;
}

[[nodiscard]] constexpr bool mask_subset(std::uint64_t sub, std::uint64_t super) noexcept
{
    return (sub & super) == sub;
}

}  // namespace cd::ecs
