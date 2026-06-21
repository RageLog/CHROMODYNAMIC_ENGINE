// =============================================================================
// CHROMODYNAMIC — engine/asset/shader_cache/src/ShaderCache.cpp
// Phase 609 / Phase 1270 — cd::asset::shader_cache implementation
// =============================================================================

#include <cd/asset/shader_cache/ShaderCache.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <fstream>
#include <utility>
#include <vector>

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
    using std::chrono::duration_cast;
    using std::chrono::milliseconds;
    using std::chrono::system_clock;
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
    return len == 0 || in.read(out_s.data(), static_cast<std::streamsize>(len)).good();
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
    const bool is_new = !entries_.contains(key);

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

// ---------------------------------------------------------------------------
// compute_key — canonical cache-key hash utility
// ---------------------------------------------------------------------------

namespace
{

// FNV-1a 64-bit constants (defined again in cpp-internal scope for clarity;
// the header-side hash specialisation defines its own copies in std::).
constexpr std::uint64_t kFnvPrime64  = 1099511628211ULL;
constexpr std::uint64_t kFnvOffset64 = 14695981039346656037ULL;

[[nodiscard]] std::uint64_t fnv1a_byte(std::uint64_t h, std::uint8_t b) noexcept
{
    return (h ^ static_cast<std::uint64_t>(b)) * kFnvPrime64;
}

[[nodiscard]] std::uint64_t fnv1a_bytes(std::uint64_t h,
                                        const char*   data,
                                        std::size_t   len) noexcept
{
    for (std::size_t i = 0; i < len; ++i)
    {
        h = fnv1a_byte(h, static_cast<std::uint8_t>(data[i]));
    }
    return h;
}

[[nodiscard]] std::uint64_t fnv1a_u32(std::uint64_t h, std::uint32_t v) noexcept
{
    h = fnv1a_byte(h, static_cast<std::uint8_t>( v        & 0xFFU));
    h = fnv1a_byte(h, static_cast<std::uint8_t>((v >>  8) & 0xFFU));
    h = fnv1a_byte(h, static_cast<std::uint8_t>((v >> 16) & 0xFFU));
    h = fnv1a_byte(h, static_cast<std::uint8_t>((v >> 24) & 0xFFU));
    return h;
}

/// Lower-case hex encoding of a 64-bit value (16 chars, no prefix).
[[nodiscard]] std::string to_hex16(std::uint64_t v)
{
    constexpr std::array<char, 16> kHexDigits = {
        '0','1','2','3','4','5','6','7','8','9','a','b','c','d','e','f'
    };
    std::string out(16, '0');
    for (int i = 15; i >= 0; --i)
    {
        out[static_cast<std::size_t>(i)] = kHexDigits[v & 0xFU];
        v >>= 4U;
    }
    return out;
}

}  // namespace

ShaderKey compute_key(std::string_view                      source_text,
                      const std::vector<ShaderDefineEntry>& defines,
                      std::uint32_t                         stage,
                      std::string_view                      entry_point)
{
    // Sort a local copy of defines by (key, value) for determinism.
    std::vector<ShaderDefineEntry> sorted_defines = defines;
    std::ranges::sort(sorted_defines, [](const ShaderDefineEntry& a,
                                         const ShaderDefineEntry& b) {
        if (a.key != b.key) return a.key < b.key;
        return a.value < b.value;
    });

    std::uint64_t h = kFnvOffset64;

    // 1. Source text bytes.
    h = fnv1a_bytes(h, source_text.data(), source_text.size());
    h = fnv1a_byte(h, 0x00U);  // field separator

    // 2. Defines (sorted, key=0x01 sep, value=0x02 sep, define=0x03 sep).
    for (const auto& d : sorted_defines)
    {
        h = fnv1a_bytes(h, d.key.data(), d.key.size());
        h = fnv1a_byte(h, 0x01U);
        h = fnv1a_bytes(h, d.value.data(), d.value.size());
        h = fnv1a_byte(h, 0x03U);
    }
    h = fnv1a_byte(h, 0x00U);  // field separator after defines block

    // 3. Stage (4 bytes LE).
    h = fnv1a_u32(h, stage);

    // 4. Entry point.
    h = fnv1a_bytes(h, entry_point.data(), entry_point.size());
    h = fnv1a_byte(h, 0x00U);

    return ShaderKey{
        to_hex16(h),                       // source_hash — 16-char hex
        std::string{ entry_point },
        stage,
        0U                                 // spec_const_hash — caller sets if needed
    };
}

// ---------------------------------------------------------------------------
// is_entry_stale — file-mtime staleness check
// ---------------------------------------------------------------------------

bool is_entry_stale(const CacheEntry&            entry,
                    const std::filesystem::path& source_path) noexcept
{
    std::error_code ec;
    const auto lwt = std::filesystem::last_write_time(source_path, ec);
    if (ec)
    {
        // File missing or unreadable — conservatively treat as stale.
        return true;
    }

    // Convert filesystem file_time to milliseconds since Unix epoch.
    //
    // We derive an approximate system-clock millisecond value by computing
    // the offset between the file's last_write_time and the current
    // file_time_type::clock::now(), then subtracting that offset from the
    // current system_clock::now().  This avoids clock_cast which is not
    // universally available across MSVC / Clang / GCC combinations.
    //
    // The computation has sub-second precision on most platforms; for cache
    // staleness detection (files touched seconds apart) this is sufficient.
    using file_clock  = std::filesystem::file_time_type::clock;
    using sys_clock   = std::chrono::system_clock;
    using ms          = std::chrono::milliseconds;

    const auto file_now   = file_clock::now();
    const auto sys_now    = sys_clock::now();
    const auto file_delta = file_now - lwt;   // positive means file is older
    const auto sys_file_time = sys_now - std::chrono::duration_cast<sys_clock::duration>(file_delta);
    const std::uint64_t file_ms = static_cast<std::uint64_t>(
        std::chrono::duration_cast<ms>(sys_file_time.time_since_epoch()).count());

    // Stale if the file was written AFTER the entry was cached.
    return file_ms > entry.cached_at_ms;
}

// ---------------------------------------------------------------------------
// LruShaderCache — capacity-bounded LRU wrapper
// ---------------------------------------------------------------------------

LruShaderCache::LruShaderCache(std::size_t capacity)
    : capacity_{ capacity }
{
    // Enforce minimum capacity of 1; a cache of 0 is unusable.
    if (capacity_ == 0U)
    {
        capacity_ = 1U;
    }
}

bool LruShaderCache::put(const ShaderKey&           key,
                         std::vector<std::uint32_t> spirv,
                         std::string_view           entry_point,
                         std::uint32_t              stage)
{
    const auto it = map_.find(key);
    const bool is_new = (it == map_.end());

    CacheEntry entry;
    entry.spirv        = std::move(spirv);
    entry.entry_point  = std::string{ entry_point };
    entry.stage        = stage;
    entry.cached_at_ms = now_ms();

    if (!is_new)
    {
        // Update existing entry and move its list node to MRU (front).
        it->second.entry = std::move(entry);
        lru_order_.splice(lru_order_.begin(), lru_order_, it->second.list_it);
    }
    else
    {
        // Evict LRU if at capacity.
        if (map_.size() >= capacity_)
        {
            evict_lru();
        }

        // Insert new list node at MRU (front).
        lru_order_.push_front(key);
        MapEntry me;
        me.entry   = std::move(entry);
        me.list_it = lru_order_.begin();
        map_.emplace(key, std::move(me));
    }

    return is_new;
}

std::optional<const CacheEntry*> LruShaderCache::get(const ShaderKey& key)
{
    const auto it = map_.find(key);
    if (it == map_.end())
    {
        return std::nullopt;
    }

    // Promote to MRU.
    lru_order_.splice(lru_order_.begin(), lru_order_, it->second.list_it);
    return &it->second.entry;
}

bool LruShaderCache::save_to_disk(const std::filesystem::path& path) const
{
    // Write the CDSC format directly so that the original cached_at_ms
    // timestamps are preserved (ShaderCache::put() would overwrite them
    // with now_ms() if we delegated through a temporary ShaderCache).
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out.is_open())
    {
        return false;
    }

    // Header: magic (8 bytes) + entry count (4 bytes).
    out.write(reinterpret_cast<const char*>(kMagic.data()),
              static_cast<std::streamsize>(kMagic.size()));

    write_u32(out, static_cast<std::uint32_t>(map_.size()));

    for (const auto& key : lru_order_)
    {
        const auto& me = map_.at(key);
        write_str(out, key.source_hash);
        write_str(out, key.entry_point);
        write_u32(out, key.stage);
        write_u32(out, key.spec_const_hash);
        write_u32(out, static_cast<std::uint32_t>(me.entry.spirv.size()));
        if (!me.entry.spirv.empty())
        {
            out.write(
                reinterpret_cast<const char*>(me.entry.spirv.data()),
                static_cast<std::streamsize>(
                    me.entry.spirv.size() * sizeof(std::uint32_t)));
        }
        write_u64(out, me.entry.cached_at_ms);
    }

    return out.good();
}

bool LruShaderCache::load_from_disk(const std::filesystem::path& path)
{
    // Read the CDSC file directly so we can preserve cached_at_ms and
    // handle the capacity enforcement on the LRU structure.
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open())
    {
        return false;
    }

    // Validate magic.
    std::array<std::uint8_t, 8> magic_buf{};
    if (!in.read(reinterpret_cast<char*>(magic_buf.data()), 8))
    {
        return false;
    }
    if (magic_buf != kMagic)
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
        if (!read_str(in, key.source_hash))          { return false; }
        if (!read_str(in, key.entry_point))          { return false; }
        if (!read_u32(in, key.stage))                { return false; }
        if (!read_u32(in, key.spec_const_hash))      { return false; }

        std::uint32_t word_count = 0;
        if (!read_u32(in, word_count))               { return false; }

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

        // Insert via put() (which updates cached_at_ms with now).
        // We then patch the timestamp from the file to preserve it.
        put(key, std::move(entry.spirv), entry.entry_point, entry.stage);

        // Fix the timestamp: find the entry we just inserted and overwrite it.
        const auto map_it = map_.find(key);
        if (map_it != map_.end())
        {
            map_it->second.entry.cached_at_ms = entry.cached_at_ms;
        }
    }

    reorder_by_timestamp();
    enforce_capacity();
    return true;
}

void LruShaderCache::clear()
{
    lru_order_.clear();
    map_.clear();
}

std::size_t LruShaderCache::entry_count() const noexcept
{
    return map_.size();
}

void LruShaderCache::evict_lru()
{
    if (lru_order_.empty())
    {
        return;
    }
    const ShaderKey& lru_key = lru_order_.back();
    map_.erase(lru_key);
    lru_order_.pop_back();
}

void LruShaderCache::enforce_capacity()
{
    while (map_.size() > capacity_)
    {
        evict_lru();
    }
}

void LruShaderCache::reorder_by_timestamp()
{
    // Rebuild lru_order_ sorted by cached_at_ms so that enforce_capacity()
    // (which pops the back = LRU) evicts the truly oldest entries after a
    // disk load rather than the last-inserted.
    //
    // Collect (cached_at_ms, key) pairs in ascending order (oldest first →
    // placed at the back of lru_order_ = LRU position).
    std::vector<std::pair<std::uint64_t, const ShaderKey*>> order;
    order.reserve(map_.size());
    for (const auto& [key, me] : map_)
    {
        order.emplace_back(me.entry.cached_at_ms, &key);
    }
    // Descending: newest first → front of list (MRU), oldest last (LRU).
    std::ranges::sort(order, [](const auto& a, const auto& b) {
        return a.first > b.first;
    });

    lru_order_.clear();
    for (const auto& [ts, key_ptr] : order)
    {
        lru_order_.push_back(*key_ptr);
    }
    // Update iterators in the map to point at the new list positions.
    auto it = lru_order_.begin();
    for (const auto& [ts, key_ptr] : order)
    {
        map_.at(*key_ptr).list_it = it;
        ++it;
    }
}

}  // namespace cd::asset::shader_cache
