// =============================================================================
// CHROMODYNAMIC — test_asset_pak.cpp
// =============================================================================
#include <cd/asset/pak/Pak.hpp>

#include <gtest/gtest.h>

#include <cstring>
#include <string_view>
#include <vector>

namespace
{

std::vector<std::byte> as_bytes(std::string_view s)
{
    std::vector<std::byte> b(s.size());
    if (!s.empty())
        std::memcpy(b.data(), s.data(), s.size());
    return b;
}

}  // namespace

TEST(AssetPak, EmptyBundleRoundTrip)
{
    cd::asset::pak::PakBuilder b;
    auto bytes = b.build();
    EXPECT_GE(bytes.size(), cd::asset::pak::kPakHeaderSize);

    cd::asset::pak::Pak pak;
    auto r = pak.open(bytes.data(), bytes.size());
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(pak.entry_count(), 0u);
}

TEST(AssetPak, SingleEntryRoundTrip)
{
    cd::asset::pak::PakBuilder b;
    b.add("hello.txt", "Hello, world!");
    auto bytes = b.build();

    cd::asset::pak::Pak pak;
    ASSERT_TRUE(pak.open(bytes.data(), bytes.size()).has_value());
    EXPECT_EQ(pak.entry_count(), 1u);
    auto blob = pak.find("hello.txt");
    ASSERT_TRUE(blob.has_value());
    EXPECT_EQ(blob->size, 13u);
    EXPECT_EQ(std::memcmp(blob->bytes, "Hello, world!", 13), 0);
}

TEST(AssetPak, MultipleEntriesAccessible)
{
    cd::asset::pak::PakBuilder b;
    b.add("textures/wall.png", as_bytes(std::string(256, 'a')));
    b.add("audio/explosion.wav", as_bytes(std::string(1024, 'b')));
    b.add("data/config.json", as_bytes("{\"key\":42}"));
    auto bytes = b.build();

    cd::asset::pak::Pak pak;
    ASSERT_TRUE(pak.open(bytes.data(), bytes.size()).has_value());
    EXPECT_EQ(pak.entry_count(), 3u);

    auto t = pak.find("textures/wall.png");
    auto a = pak.find("audio/explosion.wav");
    auto c = pak.find("data/config.json");
    ASSERT_TRUE(t && a && c);
    EXPECT_EQ(t->size, 256u);
    EXPECT_EQ(a->size, 1024u);
    EXPECT_EQ(c->size, 10u);
    EXPECT_EQ(static_cast<char>(t->bytes[0]), 'a');
    EXPECT_EQ(static_cast<char>(a->bytes[0]), 'b');
    EXPECT_EQ(static_cast<char>(c->bytes[0]), '{');
}

TEST(AssetPak, MissingEntryReturnsNullopt)
{
    cd::asset::pak::PakBuilder b;
    b.add("only.txt", "x");
    auto bytes = b.build();
    cd::asset::pak::Pak pak;
    ASSERT_TRUE(pak.open(bytes.data(), bytes.size()).has_value());
    EXPECT_FALSE(pak.find("missing.txt").has_value());
}

TEST(AssetPak, BadMagicRejected)
{
    std::vector<std::byte> tiny(40, std::byte { 0 });
    cd::asset::pak::Pak pak;
    auto r = pak.open(tiny.data(), tiny.size());
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code,
              static_cast<std::uint32_t>(cd::asset::pak::pak_errors::Code::kMagicMismatch));
}

TEST(AssetPak, ForEachIteratesAllEntries)
{
    cd::asset::pak::PakBuilder b;
    b.add("a", "1");
    b.add("b", "22");
    b.add("c", "333");
    auto bytes = b.build();
    cd::asset::pak::Pak pak;
    ASSERT_TRUE(pak.open(bytes.data(), bytes.size()).has_value());
    std::size_t total_bytes = 0;
    int names_seen = 0;
    pak.for_each([&](std::string_view name, cd::asset::pak::PakBlobView blob) {
        ++names_seen;
        total_bytes += blob.size;
        (void)name;
    });
    EXPECT_EQ(names_seen, 3);
    EXPECT_EQ(total_bytes, 1u + 2u + 3u);
}

// =============================================================================
// Robustness / edge / negative coverage (≥80→100 marathon, ADD-ONLY).
// Defensive deserialization: a corrupt header/TOC must yield a typed error,
// never an out-of-bounds read.
// =============================================================================

namespace
{
using Code = cd::asset::pak::pak_errors::Code;

void patch_u32_le(std::vector<std::byte>& v, std::size_t off, std::uint32_t x)
{
    v[off + 0] = std::byte { static_cast<unsigned char>(x & 0xFFu) };
    v[off + 1] = std::byte { static_cast<unsigned char>((x >> 8u) & 0xFFu) };
    v[off + 2] = std::byte { static_cast<unsigned char>((x >> 16u) & 0xFFu) };
    v[off + 3] = std::byte { static_cast<unsigned char>((x >> 24u) & 0xFFu) };
}

void patch_u64_le(std::vector<std::byte>& v, std::size_t off, std::uint64_t x)
{
    patch_u32_le(v, off, static_cast<std::uint32_t>(x & 0xFFFFFFFFull));
    patch_u32_le(v, off + 4, static_cast<std::uint32_t>((x >> 32u) & 0xFFFFFFFFull));
}
}  // namespace

TEST(AssetPakEdge, BufferTooSmallReturnsCorrupt)
{
    std::vector<std::byte> tiny(10, std::byte { 0 });
    cd::asset::pak::Pak pak;
    auto r = pak.open(tiny.data(), tiny.size());
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, static_cast<std::uint32_t>(Code::kCorrupt));
}

TEST(AssetPakEdge, NullBufferReturnsCorrupt)
{
    cd::asset::pak::Pak pak;
    auto r = pak.open(nullptr, 1024);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, static_cast<std::uint32_t>(Code::kCorrupt));
}

TEST(AssetPakEdge, BadVersionReturnsVersionMismatch)
{
    cd::asset::pak::PakBuilder b;
    b.add("x", "y");
    auto bytes = b.build();
    // version field at offset 4.
    patch_u32_le(bytes, 4, cd::asset::pak::kPakVersion + 99u);
    cd::asset::pak::Pak pak;
    auto r = pak.open(bytes.data(), bytes.size());
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, static_cast<std::uint32_t>(Code::kVersionMismatch));
}

TEST(AssetPakEdge, TocOffsetPastBufferReturnsCorrupt)
{
    cd::asset::pak::PakBuilder b;
    b.add("x", "y");
    auto bytes = b.build();
    // toc_offset (u64) at offset 12.
    patch_u64_le(bytes, 12, static_cast<std::uint64_t>(bytes.size()) + 1000u);
    cd::asset::pak::Pak pak;
    auto r = pak.open(bytes.data(), bytes.size());
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, static_cast<std::uint32_t>(Code::kCorrupt));
}

TEST(AssetPakEdge, TruncatedTocReturnsCorrupt)
{
    cd::asset::pak::PakBuilder b;
    b.add("hello.txt", "data");
    auto bytes = b.build();
    // Point the TOC at the very last byte: reading the u16 name_len needs 2
    // bytes but only 1 remains → "truncated TOC".
    patch_u64_le(bytes, 12, static_cast<std::uint64_t>(bytes.size()) - 1u);
    cd::asset::pak::Pak pak;
    auto r = pak.open(bytes.data(), bytes.size());
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, static_cast<std::uint32_t>(Code::kCorrupt));
}

TEST(AssetPakEdge, EntryClaimingHugeNameLenReturnsCorrupt)
{
    cd::asset::pak::PakBuilder b;
    b.add("hello.txt", "data");
    auto bytes = b.build();
    const auto toc = cd::asset::pak::pak_detail::read_u64(bytes.data() + 12);
    // Overwrite the first TOC entry's name_len (u16 at toc start) with a value
    // that overruns the buffer → "truncated entry".
    bytes[static_cast<std::size_t>(toc) + 0] = std::byte { 0xFF };
    bytes[static_cast<std::size_t>(toc) + 1] = std::byte { 0xFF };
    cd::asset::pak::Pak pak;
    auto r = pak.open(bytes.data(), bytes.size());
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, static_cast<std::uint32_t>(Code::kCorrupt));
}

TEST(AssetPakEdge, BlobOffsetPlusSizePastBufferReturnsCorrupt)
{
    cd::asset::pak::PakBuilder b;
    b.add("hello.txt", "data");
    auto bytes = b.build();
    const auto toc = static_cast<std::size_t>(cd::asset::pak::pak_detail::read_u64(bytes.data() + 12));
    // TOC entry layout: u16 name_len, name bytes, u64 offset, u64 size.
    const auto name_len = cd::asset::pak::pak_detail::read_u16(bytes.data() + toc);
    const std::size_t size_field = toc + 2 + name_len + 8;  // skip name_len, name, offset
    patch_u64_le(bytes, size_field, 0xFFFFFFFFull);          // absurd blob size
    cd::asset::pak::Pak pak;
    auto r = pak.open(bytes.data(), bytes.size());
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, static_cast<std::uint32_t>(Code::kCorrupt));
}

TEST(AssetPakEdge, DuplicateKeyLastWins)
{
    // Two entries with the same name: the reader's unordered_map keeps the
    // last-inserted offset/size. Distinct payload lengths let us tell which won.
    cd::asset::pak::PakBuilder b;
    b.add("dup", as_bytes(std::string(4, 'A')));
    b.add("dup", as_bytes(std::string(9, 'B')));
    auto bytes = b.build();
    cd::asset::pak::Pak pak;
    ASSERT_TRUE(pak.open(bytes.data(), bytes.size()).has_value());
    EXPECT_EQ(pak.entry_count(), 1u);
    auto blob = pak.find("dup");
    ASSERT_TRUE(blob.has_value());
    EXPECT_EQ(blob->size, 9u);
    EXPECT_EQ(static_cast<char>(blob->bytes[0]), 'B');
}

TEST(AssetPakEdge, OverlongNameYieldsEmptyBuild)
{
    cd::asset::pak::PakBuilder b;
    b.add(std::string(65536, 'z'), as_bytes("payload"));  // 1 over the u16 limit
    auto bytes = b.build();
    EXPECT_TRUE(bytes.empty());
}

TEST(AssetPakEdge, EmptyPayloadEntryRoundTrips)
{
    cd::asset::pak::PakBuilder b;
    b.add("zero.bin", std::vector<std::byte> {});
    b.add("after.txt", as_bytes("tail"));
    auto bytes = b.build();
    cd::asset::pak::Pak pak;
    ASSERT_TRUE(pak.open(bytes.data(), bytes.size()).has_value());
    auto z = pak.find("zero.bin");
    ASSERT_TRUE(z.has_value());
    EXPECT_EQ(z->size, 0u);
    auto a = pak.find("after.txt");
    ASSERT_TRUE(a.has_value());
    EXPECT_EQ(a->size, 4u);
}

TEST(AssetPakEdge, EmptyNameEntryRoundTrips)
{
    // The wire format allows a zero-length name (name_len == 0).
    cd::asset::pak::PakBuilder b;
    b.add("", as_bytes("body"));
    auto bytes = b.build();
    cd::asset::pak::Pak pak;
    ASSERT_TRUE(pak.open(bytes.data(), bytes.size()).has_value());
    auto blob = pak.find("");
    ASSERT_TRUE(blob.has_value());
    EXPECT_EQ(blob->size, 4u);
}
