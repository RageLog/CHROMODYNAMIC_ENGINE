// =============================================================================
// CHROMODYNAMIC — cd/core/EnumFlags.hpp
// Phase 85.B / Wave 253 — strongly-typed bitwise enum operators.
//
// `CD_ENUM_FLAGS(EnumType)` macro injects `operator|`, `operator&`,
// `operator|=`, `operator&=`, `operator~`, and `has` for an `enum
// class : T` so it can be used as a typed bitmask without casts:
//
//   enum class Flags : std::uint32_t { kNone = 0, kA = 1, kB = 2 };
//   CD_ENUM_FLAGS(Flags)
//
//   auto f = Flags::kA | Flags::kB;
//   if (cd::core::has(f, Flags::kA)) ...
//
// Avoids the runaway `static_cast<std::uint32_t>(Flags::kA) | ...`
// boilerplate across the codebase.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>

#include <type_traits>

namespace cd::core
{

template <class E>
[[nodiscard]] constexpr bool has(E set, E bit) noexcept
{
    using U = std::underlying_type_t<E>;
    return (static_cast<U>(set) & static_cast<U>(bit)) != 0;
}

}  // namespace cd::core

#define CD_ENUM_FLAGS(EnumType)                                                \
    [[nodiscard]] constexpr EnumType operator|(EnumType a, EnumType b) noexcept \
    {                                                                          \
        using U = std::underlying_type_t<EnumType>;                            \
        return static_cast<EnumType>(static_cast<U>(a) | static_cast<U>(b));   \
    }                                                                          \
    [[nodiscard]] constexpr EnumType operator&(EnumType a, EnumType b) noexcept \
    {                                                                          \
        using U = std::underlying_type_t<EnumType>;                            \
        return static_cast<EnumType>(static_cast<U>(a) & static_cast<U>(b));   \
    }                                                                          \
    [[nodiscard]] constexpr EnumType operator~(EnumType a) noexcept            \
    {                                                                          \
        using U = std::underlying_type_t<EnumType>;                            \
        return static_cast<EnumType>(~static_cast<U>(a));                      \
    }                                                                          \
    constexpr EnumType& operator|=(EnumType& a, EnumType b) noexcept           \
    {                                                                          \
        return a = a | b;                                                      \
    }                                                                          \
    constexpr EnumType& operator&=(EnumType& a, EnumType b) noexcept           \
    {                                                                          \
        return a = a & b;                                                      \
    }
