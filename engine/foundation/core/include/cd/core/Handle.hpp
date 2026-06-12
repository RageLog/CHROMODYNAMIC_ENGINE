// =============================================================================
// CHROMODYNAMIC — cd/core/Handle.hpp
// ADR-001 §A (Strongly-typed handles) + ADR-005 §B (handle-based hot path)
// + ADR-017 P0 (DtForHil store/handle.hpp salvage)
//
// Phantom-tagged 64-bit packed handle:
//   layout: [ index:32 | generation:16 | type_id:16 ]
//
// Design notes:
//   - Tag = phantom type parameter for compile-time type safety. e.g.,
//       using TextureHandle = cd::core::Handle<struct TextureTag>;
//   - Cross-Tag conversion is forbidden by the type system.
//   - The runtime type_id field is preserved for cases where the handle is
//     stored in a type-erased container (`Handle<AnyTag>`) and needs runtime
//     re-checking; for compile-time-tagged handles this field is informational.
//   - Value 0 is the canonical "null" sentinel (cf. operator bool / is_valid).
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>

#include <compare>
#include <cstdint>
#include <functional>

namespace cd::core
{

/// Type-erased opaque tag used by `Handle<AnyTag>` when phantom typing is
/// inappropriate (e.g., heterogeneous resource registries). Prefer a domain
/// tag in new code.
struct AnyTag
{
};

/// Strongly-typed packed handle.
///
/// The `Tag` is a *phantom* type — it does not participate in the storage
/// layout (which is exactly 64 bits) but does participate in the type system,
/// so `Handle<TextureTag>` and `Handle<BufferTag>` are distinct types.
template <class Tag = AnyTag>
class Handle
{
public:
    using value_type = std::uint64_t;
    using index_type = std::uint32_t;
    using generation_type = std::uint16_t;
    using type_id_type = std::uint16_t;

    static constexpr value_type kIndexMask = 0xFFFFFFFFu;
    static constexpr value_type kGenerationMask = 0xFFFFu;
    static constexpr value_type kTypeIdMask = 0xFFFFu;
    static constexpr value_type kGenerationShift = 32u;
    static constexpr value_type kTypeIdShift = 48u;

    static constexpr index_type kMaxIndex = static_cast<index_type>(kIndexMask);
    static constexpr generation_type kMaxGeneration = static_cast<generation_type>(kGenerationMask);

    constexpr Handle() noexcept = default;

    constexpr explicit Handle(value_type packed) noexcept
        : value_ { packed }
    {
    }

    constexpr Handle(index_type index, generation_type generation, type_id_type type_id = 0) noexcept
        : value_ { pack(index, generation, type_id) }
    {
    }

    /// Canonical null handle. `Handle{}` and `null()` are interchangeable.
    [[nodiscard]] static constexpr Handle null() noexcept
    {
        return Handle {};
    }

    // --- Accessors ------------------------------------------------------------

    [[nodiscard]] constexpr value_type value() const noexcept
    {
        return value_;
    }

    [[nodiscard]] constexpr index_type index() const noexcept
    {
        return static_cast<index_type>(value_ & kIndexMask);
    }

    [[nodiscard]] constexpr generation_type generation() const noexcept
    {
        return static_cast<generation_type>((value_ >> kGenerationShift) & kGenerationMask);
    }

    [[nodiscard]] constexpr type_id_type type_id() const noexcept
    {
        return static_cast<type_id_type>((value_ >> kTypeIdShift) & kTypeIdMask);
    }

    [[nodiscard]] constexpr bool is_valid() const noexcept
    {
        return value_ != 0u;
    }

    [[nodiscard]] constexpr bool is_null() const noexcept
    {
        return value_ == 0u;
    }

    constexpr explicit operator bool() const noexcept
    {
        return is_valid();
    }

    // --- Mutating helpers (rare; HandleStore prefers fresh construction) ----

    constexpr void reset() noexcept
    {
        value_ = 0u;
    }

    // --- Comparison -----------------------------------------------------------
    // Defaulted on the packed 64-bit value; deterministic and trivial.

    friend constexpr auto operator<=>(const Handle&, const Handle&) noexcept = default;
    friend constexpr bool operator==(const Handle&, const Handle&) noexcept = default;

    // --- Packing helpers ------------------------------------------------------

    [[nodiscard]] static constexpr value_type
    pack(index_type index, generation_type generation, type_id_type type_id = 0) noexcept
    {
        return (static_cast<value_type>(index) & kIndexMask) |
               ((static_cast<value_type>(generation) & kGenerationMask) << kGenerationShift) |
               ((static_cast<value_type>(type_id) & kTypeIdMask) << kTypeIdShift);
    }

private:
    value_type value_ { 0u };
};

static_assert(sizeof(Handle<AnyTag>) == 8u, "Handle must pack into 64 bits");
static_assert(alignof(Handle<AnyTag>) <= 8u);

}  // namespace cd::core

// std::hash specialization so Handle works in std::unordered_map etc.
// NOLINTNEXTLINE(cert-dcl58-cpp) — specializing std::hash for a
// program-defined type is explicitly permitted ([namespace.std]/2);
// the check cannot distinguish this from forbidden std additions.
namespace std
{
template <class Tag>
struct hash<::cd::core::Handle<Tag>>
{
    [[nodiscard]] size_t operator()(const ::cd::core::Handle<Tag>& h) const noexcept
    {
        // 64-bit avalanche mixer (SplitMix64 finalizer) — high quality, branch-free.
        auto x = h.value();
        x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ULL;
        x = (x ^ (x >> 27)) * 0x94D049BB133111EBULL;
        x = x ^ (x >> 31);
        return static_cast<size_t>(x);
    }
};
}  // namespace std
