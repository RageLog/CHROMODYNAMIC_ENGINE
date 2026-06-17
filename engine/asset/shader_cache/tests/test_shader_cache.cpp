// =============================================================================
// CHROMODYNAMIC — engine/asset/shader_cache/tests/test_shader_cache.cpp
// Phase 609 — cd::asset::shader_cache unit tests
//
// Tests:
//   T1  put + get round-trip: stored SPIR-V matches what was inserted.
//   T2  missing key returns std::nullopt.
//   T3  save_to_disk + load_from_disk preserves all entries.
//   T4  clear() empties the cache (entry_count → 0).
//   T5  Different inputs produce different ShaderKey hashes (no collision).
//   T6  put() returns true for a new key, false for an overwrite.
//   T7  Overwrite via put() replaces SPIR-V; get() returns updated blob.
//   T8  load_from_disk rejects a file whose magic version bytes differ from the
//       current CDSC format (version-mismatch invalidation) without clobbering
//       any already-loaded entries.
//   T9  load_from_disk rejects a corrupt / truncated header (garbage prefix and
//       a header that ends mid-entry-count) — returns false, no partial load.
// =============================================================================

#include <cd/asset/shader_cache/ShaderCache.hpp>

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <optional>
#include <vector>

namespace
{

using cd::asset::shader_cache::CacheEntry;
using cd::asset::shader_cache::ShaderCache;
using cd::asset::shader_cache::ShaderKey;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

ShaderKey make_key(std::string source_hash  = "abc123",
                   std::string entry_point  = "main",
                   std::uint32_t stage      = 1,
                   std::uint32_t spec_hash  = 0)
{
    return ShaderKey{ std::move(source_hash),
                      std::move(entry_point),
                      stage,
                      spec_hash };
}

std::vector<std::uint32_t> make_spirv(std::uint32_t seed, std::size_t word_count = 8)
{
    std::vector<std::uint32_t> v(word_count);
    for (std::size_t i = 0; i < word_count; ++i)
    {
        v[i] = seed + static_cast<std::uint32_t>(i);
    }
    return v;
}

// ---------------------------------------------------------------------------
// T1 — put + get round-trip
// ---------------------------------------------------------------------------

TEST(ShaderCache, PutGetRoundTrip)
{
    ShaderCache cache;
    const ShaderKey key   = make_key("hash_t1", "vs_main", 2, 42);
    const auto      spirv = make_spirv(0xDEAD, 16);

    cache.put(key, spirv, "vs_main", 2);
    ASSERT_EQ(cache.entry_count(), 1U);

    const auto result = cache.get(key);
    ASSERT_TRUE(result.has_value());

    const CacheEntry* entry = *result;
    ASSERT_NE(entry, nullptr);
    EXPECT_EQ(entry->spirv,       spirv);
    EXPECT_EQ(entry->entry_point, "vs_main");
    EXPECT_EQ(entry->stage,       2U);
    EXPECT_GT(entry->cached_at_ms, 0U);
}

// ---------------------------------------------------------------------------
// T2 — missing key returns nullopt
// ---------------------------------------------------------------------------

TEST(ShaderCache, MissingKeyReturnsNullopt)
{
    const ShaderCache cache;  // empty
    const ShaderKey   key = make_key("not_inserted", "main", 1);

    const auto result = cache.get(key);
    EXPECT_FALSE(result.has_value());
}

// ---------------------------------------------------------------------------
// T3 — save_to_disk + load_from_disk preserves all entries
// ---------------------------------------------------------------------------

TEST(ShaderCache, SaveLoadPreservesAllEntries)
{
    // --- Build a cache with 3 entries ---
    ShaderCache src;
    const ShaderKey k1 = make_key("src_a", "main",    1, 0);
    const ShaderKey k2 = make_key("src_b", "fs_main", 2, 0);
    const ShaderKey k3 = make_key("src_c", "cs_main", 3, 99);

    const auto spirv1 = make_spirv(0x1111, 4);
    const auto spirv2 = make_spirv(0x2222, 8);
    const auto spirv3 = make_spirv(0x3333, 12);

    src.put(k1, spirv1, "main",    1);
    src.put(k2, spirv2, "fs_main", 2);
    src.put(k3, spirv3, "cs_main", 3);
    ASSERT_EQ(src.entry_count(), 3U);

    // --- Save ---
    const auto tmp = std::filesystem::temp_directory_path()
                   / "cd_shader_cache_test_save_load.bin";
    ASSERT_TRUE(src.save_to_disk(tmp));

    // --- Load into a fresh cache ---
    ShaderCache dst;
    ASSERT_TRUE(dst.load_from_disk(tmp));
    EXPECT_EQ(dst.entry_count(), 3U);

    // Verify each entry is intact.
    for (const auto& [key, expected_spirv] :
         std::vector<std::pair<ShaderKey, std::vector<std::uint32_t>>>{
             { k1, spirv1 }, { k2, spirv2 }, { k3, spirv3 }
         })
    {
        const auto r = dst.get(key);
        ASSERT_TRUE(r.has_value()) << "key missing after load";
        EXPECT_EQ((*r)->spirv, expected_spirv);
        EXPECT_EQ((*r)->stage, key.stage);
    }

    // Cleanup.
    std::filesystem::remove(tmp);
}

// ---------------------------------------------------------------------------
// T4 — clear() empties the cache
// ---------------------------------------------------------------------------

TEST(ShaderCache, ClearEmptiesCache)
{
    ShaderCache cache;
    cache.put(make_key("a", "main", 1), make_spirv(1), "main", 1);
    cache.put(make_key("b", "main", 2), make_spirv(2), "main", 2);
    cache.put(make_key("c", "main", 3), make_spirv(3), "main", 3);

    ASSERT_EQ(cache.entry_count(), 3U);

    cache.clear();
    EXPECT_EQ(cache.entry_count(), 0U);

    // get() on a cleared cache must return nullopt.
    EXPECT_FALSE(cache.get(make_key("a", "main", 1)).has_value());
}

// ---------------------------------------------------------------------------
// T5 — different inputs produce different hashes (no collision)
// ---------------------------------------------------------------------------

TEST(ShaderCache, DifferentInputsDifferentHashes)
{
    const std::hash<ShaderKey> hasher;

    const auto h1 = hasher(make_key("hash_AAA", "main", 1, 0));
    const auto h2 = hasher(make_key("hash_BBB", "main", 1, 0));
    const auto h3 = hasher(make_key("hash_AAA", "other", 1, 0));
    const auto h4 = hasher(make_key("hash_AAA", "main", 2, 0));
    const auto h5 = hasher(make_key("hash_AAA", "main", 1, 7));

    EXPECT_NE(h1, h2) << "source_hash change must change key hash";
    EXPECT_NE(h1, h3) << "entry_point change must change key hash";
    EXPECT_NE(h1, h4) << "stage change must change key hash";
    EXPECT_NE(h1, h5) << "spec_const_hash change must change key hash";

    // Also verify equality-based comparison.
    const ShaderKey ka = make_key("x", "y", 3, 5);
    const ShaderKey kb = make_key("x", "y", 3, 5);
    EXPECT_EQ(ka, kb);
    EXPECT_EQ(hasher(ka), hasher(kb));
}

// ---------------------------------------------------------------------------
// T6 — put() return value: true=new, false=overwrite
// ---------------------------------------------------------------------------

TEST(ShaderCache, PutReturnValueNewVsOverwrite)
{
    ShaderCache cache;
    const ShaderKey key = make_key("ret_test", "main", 1);

    // First insertion — must return true.
    const bool first = cache.put(key, make_spirv(0xA), "main", 1);
    EXPECT_TRUE(first);

    // Second insertion (same key) — must return false.
    const bool second = cache.put(key, make_spirv(0xB), "main", 1);
    EXPECT_FALSE(second);

    EXPECT_EQ(cache.entry_count(), 1U);
}

// ---------------------------------------------------------------------------
// T7 — overwrite replaces SPIR-V; get() returns updated blob
// ---------------------------------------------------------------------------

TEST(ShaderCache, OverwriteReplacesSpirv)
{
    ShaderCache cache;
    const ShaderKey key = make_key("overwrite_key", "main", 1);

    const auto original  = make_spirv(0x1000, 4);
    const auto updated   = make_spirv(0x2000, 8);  // Different length too.

    cache.put(key, original, "main", 1);
    cache.put(key, updated,  "main", 1);  // Overwrite.

    const auto r = cache.get(key);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ((*r)->spirv, updated);
    EXPECT_NE((*r)->spirv, original);
}

// ---------------------------------------------------------------------------
// T8 — version-mismatch invalidation: a file with a different CDSC version in
//      the magic suffix is rejected, leaving any pre-existing entries intact.
// ---------------------------------------------------------------------------

TEST(ShaderCache, VersionMismatchRejectedAndDoesNotClobber)
{
    // Write a valid file first, then corrupt only the version bytes (offset 4-7)
    // of the 8-byte "CDSC\x00\x01\x00\x00" magic to a bumped major version.
    const auto tmp = std::filesystem::temp_directory_path()
                   / "cd_shader_cache_test_version_mismatch.bin";
    {
        ShaderCache       src;
        const ShaderKey   k = make_key("vm_src", "main", 1, 0);
        src.put(k, make_spirv(0x5A5A, 6), "main", 1);
        ASSERT_TRUE(src.save_to_disk(tmp));
    }

    // Flip the minor-version byte (offset 5) from 0x01 to 0x02 — a future format.
    {
        std::fstream f(tmp, std::ios::binary | std::ios::in | std::ios::out);
        ASSERT_TRUE(f.is_open());
        f.seekp(5, std::ios::beg);
        const char bumped = 0x02;
        f.write(&bumped, 1);
        ASSERT_TRUE(f.good());
    }

    // Target cache already holds one good entry — load must fail and NOT wipe it.
    ShaderCache       dst;
    const ShaderKey   existing = make_key("kept", "main", 1, 0);
    dst.put(existing, make_spirv(0x1234, 4), "main", 1);
    ASSERT_EQ(dst.entry_count(), 1U);

    EXPECT_FALSE(dst.load_from_disk(tmp))
        << "version-mismatched magic must be rejected";
    EXPECT_TRUE(dst.get(existing).has_value())
        << "rejected load must not clobber the pre-existing entry";

    std::filesystem::remove(tmp);
}

// ---------------------------------------------------------------------------
// T9 — corrupt / truncated header reject: garbage magic and a header that ends
//      before the entry-count both return false (no exception, no partial load).
// ---------------------------------------------------------------------------

TEST(ShaderCache, CorruptAndTruncatedHeaderRejected)
{
    // (a) Garbage 8-byte magic.
    {
        const auto tmp = std::filesystem::temp_directory_path()
                       / "cd_shader_cache_test_corrupt_magic.bin";
        {
            std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
            ASSERT_TRUE(f.is_open());
            const std::array<std::uint8_t, 8> garbage{
                'X', 'X', 'X', 'X', 0x00U, 0x00U, 0x00U, 0x00U
            };
            f.write(reinterpret_cast<const char*>(garbage.data()),
                    static_cast<std::streamsize>(garbage.size()));
        }

        ShaderCache cache;
        EXPECT_FALSE(cache.load_from_disk(tmp))
            << "garbage magic must be rejected";
        EXPECT_EQ(cache.entry_count(), 0U);
        std::filesystem::remove(tmp);
    }

    // (b) Correct magic but the file ends before the 4-byte entry count.
    {
        const auto tmp = std::filesystem::temp_directory_path()
                       / "cd_shader_cache_test_truncated_header.bin";
        {
            std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
            ASSERT_TRUE(f.is_open());
            const std::array<std::uint8_t, 8> magic{
                'C', 'D', 'S', 'C', 0x00U, 0x01U, 0x00U, 0x00U
            };
            f.write(reinterpret_cast<const char*>(magic.data()),
                    static_cast<std::streamsize>(magic.size()));
            // Only 2 of the 4 entry-count bytes follow — header is truncated.
            const std::array<std::uint8_t, 2> partial{ 0x01U, 0x00U };
            f.write(reinterpret_cast<const char*>(partial.data()),
                    static_cast<std::streamsize>(partial.size()));
        }

        ShaderCache cache;
        EXPECT_FALSE(cache.load_from_disk(tmp))
            << "header truncated before entry count must be rejected";
        EXPECT_EQ(cache.entry_count(), 0U);
        std::filesystem::remove(tmp);
    }

    // (c) Non-existent path — load must fail cleanly (no throw).
    {
        ShaderCache cache;
        const auto missing = std::filesystem::temp_directory_path()
                           / "cd_shader_cache_test_does_not_exist_q9.bin";
        std::filesystem::remove(missing);  // ensure absent
        EXPECT_FALSE(cache.load_from_disk(missing));
        EXPECT_EQ(cache.entry_count(), 0U);
    }
}

}  // namespace
