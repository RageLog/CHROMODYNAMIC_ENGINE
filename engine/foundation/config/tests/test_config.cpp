// =============================================================================
// CHROMODYNAMIC — cd::config tests (Sprint S2.8 + 100% gap-close)
// =============================================================================
#include <cd/config/Config.hpp>
#include <cd/core/CVar.hpp>
#include <cd/io/BinaryStream.hpp>
#include <gtest/gtest.h>

#include <climits>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>

namespace
{

TEST(ConfigSerialize, EmptyRegistryRoundTrip)
{
    cd::core::CVarRegistry empty;
    cd::io::BinaryWriter w;
    cd::config::save(w, empty);

    cd::core::CVarRegistry into;
    cd::io::BinaryReader r { w.data() };
    auto ok = cd::config::load(r, into);
    ASSERT_TRUE(ok.has_value());
    EXPECT_EQ(into.size(), 0u);
}

TEST(ConfigSerialize, AllFourTypesRoundTrip)
{
    cd::core::CVarRegistry src;
    src.set("audio.enabled", true);
    src.set("audio.samples", static_cast<std::int64_t>(44'100));
    src.set("camera.fov", 90.0);
    src.set("game.title", std::string { "CHROMODYNAMIC" });

    cd::io::BinaryWriter w;
    cd::config::save(w, src);

    cd::core::CVarRegistry dst;
    cd::io::BinaryReader r { w.data() };
    ASSERT_TRUE(cd::config::load(r, dst).has_value());

    EXPECT_EQ(dst.size(), 4u);
    EXPECT_EQ(*dst.get_as<bool>("audio.enabled"), true);
    EXPECT_EQ(*dst.get_as<std::int64_t>("audio.samples"), 44'100);
    EXPECT_DOUBLE_EQ(*dst.get_as<double>("camera.fov"), 90.0);
    EXPECT_EQ(*dst.get_as<std::string>("game.title"), "CHROMODYNAMIC");
}

TEST(ConfigSerialize, OutputIsCanonicalOrder)
{
    // Inserting in non-sorted order must still produce a byte-equal blob to
    // the one produced by sorted insertion (canonical-output guarantee).
    cd::core::CVarRegistry a;
    a.set("zeta", static_cast<std::int64_t>(3));
    a.set("alpha", static_cast<std::int64_t>(1));
    a.set("mu", static_cast<std::int64_t>(2));

    cd::core::CVarRegistry b;
    b.set("alpha", static_cast<std::int64_t>(1));
    b.set("mu", static_cast<std::int64_t>(2));
    b.set("zeta", static_cast<std::int64_t>(3));

    cd::io::BinaryWriter wa;
    cd::io::BinaryWriter wb;
    cd::config::save(wa, a);
    cd::config::save(wb, b);
    ASSERT_EQ(wa.data().size(), wb.data().size());
    EXPECT_EQ(std::memcmp(wa.data().data(), wb.data().data(), wa.data().size()), 0);
}

TEST(ConfigSerialize, OverwritesExistingKeys)
{
    cd::core::CVarRegistry src;
    src.set("speed", static_cast<std::int64_t>(99));

    cd::io::BinaryWriter w;
    cd::config::save(w, src);

    cd::core::CVarRegistry dst;
    dst.set("speed", static_cast<std::int64_t>(10));
    dst.set("untouched", static_cast<std::int64_t>(7));

    cd::io::BinaryReader r { w.data() };
    ASSERT_TRUE(cd::config::load(r, dst).has_value());
    EXPECT_EQ(*dst.get_as<std::int64_t>("speed"), 99);
    EXPECT_EQ(*dst.get_as<std::int64_t>("untouched"), 7);
}

TEST(ConfigDeserialize, BadMagicRejected)
{
    cd::io::BinaryWriter w;
    w.write<std::uint32_t>(0xDEADBEEFu);
    cd::core::CVarRegistry dst;
    cd::io::BinaryReader r { w.data() };
    auto res = cd::config::load(r, dst);
    ASSERT_FALSE(res.has_value());
    EXPECT_EQ(res.error().code, static_cast<std::uint32_t>(cd::config::config_errors::Code::kBadMagic));
}

TEST(ConfigDeserialize, BadVersionRejected)
{
    cd::io::BinaryWriter w;
    w.write<std::uint32_t>(cd::config::kMagic);
    w.write<std::uint32_t>(99u);
    cd::core::CVarRegistry dst;
    cd::io::BinaryReader r { w.data() };
    auto res = cd::config::load(r, dst);
    ASSERT_FALSE(res.has_value());
    EXPECT_EQ(res.error().code, static_cast<std::uint32_t>(cd::config::config_errors::Code::kBadVersion));
}

TEST(ConfigDeserialize, TruncatedRejected)
{
    cd::io::BinaryWriter w;
    w.write<std::uint32_t>(cd::config::kMagic);
    w.write<std::uint32_t>(cd::config::kVersion);
    // Count is missing — truncated.
    cd::core::CVarRegistry dst;
    cd::io::BinaryReader r { w.data() };
    auto res = cd::config::load(r, dst);
    ASSERT_FALSE(res.has_value());
    EXPECT_EQ(res.error().code, static_cast<std::uint32_t>(cd::config::config_errors::Code::kTruncated));
}

TEST(ConfigDeserialize, BadTypeTagRejected)
{
    cd::io::BinaryWriter w;
    w.write<std::uint32_t>(cd::config::kMagic);
    w.write<std::uint32_t>(cd::config::kVersion);
    w.write<std::uint32_t>(1u);    // 1 entry
    w.write_string("foo");
    w.write<std::uint8_t>(0xFFu);  // unknown tag
    cd::core::CVarRegistry dst;
    cd::io::BinaryReader r { w.data() };
    auto res = cd::config::load(r, dst);
    ASSERT_FALSE(res.has_value());
    EXPECT_EQ(res.error().code, static_cast<std::uint32_t>(cd::config::config_errors::Code::kBadTypeTag));
}

// ---------------------------------------------------------------------------
// Round-trip edge: bool false
// ---------------------------------------------------------------------------
TEST(ConfigSerialize, BoolFalseRoundTrip)
{
    cd::core::CVarRegistry src;
    src.set("flag.off", false);

    cd::io::BinaryWriter w;
    cd::config::save(w, src);

    cd::core::CVarRegistry dst;
    cd::io::BinaryReader r { w.data() };
    ASSERT_TRUE(cd::config::load(r, dst).has_value());
    ASSERT_TRUE(dst.get_as<bool>("flag.off").has_value());
    EXPECT_FALSE(*dst.get_as<bool>("flag.off"));
}

// ---------------------------------------------------------------------------
// Round-trip edge: int64 extremes (INT64_MIN, INT64_MAX, zero)
// ---------------------------------------------------------------------------
TEST(ConfigSerialize, Int64ExtremeValues)
{
    cd::core::CVarRegistry src;
    src.set("i64.min",  std::numeric_limits<std::int64_t>::min());
    src.set("i64.max",  std::numeric_limits<std::int64_t>::max());
    src.set("i64.zero", static_cast<std::int64_t>(0));

    cd::io::BinaryWriter w;
    cd::config::save(w, src);

    cd::core::CVarRegistry dst;
    cd::io::BinaryReader r { w.data() };
    ASSERT_TRUE(cd::config::load(r, dst).has_value());
    EXPECT_EQ(*dst.get_as<std::int64_t>("i64.min"),  std::numeric_limits<std::int64_t>::min());
    EXPECT_EQ(*dst.get_as<std::int64_t>("i64.max"),  std::numeric_limits<std::int64_t>::max());
    EXPECT_EQ(*dst.get_as<std::int64_t>("i64.zero"), static_cast<std::int64_t>(0));
}

// ---------------------------------------------------------------------------
// Round-trip edge: double special values (negative, very small, very large)
// ---------------------------------------------------------------------------
TEST(ConfigSerialize, DoubleEdgeValues)
{
    const double neg   = -1.5e-300;
    const double large = 1.7e308;
    const double zero  = 0.0;

    cd::core::CVarRegistry src;
    src.set("d.neg",   neg);
    src.set("d.large", large);
    src.set("d.zero",  zero);

    cd::io::BinaryWriter w;
    cd::config::save(w, src);

    cd::core::CVarRegistry dst;
    cd::io::BinaryReader r { w.data() };
    ASSERT_TRUE(cd::config::load(r, dst).has_value());
    EXPECT_DOUBLE_EQ(*dst.get_as<double>("d.neg"),   neg);
    EXPECT_DOUBLE_EQ(*dst.get_as<double>("d.large"), large);
    EXPECT_DOUBLE_EQ(*dst.get_as<double>("d.zero"),  zero);
}

// ---------------------------------------------------------------------------
// Round-trip edge: empty string value
// ---------------------------------------------------------------------------
TEST(ConfigSerialize, EmptyStringValue)
{
    cd::core::CVarRegistry src;
    src.set("str.empty", std::string {});

    cd::io::BinaryWriter w;
    cd::config::save(w, src);

    cd::core::CVarRegistry dst;
    cd::io::BinaryReader r { w.data() };
    ASSERT_TRUE(cd::config::load(r, dst).has_value());
    ASSERT_TRUE(dst.get_as<std::string>("str.empty").has_value());
    EXPECT_TRUE(dst.get_as<std::string>("str.empty")->empty());
}

// ---------------------------------------------------------------------------
// Round-trip edge: multi-byte / non-ASCII string value
// ---------------------------------------------------------------------------
TEST(ConfigSerialize, NonAsciiStringRoundTrip)
{
    // UTF-8 encoded "Ölçü" (Turkish) and an emoji sequence.
    const std::string utf8_val { "\xC3\x96l\xC3\xA7\xC3\xBC \xF0\x9F\x8E\xAE" };

    cd::core::CVarRegistry src;
    src.set("str.utf8", utf8_val);

    cd::io::BinaryWriter w;
    cd::config::save(w, src);

    cd::core::CVarRegistry dst;
    cd::io::BinaryReader r { w.data() };
    ASSERT_TRUE(cd::config::load(r, dst).has_value());
    EXPECT_EQ(*dst.get_as<std::string>("str.utf8"), utf8_val);
}

// ---------------------------------------------------------------------------
// Truncation: tag byte missing (key written, but no tag byte follows)
// ---------------------------------------------------------------------------
TEST(ConfigDeserialize, TruncatedAfterKey)
{
    cd::io::BinaryWriter w;
    w.write<std::uint32_t>(cd::config::kMagic);
    w.write<std::uint32_t>(cd::config::kVersion);
    w.write<std::uint32_t>(1u);
    w.write_string("mykey");  // key present — tag byte absent
    cd::core::CVarRegistry dst;
    cd::io::BinaryReader r { w.data() };
    auto res = cd::config::load(r, dst);
    ASSERT_FALSE(res.has_value());
    EXPECT_EQ(res.error().code, static_cast<std::uint32_t>(cd::config::config_errors::Code::kTruncated));
}

// ---------------------------------------------------------------------------
// Truncation: bool payload missing
// ---------------------------------------------------------------------------
TEST(ConfigDeserialize, TruncatedBoolPayload)
{
    cd::io::BinaryWriter w;
    w.write<std::uint32_t>(cd::config::kMagic);
    w.write<std::uint32_t>(cd::config::kVersion);
    w.write<std::uint32_t>(1u);
    w.write_string("flag");
    w.write<std::uint8_t>(static_cast<std::uint8_t>(cd::config::TypeTag::kBool));
    // bool payload (u8) absent
    cd::core::CVarRegistry dst;
    cd::io::BinaryReader r { w.data() };
    auto res = cd::config::load(r, dst);
    ASSERT_FALSE(res.has_value());
    EXPECT_EQ(res.error().code, static_cast<std::uint32_t>(cd::config::config_errors::Code::kTruncated));
}

// ---------------------------------------------------------------------------
// Truncation: int64 payload missing (only 4 of 8 bytes present)
// ---------------------------------------------------------------------------
TEST(ConfigDeserialize, TruncatedInt64Payload)
{
    cd::io::BinaryWriter w;
    w.write<std::uint32_t>(cd::config::kMagic);
    w.write<std::uint32_t>(cd::config::kVersion);
    w.write<std::uint32_t>(1u);
    w.write_string("num");
    w.write<std::uint8_t>(static_cast<std::uint8_t>(cd::config::TypeTag::kInt64));
    w.write<std::uint32_t>(0u);  // only 4 of 8 required bytes
    cd::core::CVarRegistry dst;
    cd::io::BinaryReader r { w.data() };
    auto res = cd::config::load(r, dst);
    ASSERT_FALSE(res.has_value());
    EXPECT_EQ(res.error().code, static_cast<std::uint32_t>(cd::config::config_errors::Code::kTruncated));
}

// ---------------------------------------------------------------------------
// Truncation: double payload missing
// ---------------------------------------------------------------------------
TEST(ConfigDeserialize, TruncatedDoublePayload)
{
    cd::io::BinaryWriter w;
    w.write<std::uint32_t>(cd::config::kMagic);
    w.write<std::uint32_t>(cd::config::kVersion);
    w.write<std::uint32_t>(1u);
    w.write_string("fov");
    w.write<std::uint8_t>(static_cast<std::uint8_t>(cd::config::TypeTag::kDouble));
    w.write<std::uint32_t>(0u);  // only 4 of 8 required bytes
    cd::core::CVarRegistry dst;
    cd::io::BinaryReader r { w.data() };
    auto res = cd::config::load(r, dst);
    ASSERT_FALSE(res.has_value());
    EXPECT_EQ(res.error().code, static_cast<std::uint32_t>(cd::config::config_errors::Code::kTruncated));
}

// ---------------------------------------------------------------------------
// Truncation: string length present but body truncated
// ---------------------------------------------------------------------------
TEST(ConfigDeserialize, TruncatedStringBody)
{
    cd::io::BinaryWriter w;
    w.write<std::uint32_t>(cd::config::kMagic);
    w.write<std::uint32_t>(cd::config::kVersion);
    w.write<std::uint32_t>(1u);
    w.write_string("title");
    w.write<std::uint8_t>(static_cast<std::uint8_t>(cd::config::TypeTag::kString));
    w.write<std::uint32_t>(20u);  // claims 20 bytes — none follow
    cd::core::CVarRegistry dst;
    cd::io::BinaryReader r { w.data() };
    auto res = cd::config::load(r, dst);
    ASSERT_FALSE(res.has_value());
    EXPECT_EQ(res.error().code, static_cast<std::uint32_t>(cd::config::config_errors::Code::kTruncated));
}

// ---------------------------------------------------------------------------
// Duplicate key in stream: last entry for a given key wins
// ---------------------------------------------------------------------------
TEST(ConfigDeserialize, DuplicateKeyLastWins)
{
    // Build stream manually with two entries for the same key.
    cd::io::BinaryWriter w;
    w.write<std::uint32_t>(cd::config::kMagic);
    w.write<std::uint32_t>(cd::config::kVersion);
    w.write<std::uint32_t>(2u);  // 2 entries, same key

    // Entry 1: speed = 10
    w.write_string("speed");
    w.write<std::uint8_t>(static_cast<std::uint8_t>(cd::config::TypeTag::kInt64));
    w.write<std::int64_t>(static_cast<std::int64_t>(10));

    // Entry 2: speed = 99 (should overwrite)
    w.write_string("speed");
    w.write<std::uint8_t>(static_cast<std::uint8_t>(cd::config::TypeTag::kInt64));
    w.write<std::int64_t>(static_cast<std::int64_t>(99));

    cd::core::CVarRegistry dst;
    cd::io::BinaryReader r { w.data() };
    ASSERT_TRUE(cd::config::load(r, dst).has_value());
    EXPECT_EQ(*dst.get_as<std::int64_t>("speed"), static_cast<std::int64_t>(99));
    // Registry de-duplication: size is implementation-defined (1 or 2 based on
    // unordered_map upsert semantics — just verify the value is correct).
}

// ---------------------------------------------------------------------------
// Large config: 100 int64 entries all round-trip correctly
// ---------------------------------------------------------------------------
TEST(ConfigSerialize, LargeConfigRoundTrip)
{
    cd::core::CVarRegistry src;
    for (std::int64_t i = 0; i < 100; ++i)
    {
        src.set("key." + std::to_string(i), i * i);
    }
    ASSERT_EQ(src.size(), 100u);

    cd::io::BinaryWriter w;
    cd::config::save(w, src);

    cd::core::CVarRegistry dst;
    cd::io::BinaryReader r { w.data() };
    ASSERT_TRUE(cd::config::load(r, dst).has_value());
    ASSERT_EQ(dst.size(), 100u);

    // Spot-check a selection of values.
    EXPECT_EQ(*dst.get_as<std::int64_t>("key.0"),  static_cast<std::int64_t>(0));
    EXPECT_EQ(*dst.get_as<std::int64_t>("key.7"),  static_cast<std::int64_t>(49));
    EXPECT_EQ(*dst.get_as<std::int64_t>("key.99"), static_cast<std::int64_t>(9801));
}

// ---------------------------------------------------------------------------
// Load into populated registry: only stream keys are overwritten; others kept
// (verifies merge semantics vs. replace-all)
// ---------------------------------------------------------------------------
TEST(ConfigSerialize, MergeDoesNotEraseUntouchedKeys)
{
    // Stream contains only "a".
    cd::core::CVarRegistry src;
    src.set("a", static_cast<std::int64_t>(1));

    cd::io::BinaryWriter w;
    cd::config::save(w, src);

    // Destination already has "b" and "c".
    cd::core::CVarRegistry dst;
    dst.set("a", static_cast<std::int64_t>(0));   // will be overwritten
    dst.set("b", static_cast<std::int64_t>(200));  // must survive
    dst.set("c", std::string { "hello" });          // must survive

    cd::io::BinaryReader r { w.data() };
    ASSERT_TRUE(cd::config::load(r, dst).has_value());

    EXPECT_EQ(*dst.get_as<std::int64_t>("a"), static_cast<std::int64_t>(1));
    EXPECT_EQ(*dst.get_as<std::int64_t>("b"), static_cast<std::int64_t>(200));
    EXPECT_EQ(*dst.get_as<std::string>("c"),  std::string { "hello" });
}

// ---------------------------------------------------------------------------
// Sorted-key invariant: blob bytes match regardless of write order (extension:
// verify the sorted order by checking the key that appears first in the blob)
// ---------------------------------------------------------------------------
TEST(ConfigSerialize, SortedKeyBytesLeadWithLexMin)
{
    cd::core::CVarRegistry src;
    src.set("zebra",  static_cast<std::int64_t>(3));
    src.set("apple",  static_cast<std::int64_t>(1));
    src.set("mango",  static_cast<std::int64_t>(2));

    cd::io::BinaryWriter w;
    cd::config::save(w, src);

    // The first key in the serialised blob (after magic+version+count=12 bytes)
    // must be "apple" (lexicographic minimum).
    const auto blob = w.data();
    // Bytes [12..15] = key_len of first entry (little-endian u32).
    ASSERT_GE(blob.size(), 16u);
    std::uint32_t first_key_len = 0;
    std::memcpy(&first_key_len, blob.data() + 12, sizeof(std::uint32_t));
    // On LE host this is already correct; we compare the raw bytes.
    // key_len for "apple" = 5.
    EXPECT_EQ(first_key_len, 5u);
    const std::string first_key(
        reinterpret_cast<const char*>(blob.data() + 16),
        static_cast<std::size_t>(first_key_len)
    );
    EXPECT_EQ(first_key, "apple");
}

}  // namespace
