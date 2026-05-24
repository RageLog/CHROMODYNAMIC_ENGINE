// =============================================================================
// CHROMODYNAMIC — cd/asset/BundleMeta.hpp
// Phase 72.B / Wave 240 — fixed-size header for cooked asset bundles.
//
// Every cooked asset bundle (cdmesh, cdtex, cdscene) starts with a
// 32-byte BundleHeader followed by payload bytes. Fields are
// little-endian on disk; this struct is the in-memory form for
// reading + writing.
//
//   magic    — 4-byte tag identifying the bundle type ("CDMS"/"CDTX"/…)
//   version  — wire format version, bumped on layout change.
//   flags    — bitmask reserved for future extensions.
//   payload_size — bytes following the header.
//   payload_crc32 — CRC-32 of payload (Phase 35 codec).
//   asset_count — number of assets inside the bundle (≥ 1).
//
// `is_valid_header(h, expected_magic)` checks magic + version + a
// reasonable size sanity bound.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>

#include <cstdint>

namespace cd::asset
{

inline constexpr std::uint32_t kBundleMagicMesh    = 0x534D4443u;  // 'CDMS'
inline constexpr std::uint32_t kBundleMagicTexture = 0x58544443u;  // 'CDTX'
inline constexpr std::uint32_t kBundleMagicScene   = 0x4E534443u;  // 'CDSN'

inline constexpr std::uint16_t kBundleVersion = 1;

struct BundleHeader
{
    std::uint32_t magic         { 0 };
    std::uint16_t version       { kBundleVersion };
    std::uint16_t flags         { 0 };
    std::uint64_t payload_size  { 0 };
    std::uint32_t payload_crc32 { 0 };
    std::uint32_t asset_count   { 0 };
    std::uint64_t reserved      { 0 };   // padding to 32 bytes
};
static_assert(sizeof(BundleHeader) == 32, "BundleHeader must stay 32 bytes");

[[nodiscard]] inline bool is_valid_header(const BundleHeader& h,
                                          std::uint32_t expected_magic,
                                          std::uint64_t max_reasonable_size = (1ULL << 32)) noexcept
{
    return h.magic == expected_magic
        && h.version == kBundleVersion
        && h.payload_size <= max_reasonable_size
        && h.asset_count > 0;
}

}  // namespace cd::asset
