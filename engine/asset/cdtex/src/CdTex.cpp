// =============================================================================
// CHROMODYNAMIC — cd/asset/cdtex/CdTex.cpp
// =============================================================================
#include <cd/asset/cdtex/CdTex.hpp>

#include <algorithm>
#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

namespace cd::asset::cdtex
{

namespace
{

// magic5 + ver1 + width4 + height4 + block_w2 + block_h2 = 18 bytes.
constexpr std::size_t kHeaderSize = 18;
constexpr std::size_t kBytesPerBlock = 16;

[[nodiscard]] std::uint32_t read_u32_le(const std::uint8_t* p) noexcept
{
    return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
}

[[nodiscard]] std::uint16_t read_u16_le(const std::uint8_t* p) noexcept
{
    return static_cast<std::uint16_t>(static_cast<std::uint16_t>(p[0]) | (static_cast<std::uint16_t>(p[1]) << 8));
}

}  // namespace

cd::core::Result<CdTex> decode(const std::uint8_t* bytes, std::size_t size)
{
    if (bytes == nullptr)
    {
        return std::unexpected(cdtex_errors::make(cdtex_errors::Code::kInvalidArgument, "null buffer"));
    }
    if (size < kHeaderSize)
    {
        return std::unexpected(cdtex_errors::make(cdtex_errors::Code::kCorrupt, "header too small"));
    }

    if (bytes[0] != 'C' || bytes[1] != 'D' || bytes[2] != 'B' || bytes[3] != 'C' || bytes[4] != '7')
    {
        return std::unexpected(cdtex_errors::make(cdtex_errors::Code::kMagicMismatch, "bad magic"));
    }
    const std::uint8_t version = bytes[5];
    if (version != 1U && version != 2U)
    {
        return std::unexpected(cdtex_errors::make(cdtex_errors::Code::kVersionMismatch, "unsupported version"));
    }

    CdTex out;
    out.width = read_u32_le(bytes + 6);
    out.height = read_u32_le(bytes + 10);
    out.block_w = read_u16_le(bytes + 14);
    out.block_h = read_u16_le(bytes + 16);

    if (out.block_w == 0 || out.block_h == 0)
    {
        return std::unexpected(cdtex_errors::make(cdtex_errors::Code::kCorrupt, "zero block grid"));
    }

    // v1 = single mip with the 18-byte header. v2 = mip-chain with an
    // additional byte right after for the mip count.
    std::uint8_t mip_count = 1;
    std::size_t cursor = kHeaderSize;
    if (version == 2U)
    {
        if (size < kHeaderSize + 1)
        {
            return std::unexpected(cdtex_errors::make(cdtex_errors::Code::kCorrupt, "mip_count truncated"));
        }
        mip_count = bytes[kHeaderSize];
        if (mip_count == 0)
        {
            return std::unexpected(cdtex_errors::make(cdtex_errors::Code::kCorrupt, "mip_count == 0"));
        }
        ++cursor;
    }

    // Pre-flight: total payload bytes must fit in the buffer. This keeps
    // the canonical kCorrupt semantics for truncated files (rather than
    // a mid-decode kIoError when the bytes were on disk).
    std::size_t total_payload = 0;
    for (std::uint8_t lvl = 0; lvl < mip_count; ++lvl)
    {
        const std::uint32_t mw = std::max(out.width >> lvl, 1U);
        const std::uint32_t mh = std::max(out.height >> lvl, 1U);
        const std::size_t bw = (mw + 3U) / 4U;
        const std::size_t bh = (mh + 3U) / 4U;
        total_payload += bw * bh * kBytesPerBlock;
    }
    if (size < cursor + total_payload)
    {
        return std::unexpected(cdtex_errors::make(cdtex_errors::Code::kCorrupt, "truncated payload"));
    }

    out.mips.reserve(mip_count);
    for (std::uint8_t lvl = 0; lvl < mip_count; ++lvl)
    {
        CdTexMip m;
        m.width = std::max(out.width >> lvl, 1U);
        m.height = std::max(out.height >> lvl, 1U);
        m.block_w = static_cast<std::uint16_t>((m.width + 3U) / 4U);
        m.block_h = static_cast<std::uint16_t>((m.height + 3U) / 4U);
        const std::size_t payload =
            static_cast<std::size_t>(m.block_w) * static_cast<std::size_t>(m.block_h) * kBytesPerBlock;
        m.blocks.resize(payload);
        std::memcpy(m.blocks.data(), bytes + cursor, payload);
        cursor += payload;
        out.mips.push_back(std::move(m));
    }

    // Legacy `blocks` mirror — first mip. Lets v1-era consumers keep working
    // unchanged when they upgrade to v2 cooker output.
    out.blocks = out.mips[0].blocks;
    return out;
}

cd::core::Result<CdTex> load(std::string_view path)
{
    if (path.empty())
    {
        return std::unexpected(cdtex_errors::make(cdtex_errors::Code::kInvalidArgument, "empty path"));
    }
    const std::string p { path };
    if (!std::filesystem::exists(p))
    {
        return std::unexpected(cdtex_errors::make(cdtex_errors::Code::kFileNotFound, p));
    }

    std::ifstream in { p, std::ios::binary | std::ios::ate };
    if (!in.is_open())
    {
        return std::unexpected(cdtex_errors::make(cdtex_errors::Code::kIoError, p));
    }
    const auto file_size = in.tellg();
    in.seekg(0);

    std::vector<std::uint8_t> buf(static_cast<std::size_t>(file_size));
    if (file_size > 0 && !in.read(reinterpret_cast<char*>(buf.data()), file_size))
    {
        return std::unexpected(cdtex_errors::make(cdtex_errors::Code::kIoError, "read failed"));
    }
    return decode(buf.data(), buf.size());
}

}  // namespace cd::asset::cdtex
