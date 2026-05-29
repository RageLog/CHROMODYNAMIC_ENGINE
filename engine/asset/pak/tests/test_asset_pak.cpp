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
