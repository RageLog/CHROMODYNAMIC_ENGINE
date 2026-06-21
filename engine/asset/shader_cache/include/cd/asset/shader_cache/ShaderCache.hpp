// =============================================================================
// CHROMODYNAMIC — cd/asset/shader_cache/ShaderCache.hpp
// Phase 609 / Phase 1270 — asset-tier in-process SPIR-V blob cache
//                           (cd::asset::shader_cache)
//
// Rationale / scope:
//   cd::shader::CachedCompiler provides a disk-backed compiler decorator.
//   This library is an *independent* asset-tier cache:
//     * Lives in the asset layer (not the render/shader layer).
//     * Stores already-compiled SPIR-V blobs keyed by ShaderKey.
//     * Optionally persists/restores the whole cache from a single binary file.
//     * No dependency on glslang or ICompiler; suitable for use by loaders
//       that receive pre-compiled SPIR-V from .pak / .cdtex pipeline.
//
// Charter — CPU-side completeness (Phase 1270):
//   [DONE] Cache-key hashing: compute_key() hashes source+defines+stage+entry
//          via FNV-1a-64 into a deterministic hex ShaderKey.source_hash.
//   [DONE] On-disk blob store: save_to_disk / load_from_disk (binary CDSC).
//   [DONE] In-memory LRU: LruShaderCache wraps ShaderCache with a capacity cap
//          and O(1) eviction via doubly-linked list + hash map.
//   [DONE] Staleness check: is_entry_stale() compares entry.cached_at_ms
//          with the filesystem last-write time of the source file.
//   [DONE] Serialization: existing CDSC little-endian binary format.
//   [SEALED] Live compiler invocation (SPIRV-Cross/DXC) — requires GPU
//            toolchain integration; sealed in PROJECT_COMPLETION_STATUS.md.
//   [SEALED] GPU pipeline-cache blob (VkPipelineCache / D3D12 PSO blob) —
//            requires RHI-layer API; sealed in PROJECT_COMPLETION_STATUS.md.
//
// Namespace: cd::asset::shader_cache
//
// File format (save_to_disk / load_from_disk):
//   [8B magic "CDSC\x00\x01\x00\x00"] [4B entry_count]
//   For each entry:
//     [4B source_hash length] [source_hash bytes (no NUL)]
//     [4B entry_point length] [entry_point bytes (no NUL)]
//     [4B stage]
//     [4B spec_const_hash]
//     [4B spirv word count] [spirv words * 4 bytes]
//     [8B cached_at_ms]
//
//   All multi-byte integers are little-endian. The format is intentionally
//   simple (no compression, no versioned frames); a future sprint can wrap
//   it with a streaming compressor or replace the magic for a v2 break.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>

#include <cstdint>
#include <filesystem>
#include <functional>
#include <list>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace cd::asset::shader_cache
{

// =============================================================================
// ShaderKey — cache lookup key
// =============================================================================

/// Identifies a unique compiled SPIR-V blob.
///
/// Fields:
///   source_hash      — hex or raw hash of the GLSL/HLSL source text (caller
///                      responsibility; the cache treats it as an opaque string).
///   entry_point      — e.g. "main", "vs_main".
///   stage            — maps to cd::shader::ShaderStage integral value;
///                      the cache layer is decoupled from that enum so it does
///                      not pull in <cd/shader/…> headers.
///   spec_const_hash  — hash of specialisation-constant values; 0 if none.
struct ShaderKey
{
    std::string  source_hash;
    std::string  entry_point;
    std::uint32_t stage            { 0 };
    std::uint32_t spec_const_hash  { 0 };

    [[nodiscard]] bool operator==(const ShaderKey& other) const noexcept
    {
        return source_hash     == other.source_hash
            && entry_point     == other.entry_point
            && stage           == other.stage
            && spec_const_hash == other.spec_const_hash;
    }

    [[nodiscard]] bool operator!=(const ShaderKey& other) const noexcept
    {
        return !(*this == other);
    }
};

}  // namespace cd::asset::shader_cache

// ---------------------------------------------------------------------------
// std::hash specialisation — must be in namespace std before unordered_map
// ---------------------------------------------------------------------------
namespace std
{
template<>
struct hash<cd::asset::shader_cache::ShaderKey>
{
    [[nodiscard]] std::size_t operator()(
        const cd::asset::shader_cache::ShaderKey& k) const noexcept
    {
        // FNV-1a 64-bit over all fields concatenated.
        constexpr std::uint64_t kFnvPrime  = 1099511628211ULL;
        constexpr std::uint64_t kFnvOffset = 14695981039346656037ULL;

        auto fnv_byte = [&](std::uint64_t h, std::uint8_t b) noexcept -> std::uint64_t {
            return (h ^ static_cast<std::uint64_t>(b)) * kFnvPrime;
        };

        auto fnv_str = [&](std::uint64_t h, const std::string& s) noexcept -> std::uint64_t {
            for (const char raw : s)
            {
                h = fnv_byte(h, static_cast<std::uint8_t>(raw));
            }
            // Null separator so "ab"+"cd" != "a"+"bcd"
            return fnv_byte(h, 0x00U);
        };

        auto fnv_u32 = [&](std::uint64_t h, std::uint32_t v) noexcept -> std::uint64_t {
            h = fnv_byte(h, static_cast<std::uint8_t>( v        & 0xFFU));
            h = fnv_byte(h, static_cast<std::uint8_t>((v >>  8) & 0xFFU));
            h = fnv_byte(h, static_cast<std::uint8_t>((v >> 16) & 0xFFU));
            h = fnv_byte(h, static_cast<std::uint8_t>((v >> 24) & 0xFFU));
            return h;
        };

        std::uint64_t h = kFnvOffset;
        h = fnv_str(h, k.source_hash);
        h = fnv_str(h, k.entry_point);
        h = fnv_u32(h, k.stage);
        h = fnv_u32(h, k.spec_const_hash);
        return static_cast<std::size_t>(h);
    }
};
}  // namespace std

namespace cd::asset::shader_cache
{

// =============================================================================
// CacheEntry — value stored per key
// =============================================================================

/// A cached SPIR-V compilation result.
struct CacheEntry
{
    std::vector<std::uint32_t> spirv;         ///< SPIR-V words.
    std::string                entry_point;   ///< Redundant but convenient for iteration.
    std::uint32_t              stage         { 0 };
    std::uint64_t              cached_at_ms  { 0 };  ///< Wall-clock ms at insertion (monotonic).
};

// =============================================================================
// ShaderCache
// =============================================================================

/// In-process cache of compiled SPIR-V blobs, keyed by ShaderKey.
///
/// Thread safety: NOT thread-safe. Callers must synchronise externally if
/// accessed from multiple threads (e.g. a single mutex around each call, or
/// use from a single asset-loading thread).
///
/// The cache owns the CacheEntry values. Pointers returned by get() are
/// invalidated by any subsequent put() or clear() call (unordered_map
/// rehash). Callers that hold pointers across mutations must copy the entry.
class ShaderCache
{
public:
    ShaderCache()  = default;
    ~ShaderCache() = default;

    // Non-copyable; movable.
    ShaderCache(const ShaderCache&)            = delete;
    ShaderCache& operator=(const ShaderCache&) = delete;
    ShaderCache(ShaderCache&&) noexcept            = default;
    ShaderCache& operator=(ShaderCache&&) noexcept = default;

    // -------------------------------------------------------------------------
    // Core operations
    // -------------------------------------------------------------------------

    /// Insert or overwrite a cache entry.
    ///
    /// @param key         The lookup key.
    /// @param spirv       SPIR-V word vector (moved into the entry).
    /// @param entry_point Entry-point name stored in the entry for convenience.
    /// @param stage       Stage value stored in the entry.
    /// @returns true  — entry was inserted (new key).
    ///          false — entry was updated (key already existed); still succeeds.
    bool put(const ShaderKey&                key,
             std::vector<std::uint32_t>      spirv,
             std::string_view               entry_point,
             std::uint32_t                  stage);

    /// Look up a compiled entry.
    ///
    /// @returns std::nullopt if the key is not present.
    ///          Otherwise a non-owning pointer to the stored CacheEntry.
    ///          The pointer is valid until the next put() / clear() / move.
    [[nodiscard]] std::optional<const CacheEntry*> get(const ShaderKey& key) const;

    // -------------------------------------------------------------------------
    // Persistence
    // -------------------------------------------------------------------------

    /// Serialise the entire cache to a single binary file.
    /// Creates or truncates `path`. Returns false on I/O error.
    [[nodiscard]] bool save_to_disk(const std::filesystem::path& path) const;

    /// Deserialise from a file previously written by save_to_disk().
    /// Merges loaded entries with any existing ones (later put() wins).
    /// Returns false if the file cannot be read or the magic/format is wrong.
    [[nodiscard]] bool load_from_disk(const std::filesystem::path& path);

    // -------------------------------------------------------------------------
    // Housekeeping
    // -------------------------------------------------------------------------

    /// Remove all entries from the cache (does NOT touch disk).
    void clear();

    /// Number of entries currently held.
    [[nodiscard]] std::size_t entry_count() const noexcept;

private:
    std::unordered_map<ShaderKey, CacheEntry> entries_;
};

// =============================================================================
// compute_key — canonical cache-key hash utility
// =============================================================================

/// A define passed to compute_key() as part of the variant discriminator.
///
/// The caller is responsible for stable ordering; compute_key() sorts the
/// list lexicographically (by key then value) before hashing so that
/// `{{"A","1"},{"B","2"}}` and `{{"B","2"},{"A","1"}}` yield the same key.
struct ShaderDefineEntry
{
    std::string key;
    std::string value;
};

/// Compute a deterministic ShaderKey from raw shader-compilation inputs.
///
/// FNV-1a-64 is mixed over:
///   * source_text bytes (the raw GLSL/HLSL source)
///   * a 0x00 field separator
///   * defines sorted by (key,value), each pair separated by 0x01/0x02
///   * stage (4 bytes LE)
///   * entry_point bytes + 0x00 separator
///
/// The resulting ShaderKey.source_hash is a 16-character lower-case hex
/// string.  stage and entry_point are also stored in the key fields so
/// two shaders with the same source but different stages map to different
/// entries (the hash already encodes them, but the explicit fields let
/// callers filter by stage without rehashing).
///
/// spec_const_hash is left at 0 by default; callers with specialisation
/// constants should set it on the returned key before use.
[[nodiscard]] ShaderKey compute_key(std::string_view                         source_text,
                                    const std::vector<ShaderDefineEntry>&    defines,
                                    std::uint32_t                            stage,
                                    std::string_view                         entry_point);

// =============================================================================
// is_entry_stale — file-mtime staleness check
// =============================================================================

/// Return true if the source file at `source_path` has been modified more
/// recently than `entry.cached_at_ms`.
///
/// Uses std::filesystem::last_write_time.  If the file does not exist or
/// the mtime cannot be read, returns `true` (conservative: treat as stale
/// so the caller re-compiles).
///
/// NOTE: cached_at_ms uses system_clock epoch milliseconds; last_write_time
/// is compared via the same epoch after conversion.  Sub-millisecond mtime
/// resolution is truncated.
[[nodiscard]] bool is_entry_stale(const CacheEntry&             entry,
                                  const std::filesystem::path&  source_path) noexcept;

// =============================================================================
// LruShaderCache — capacity-bounded LRU wrapper
// =============================================================================

/// In-process SPIR-V cache with a hard capacity limit and LRU eviction.
///
/// When `put()` would exceed `capacity`, the least-recently-used entry is
/// evicted first.  A `get()` promotes the accessed entry to most-recently-used.
///
/// Persistence: delegates to an internal ShaderCache for save_to_disk /
/// load_from_disk.  After load, if the number of loaded entries exceeds
/// `capacity`, the oldest (by cached_at_ms) are evicted until the cap is met.
///
/// Thread safety: NOT thread-safe.  Same contract as ShaderCache.
class LruShaderCache
{
public:
    /// Construct with a maximum entry count.  `capacity` must be >= 1.
    explicit LruShaderCache(std::size_t capacity);

    // Non-copyable; movable.
    LruShaderCache(const LruShaderCache&)            = delete;
    LruShaderCache& operator=(const LruShaderCache&) = delete;
    LruShaderCache(LruShaderCache&&) noexcept            = default;
    LruShaderCache& operator=(LruShaderCache&&) noexcept = default;

    // -------------------------------------------------------------------------
    // Core operations
    // -------------------------------------------------------------------------

    /// Insert or overwrite.  Promotes the entry to MRU.  If the cache is at
    /// capacity and the key is new, the LRU entry is evicted before insertion.
    ///
    /// @returns true  — entry was inserted (new key).
    ///          false — entry was updated (key already existed).
    bool put(const ShaderKey&           key,
             std::vector<std::uint32_t> spirv,
             std::string_view           entry_point,
             std::uint32_t              stage);

    /// Look up a compiled entry and promote it to MRU position.
    ///
    /// @returns std::nullopt if the key is not present.
    ///          Otherwise a const pointer to the stored CacheEntry.
    ///          The pointer is valid until the next put() / clear() / move.
    [[nodiscard]] std::optional<const CacheEntry*> get(const ShaderKey& key);

    // -------------------------------------------------------------------------
    // Persistence
    // -------------------------------------------------------------------------

    /// Serialise the entire cache to a single binary file (CDSC format).
    [[nodiscard]] bool save_to_disk(const std::filesystem::path& path) const;

    /// Deserialise.  After loading, enforces the capacity limit by evicting
    /// the oldest entries (by cached_at_ms) until entry_count() <= capacity().
    [[nodiscard]] bool load_from_disk(const std::filesystem::path& path);

    // -------------------------------------------------------------------------
    // Housekeeping
    // -------------------------------------------------------------------------

    void clear();

    [[nodiscard]] std::size_t entry_count() const noexcept;
    [[nodiscard]] std::size_t capacity()    const noexcept { return capacity_; }

private:
    // Ordered from MRU (front) to LRU (back).
    using KeyList   = std::list<ShaderKey>;
    using KeyListIt = KeyList::iterator;

    struct MapEntry
    {
        CacheEntry  entry;
        KeyListIt   list_it;
    };

    void evict_lru();
    void enforce_capacity();
    void reorder_by_timestamp();

    std::size_t                                             capacity_;
    KeyList                                                 lru_order_;
    std::unordered_map<ShaderKey, MapEntry>                 map_;
};

}  // namespace cd::asset::shader_cache
