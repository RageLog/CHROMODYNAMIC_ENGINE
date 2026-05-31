// =============================================================================
// CHROMODYNAMIC — engine/asset/shader_cache/src/ShaderCache.cpp
// Phase 609 — cd::asset::shader_cache implementation
// =============================================================================

#include <cd/asset/shader_cache/ShaderCache.hpp>

#include <array>
#include <chrono>
#include <cstring>
#include <fstream>
#include <stdexcept>

namespace cd::asset::shader_cache
{

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

namespace
{

// File magic: "CDSC" + version bytes (major=1, minor=0, patch=0, reserved=0)
constexpr std::array<std::uint8_t, 8> kMagic = {
    'C', 'D', 'S', 'C', 0x00, 0x01, 0x00, 0x00
};

/// Current wall-clock milliseconds (monotonic enough for cache timestamps;
/// we use system_clock because we want wall time, not process-relative time).
[[nodiscard]] std::uint64_t now_ms() noexcept
{
    using namespace std::chrono;
    return static_cast<std::uint64_t>(
        duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count());
}

// ---- Little-endian serialisation helpers ----------------------------------

void write_u32(std::ostream& out, std::uint32_t v)
{
    const std::array<std::uint8_t, 4> buf = {
        static_cast<std::uint8_t>( v        & 0xFFU),
        static_cast<std::uint8_t>((v >>  8) & 0xFFU),
        static_cast<std::uint8_t>((v >> 16) & 0xFFU),
        static_cast<std::uint8_t>((v >> 24) & 0xFFU),
    };
    out.write(reinterpret_cast<const char*>(buf.data()),
              static_cast<std::streamsize>(buf.size()));
}

void write_u64(std::ostream& out, std::uint64_t v)
{
    const std::array<std::uint8_t, 8> buf = {
        static_cast<std::uint8_t>( v        & 0xFFU),
        static_cast<std::uint8_t>((v >>  8) & 0xFFU),
        static_cast<std::uint8_t>((v >> 16) & 0xFFU),
        static_cast<std::uint8_t>((v >> 24) & 0xFFU),
        static_cast<std::uint8_t>((v >> 32) & 0xFFU),
        static_cast<std::uint8_t>((v >> 40) & 0xFFU),
        static_cast<std::uint8_t>((v >> 48) & 0xFFU),
        static_cast<std::uint8_t>((v >> 56) & 0xFFU),
    };
    out.write(reinterpret_cast<const char*>(buf.data()),
              static_cast<std::streamsize>(buf.size()));
}

void write_str(std::ostream& out, const std::string& s)
{
    write_u32(out, static_cast<std::uint32_t>(s.size()));
    if (!s.empty())
    {
        out.write(s.data(), static_cast<std::streamsize>(s.size()));
    }
}

// ---- Little-endian deserialisation helpers --------------------------------

[[nodiscard]] bool read_u32(std::istream& in, std::uint32_t& out_v) noexcept
{
    std::array<std::uint8_t, 4> buf{};
    if (!in.read(reinterpret_cast<char*>(buf.data()), 4))
    {
        return false;
    }
    out_v = static_cast<std::uint32_t>(buf[0])
          | (static_cast<std::uint32_t>(buf[1]) <<  8)
          | (static_cast<std::uint32_t>(buf[2]) << 16)
          | (static_cast<std::uint32_t>(buf[3]) << 24);
    return true;
}

[[nodiscard]] bool read_u64(std::istream& in, std::uint64_t& out_v) noexcept
{
    std::array<std::uint8_t, 8> buf{};
    if (!in.read(reinterpret_cast<char*>(buf.data()), 8))
    {
        return false;
    }
    out_v = static_cast<std::uint64_t>(buf[0])
          | (static_cast<std::uint64_t>(buf[1]) <<  8)
          | (static_cast<std::uint64_t>(buf[2]) << 16)
          | (static_cast<std::uint64_t>(buf[3]) << 24)
          | (static_cast<std::uint64_t>(buf[4]) << 32)
          | (static_cast<std::uint64_t>(buf[5]) << 40)
          | (static_cast<std::uint64_t>(buf[6]) << 48)
          | (static_cast<std::uint64_t>(buf[7]) << 56);
    return true;
}

[[nodiscard]] bool read_str(std::istream& in, std::string& out_s)
{
    std::uint32_t len = 0;
    if (!read_u32(in, len))
    {
        return false;
    }
    out_s.resize(len);
    if (len > 0 && !in.read(out_s.data(), static_cast<std::streamsize>(len)))
    {
        return false;
    }
    return true;
}

}  // namespace

// ---------------------------------------------------------------------------
// ShaderCache — implementation
// ---------------------------------------------------------------------------

bool ShaderCache::put(const ShaderKey&           key,
                      std::vector<std::uint32_t> spirv,
                      std::string_view           entry_point,
                      std::uint32_t              stage)
{
    const bool is_new = (entries_.find(key) == entries_.end());

    CacheEntry entry;
    entry.spirv        = std::move(spirv);
    entry.entry_point  = std::string{ entry_point };
    entry.stage        = stage;
    entry.cached_at_ms = now_ms();

    entries_.insert_or_assign(key, std::move(entry));
    return is_new;
}

std::optional<const CacheEntry*> ShaderCache::get(const ShaderKey& key) const
{
    const auto it = entries_.find(key);
    if (it == entries_.end())
    {
        return std::nullopt;
    }
    return &it->second;
}

bool ShaderCache::save_to_disk(const std::filesystem::path& path) const
{
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out.is_open())
    {
        return false;
    }

    // Header: magic (8 bytes) + entry count (4 bytes).
    out.write(reinterpret_cast<const char*>(kMagic.data()),
              static_cast<std::streamsize>(kMagic.size()));
    write_u32(out, static_cast<std::uint32_t>(entries_.size()));

    for (const auto& [key, entry] : entries_)
    {
        write_str(out, key.source_hash);
        write_str(out, key.entry_point);
        write_u32(out, key.stage);
        write_u32(out, key.spec_const_hash);
        write_u32(out, static_cast<std::uint32_t>(entry.spirv.size()));
        if (!entry.spirv.empty())
        {
            out.write(
                reinterpret_cast<const char*>(entry.spirv.data()),
                static_cast<std::streamsize>(entry.spirv.size() * sizeof(std::uint32_t)));
        }
        write_u64(out, entry.cached_at_ms);
    }

    return out.good();
}

bool ShaderCache::load_from_disk(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open())
    {
        return false;
    }

    // Verify magic.
    std::array<std::uint8_t, 8> magic{};
    if (!in.read(reinterpret_cast<char*>(magic.data()), 8))
    {
        return false;
    }
    if (magic != kMagic)
    {
        return false;
    }

    std::uint32_t count = 0;
    if (!read_u32(in, count))
    {
        return false;
    }

    for (std::uint32_t i = 0; i < count; ++i)
    {
        ShaderKey key;
        if (!read_str(in, key.source_hash)) { return false; }
        if (!read_str(in, key.entry_point)) { return false; }
        if (!read_u32(in, key.stage))       { return false; }
        if (!read_u32(in, key.spec_const_hash)) { return false; }

        std::uint32_t word_count = 0;
        if (!read_u32(in, word_count))      { return false; }

        CacheEntry entry;
        entry.entry_point = key.entry_point;
        entry.stage       = key.stage;
        entry.spirv.resize(word_count);
        if (word_count > 0)
        {
            if (!in.read(reinterpret_cast<char*>(entry.spirv.data()),
                         static_cast<std::streamsize>(
                             word_count * sizeof(std::uint32_t))))
            {
                return false;
            }
        }
        if (!read_u64(in, entry.cached_at_ms)) { return false; }

        entries_.insert_or_assign(key, std::move(entry));
    }

    return true;
}

void ShaderCache::clear()
{
    entries_.clear();
}

std::size_t ShaderCache::entry_count() const noexcept
{
    return entries_.size();
}

}  // namespace cd::asset::shader_cache
