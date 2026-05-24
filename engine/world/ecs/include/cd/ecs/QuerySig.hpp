// =============================================================================
// CHROMODYNAMIC — cd/ecs/QuerySig.hpp
// Phase 74.B / Wave 242 — compile-time component-set signature.
//
// `query_signature<A, B, C>()` returns a 64-bit hash over a *set* of
// component types. Order-independent (XOR of per-type hashes), so
// query_signature<A, B>() == query_signature<B, A>().
//
// Used by tooling to label "what does this system query" — equality
// comparison tells you two systems touch the same set. (For overlap
// detection, see cd::ecs::Bitset-based archetype masks; XOR set hash
// can't answer "do these two sets share an element".)
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>

#include <cstdint>
#include <string_view>
#include <typeinfo>

namespace cd::ecs
{

namespace detail
{

[[nodiscard]] inline std::uint64_t fnv1a_64_str(std::string_view s) noexcept
{
    std::uint64_t h = 0xCBF29CE484222325ULL;
    for (char c : s)
    {
        h ^= static_cast<std::uint64_t>(static_cast<unsigned char>(c));
        h *= 0x100000001B3ULL;
    }
    return h;
}

template <class T>
[[nodiscard]] inline std::uint64_t type_sig() noexcept
{
    return fnv1a_64_str(typeid(T).name());
}

}  // namespace detail

template <class... Ts>
[[nodiscard]] inline std::uint64_t query_signature() noexcept
{
    std::uint64_t sig = 0;
    ((sig ^= detail::type_sig<Ts>()), ...);
    return sig;
}

}  // namespace cd::ecs
