// =============================================================================
// CHROMODYNAMIC — cd/asset_pak/Pak.hpp
// Phase 5 — single-file asset bundle format.
//
// A `.pak` bundle is one file containing many named assets (mesh + texture
// + json + wav + …) with a flat TOC. Use cases:
//   * Shipped game data — one disk read, one mmap, no per-asset open()
//   * Mod packs — drop a .pak in the load path; mod replaces base assets
//   * Reproducible test fixtures — embed every asset a sample needs
//
// Wire format (little-endian):
//   ┌──────────────── HEADER (24 B) ───────────────┐
//   │ magic[4]      "CDPK"                          │
//   │ version       u32 = 1                         │
//   │ entry_count   u32                             │
//   │ toc_offset    u64                             │
//   │ flags         u32 (reserved)                  │
//   └───────────────────────────────────────────────┘
//   Payload: every asset blob concatenated (no per-blob header).
//   TOC at `toc_offset`: entry_count records:
//     u16 name_len; bytes name_len name
//     u64 offset (into the file)
//     u64 size
//
// Author API: Builder pattern.
//   PakBuilder b;
//   b.add("textures/wall.png", png_bytes);
//   b.add("audio/explosion.wav", wav_bytes);
//   auto bytes = b.build();  // single byte buffer ready to write
//
// Reader API:
//   Pak pak;
//   pak.open(bytes.data(), bytes.size());
//   if (auto blob = pak.find("textures/wall.png"); blob)
//     image_loader.decode(blob->bytes, blob->size, ...);
//
// Header-only. cd::core only — same minimal dep as the other asset libs.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <cd/core/ErrorCode.hpp>
#include <cd/core/Result.hpp>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace cd::asset_pak
{

namespace pak_errors
{
inline constexpr std::uint32_t kDomain = 0x0018;

enum class Code : std::uint32_t
{
    kOk = 0,
    kMagicMismatch = 1,
    kVersionMismatch = 2,
    kCorrupt = 3,
    kNotFound = 4,
    kInvalidArgument = 5,
};

[[nodiscard]] inline cd::core::ErrorCode make(Code c, std::string_view m = {}) noexcept
{
    return cd::core::ErrorCode { kDomain, static_cast<std::uint32_t>(c), m };
}
}  // namespace pak_errors

inline constexpr std::uint32_t kPakVersion = 1;
inline constexpr std::size_t kPakHeaderSize = 24;

namespace pak_detail
{

inline void put_u16(std::vector<std::byte>& out, std::uint16_t v)
{
    const std::uint8_t b[2] = { static_cast<std::uint8_t>(v & 0xFFu),
                                static_cast<std::uint8_t>((v >> 8u) & 0xFFu) };
    out.insert(out.end(),
               reinterpret_cast<const std::byte*>(b),
               reinterpret_cast<const std::byte*>(b) + 2);
}
inline void put_u32(std::vector<std::byte>& out, std::uint32_t v)
{
    const std::uint8_t b[4] = { static_cast<std::uint8_t>(v & 0xFFu),
                                static_cast<std::uint8_t>((v >> 8u) & 0xFFu),
                                static_cast<std::uint8_t>((v >> 16u) & 0xFFu),
                                static_cast<std::uint8_t>((v >> 24u) & 0xFFu) };
    out.insert(out.end(),
               reinterpret_cast<const std::byte*>(b),
               reinterpret_cast<const std::byte*>(b) + 4);
}
inline void put_u64(std::vector<std::byte>& out, std::uint64_t v)
{
    put_u32(out, static_cast<std::uint32_t>(v & 0xFFFFFFFFull));
    put_u32(out, static_cast<std::uint32_t>((v >> 32u) & 0xFFFFFFFFull));
}

[[nodiscard]] inline std::uint16_t read_u16(const std::byte* p) noexcept
{
    const auto* b = reinterpret_cast<const std::uint8_t*>(p);
    const auto lo = static_cast<std::uint16_t>(b[0]);
    const auto hi = static_cast<std::uint16_t>(b[1]);
    return static_cast<std::uint16_t>(lo | static_cast<std::uint16_t>(hi << 8));
}
[[nodiscard]] inline std::uint32_t read_u32(const std::byte* p) noexcept
{
    const auto* b = reinterpret_cast<const std::uint8_t*>(p);
    return static_cast<std::uint32_t>(b[0]) | (static_cast<std::uint32_t>(b[1]) << 8)
         | (static_cast<std::uint32_t>(b[2]) << 16) | (static_cast<std::uint32_t>(b[3]) << 24);
}
[[nodiscard]] inline std::uint64_t read_u64(const std::byte* p) noexcept
{
    const std::uint64_t lo = read_u32(p);
    const std::uint64_t hi = read_u32(p + 4);
    return lo | (hi << 32);
}

}  // namespace pak_detail

class PakBuilder
{
public:
    /// Add an asset. `bytes` is copied — caller does not need to keep
    /// it alive past add(). Names are case-sensitive; duplicates throw
    /// (well, return false from build()). Maximum name length 65535 bytes.
    void add(std::string name, std::vector<std::byte> bytes)
    {
        entries_.push_back({ std::move(name), std::move(bytes) });
    }

    /// Convenience: copy from a std::string_view of raw bytes.
    void add(std::string name, std::string_view bytes)
    {
        std::vector<std::byte> b(bytes.size());
        if (!bytes.empty())
            std::memcpy(b.data(), bytes.data(), bytes.size());
        add(std::move(name), std::move(b));
    }

    [[nodiscard]] std::size_t entry_count() const noexcept { return entries_.size(); }

    /// Serialize the .pak byte buffer. Returns an empty buffer if any
    /// entry has a name longer than 65535 bytes (the wire format limit).
    [[nodiscard]] std::vector<std::byte> build() const
    {
        for (const auto& e : entries_)
        {
            if (e.name.size() > 65535)
                return {};
        }

        std::vector<std::byte> out;
        // Header layout: write magic now, fix toc_offset later.
        constexpr char kMagic[4] = { 'C', 'D', 'P', 'K' };
        out.insert(out.end(),
                   reinterpret_cast<const std::byte*>(kMagic),
                   reinterpret_cast<const std::byte*>(kMagic) + 4);
        pak_detail::put_u32(out, kPakVersion);
        pak_detail::put_u32(out, static_cast<std::uint32_t>(entries_.size()));
        const std::size_t toc_offset_slot = out.size();
        pak_detail::put_u64(out, 0);  // placeholder
        pak_detail::put_u32(out, 0);  // flags reserved

        // Payload: write each blob, remember offsets.
        std::vector<std::pair<std::uint64_t, std::uint64_t>> offsets_sizes;
        offsets_sizes.reserve(entries_.size());
        for (const auto& e : entries_)
        {
            offsets_sizes.emplace_back(static_cast<std::uint64_t>(out.size()),
                                       static_cast<std::uint64_t>(e.bytes.size()));
            out.insert(out.end(), e.bytes.begin(), e.bytes.end());
        }

        // TOC starts here.
        const auto toc_off = static_cast<std::uint64_t>(out.size());
        for (std::size_t i = 0; i < entries_.size(); ++i)
        {
            const auto& e = entries_[i];
            pak_detail::put_u16(out, static_cast<std::uint16_t>(e.name.size()));
            out.insert(out.end(),
                       reinterpret_cast<const std::byte*>(e.name.data()),
                       reinterpret_cast<const std::byte*>(e.name.data()) + e.name.size());
            pak_detail::put_u64(out, offsets_sizes[i].first);
            pak_detail::put_u64(out, offsets_sizes[i].second);
        }

        // Patch toc_offset into header.
        const auto* bytes = reinterpret_cast<const std::uint8_t*>(&toc_off);
        for (std::size_t k = 0; k < 8; ++k)
            out[toc_offset_slot + k] = std::byte { bytes[k] };

        return out;
    }

private:
    struct Entry
    {
        std::string name;
        std::vector<std::byte> bytes;
    };
    std::vector<Entry> entries_;
};

struct PakBlobView
{
    const std::byte* bytes { nullptr };
    std::size_t size { 0 };
};

class Pak
{
public:
    /// Parse a .pak buffer in-place. The Pak holds NON-OWNING pointers
    /// into `bytes`, so the caller must keep the buffer alive for the
    /// Pak's lifetime.
    [[nodiscard]] cd::core::Result<void> open(const std::byte* bytes, std::size_t size)
    {
        if (bytes == nullptr || size < kPakHeaderSize)
            return std::unexpected(pak_errors::make(pak_errors::Code::kCorrupt, "buffer too small"));
        if (bytes[0] != std::byte { 'C' } || bytes[1] != std::byte { 'D' } ||
            bytes[2] != std::byte { 'P' } || bytes[3] != std::byte { 'K' })
        {
            return std::unexpected(pak_errors::make(pak_errors::Code::kMagicMismatch, "bad magic"));
        }
        const auto version = pak_detail::read_u32(bytes + 4);
        if (version != kPakVersion)
            return std::unexpected(pak_errors::make(pak_errors::Code::kVersionMismatch, "version"));
        const auto entry_count = pak_detail::read_u32(bytes + 8);
        const auto toc_off = pak_detail::read_u64(bytes + 12);
        if (toc_off > size)
            return std::unexpected(pak_errors::make(pak_errors::Code::kCorrupt, "toc_offset OOB"));

        bytes_ = bytes;
        size_ = size;
        entries_.clear();
        std::size_t cursor = static_cast<std::size_t>(toc_off);
        for (std::uint32_t i = 0; i < entry_count; ++i)
        {
            if (cursor + 2 > size)
                return std::unexpected(pak_errors::make(pak_errors::Code::kCorrupt, "truncated TOC"));
            const auto name_len = pak_detail::read_u16(bytes + cursor);
            cursor += 2;
            if (cursor + name_len + 16 > size)
                return std::unexpected(pak_errors::make(pak_errors::Code::kCorrupt, "truncated entry"));
            std::string name(reinterpret_cast<const char*>(bytes + cursor), name_len);
            cursor += name_len;
            const auto off = pak_detail::read_u64(bytes + cursor);
            cursor += 8;
            const auto sz = pak_detail::read_u64(bytes + cursor);
            cursor += 8;
            if (off + sz > size)
                return std::unexpected(pak_errors::make(pak_errors::Code::kCorrupt, "blob extends past buffer"));
            entries_[std::move(name)] = { static_cast<std::size_t>(off), static_cast<std::size_t>(sz) };
        }
        return {};
    }

    [[nodiscard]] std::size_t entry_count() const noexcept { return entries_.size(); }

    /// Look up an asset by name. Returns nullopt if missing.
    [[nodiscard]] std::optional<PakBlobView> find(std::string_view name) const
    {
        const auto it = entries_.find(std::string { name });
        if (it == entries_.end())
            return std::nullopt;
        return PakBlobView { bytes_ + it->second.offset, it->second.size };
    }

    /// Iteration helper for "list all assets" UIs.
    template <class Fn>
    void for_each(Fn&& fn) const
    {
        for (const auto& [name, e] : entries_)
            fn(std::string_view { name }, PakBlobView { bytes_ + e.offset, e.size });
    }

private:
    struct Entry
    {
        std::size_t offset;
        std::size_t size;
    };
    const std::byte* bytes_ { nullptr };
    std::size_t size_ { 0 };
    std::unordered_map<std::string, Entry> entries_;
};

}  // namespace cd::asset_pak
