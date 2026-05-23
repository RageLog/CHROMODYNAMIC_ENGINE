// =============================================================================
// CHROMODYNAMIC — cd/asset/AssetId.hpp
// ADR-006 (Sprint S3.2) — strongly-typed stable asset identifier.
//
// AssetId is a 64-bit FNV-1a hash of the asset's VFS path. Hashes are stable
// across runs (input-independent), which makes them safe for save-files and
// cross-process references. Hash collisions are practically impossible for
// the path space a game ships with; a future debug build will optionally
// store the source string side-by-side for diagnostics.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>

#include <compare>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string_view>

namespace cd::asset
{

namespace detail
{

[[nodiscard]] constexpr std::uint64_t fnv1a_64(std::string_view s) noexcept
{
    constexpr std::uint64_t kOffset = 1469598103934665603ULL;
    constexpr std::uint64_t kPrime = 1099511628211ULL;
    std::uint64_t h = kOffset;
    for (auto c : s)
    {
        h ^= static_cast<std::uint8_t>(c);
        h *= kPrime;
    }
    return h;
}

}  // namespace detail

class AssetId
{
public:
    constexpr AssetId() noexcept = default;

    constexpr explicit AssetId(std::uint64_t v) noexcept
        : value_ { v }
    {
    }

    [[nodiscard]] static constexpr AssetId from_path(std::string_view path) noexcept
    {
        return AssetId { detail::fnv1a_64(path) };
    }

    [[nodiscard]] constexpr std::uint64_t value() const noexcept
    {
        return value_;
    }

    [[nodiscard]] constexpr bool is_valid() const noexcept
    {
        return value_ != 0;
    }

    constexpr explicit operator bool() const noexcept
    {
        return is_valid();
    }

    friend constexpr auto operator<=>(const AssetId&, const AssetId&) noexcept = default;

private:
    std::uint64_t value_ { 0 };
};

inline constexpr AssetId kNullAssetId {};

constexpr AssetId operator""_assetid(const char* s, std::size_t n) noexcept
{
    return AssetId::from_path(std::string_view { s, n });
}

}  // namespace cd::asset

namespace std
{
template <>
struct hash<cd::asset::AssetId>
{
    [[nodiscard]] std::size_t operator()(cd::asset::AssetId id) const noexcept
    {
        // Delegate to std::hash<uint64_t> so the conversion from 64-bit id to
        // size_t is handled portably (no useless-cast on 64-bit hosts, no
        // narrowing on 32-bit).
        return std::hash<std::uint64_t> {}(id.value());
    }
};
}  // namespace std
