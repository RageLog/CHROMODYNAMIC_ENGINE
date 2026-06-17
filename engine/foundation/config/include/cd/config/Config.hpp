// =============================================================================
// CHROMODYNAMIC — cd/config/Config.hpp
// ADR-017 P4 (Sprint S2.8) — CVarRegistry persistence using cd::io binary.
//
// Wire format (little-endian via cd::io::BinaryStream):
//   u32 magic     = 0x43564152  ('CVAR')
//   u32 version   = 1
//   u32 count
//   for count entries:
//     u8  type-tag {0=bool, 1=int64, 2=double, 3=string}
//     u32 key_len; bytes key_len           // key
//     payload depending on type-tag:
//        bool:    u8
//        int64:   i64
//        double:  f64
//        string:  u32 len; bytes len
//
// Entries are written in sorted-key order so byte-equal files imply equal
// CVar state regardless of insertion order (reproducibility / git-friendly).
// =============================================================================
#pragma once

#include <cd/core/CVar.hpp>
#include <cd/core/ErrorCode.hpp>
#include <cd/core/Result.hpp>
#include <cd/io/BinaryStream.hpp>

#include <algorithm>
#include <ranges>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace cd::config
{

namespace config_errors
{
inline constexpr std::uint32_t kDomain = 0x0007;

enum class Code : std::uint32_t
{
    kOk = 0,
    kBadMagic = 1,
    kBadVersion = 2,
    kBadTypeTag = 3,
    kTruncated = 4,
};

[[nodiscard]] inline cd::core::ErrorCode make(Code c, std::string_view msg = {}) noexcept
{
    return cd::core::ErrorCode { kDomain, static_cast<std::uint32_t>(c), msg };
}
}  // namespace config_errors

inline constexpr std::uint32_t kMagic = 0x43564152u;  // 'CVAR'
inline constexpr std::uint32_t kVersion = 1u;

enum class TypeTag : std::uint8_t
{
    kBool = 0,
    kInt64 = 1,
    kDouble = 2,
    kString = 3,
};

/// Serialize all CVars from `registry` into `writer`. Order is canonical
/// (lexicographic by key) so the output is deterministic.
inline void save(cd::io::BinaryWriter& writer, const cd::core::CVarRegistry& registry)
{
    auto entries = registry.snapshot();
    std::ranges::sort(
        entries,
        [](const auto& a, const auto& b)
        {
            return a.first < b.first;
        }
    );

    writer.write<std::uint32_t>(kMagic);
    writer.write<std::uint32_t>(kVersion);
    writer.write<std::uint32_t>(static_cast<std::uint32_t>(entries.size()));

    for (const auto& [key, value] : entries)
    {
        writer.write_string(key);
        std::visit(
            [&](const auto& v)
            {
                using V = std::decay_t<decltype(v)>;
                if constexpr (std::is_same_v<V, bool>)
                {
                    writer.write<std::uint8_t>(static_cast<std::uint8_t>(TypeTag::kBool));
                    writer.write_bool(v);
                }
                else if constexpr (std::is_same_v<V, std::int64_t>)
                {
                    writer.write<std::uint8_t>(static_cast<std::uint8_t>(TypeTag::kInt64));
                    writer.write<std::int64_t>(v);
                }
                else if constexpr (std::is_same_v<V, double>)
                {
                    writer.write<std::uint8_t>(static_cast<std::uint8_t>(TypeTag::kDouble));
                    writer.write<double>(v);
                }
                else if constexpr (std::is_same_v<V, std::string>)
                {
                    writer.write<std::uint8_t>(static_cast<std::uint8_t>(TypeTag::kString));
                    writer.write_string(v);
                }
            },
            value
        );
    }
}

/// Decode the byte stream produced by `save()` into `registry`. Pre-existing
/// CVars NOT present in the stream are left untouched; the stream's entries
/// overwrite any clashing keys.
inline cd::core::Result<void> load(cd::io::BinaryReader& reader, cd::core::CVarRegistry& registry)
{
    auto magic = reader.read<std::uint32_t>();
    if (!magic.has_value())
        return std::unexpected(config_errors::make(config_errors::Code::kTruncated, "magic"));
    if (*magic != kMagic)
        return std::unexpected(config_errors::make(config_errors::Code::kBadMagic, "magic mismatch"));

    auto version = reader.read<std::uint32_t>();
    if (!version.has_value())
        return std::unexpected(config_errors::make(config_errors::Code::kTruncated, "version"));
    if (*version != kVersion)
        return std::unexpected(config_errors::make(config_errors::Code::kBadVersion, "unsupported version"));

    auto count = reader.read<std::uint32_t>();
    if (!count.has_value())
        return std::unexpected(config_errors::make(config_errors::Code::kTruncated, "count"));

    for (std::uint32_t i = 0; i < *count; ++i)
    {
        auto key = reader.read_string();
        if (!key.has_value())
            return std::unexpected(config_errors::make(config_errors::Code::kTruncated, "key"));

        auto tag_raw = reader.read<std::uint8_t>();
        if (!tag_raw.has_value())
            return std::unexpected(config_errors::make(config_errors::Code::kTruncated, "tag"));

        switch (static_cast<TypeTag>(*tag_raw))
        {
            case TypeTag::kBool:
            {
                auto v = reader.read_bool();
                if (!v.has_value())
                    return std::unexpected(config_errors::make(config_errors::Code::kTruncated, "bool"));
                registry.set(std::move(*key), cd::core::CVarValue { *v });
                break;
            }
            case TypeTag::kInt64:
            {
                auto v = reader.read<std::int64_t>();
                if (!v.has_value())
                    return std::unexpected(config_errors::make(config_errors::Code::kTruncated, "i64"));
                registry.set(std::move(*key), cd::core::CVarValue { *v });
                break;
            }
            case TypeTag::kDouble:
            {
                auto v = reader.read<double>();
                if (!v.has_value())
                    return std::unexpected(config_errors::make(config_errors::Code::kTruncated, "f64"));
                registry.set(std::move(*key), cd::core::CVarValue { *v });
                break;
            }
            case TypeTag::kString:
            {
                auto v = reader.read_string();
                if (!v.has_value())
                    return std::unexpected(config_errors::make(config_errors::Code::kTruncated, "str"));
                registry.set(std::move(*key), cd::core::CVarValue { std::move(*v) });
                break;
            }
            default:
                return std::unexpected(config_errors::make(config_errors::Code::kBadTypeTag, "unknown tag"));
        }
    }
    return {};
}

}  // namespace cd::config
