// =============================================================================
// CHROMODYNAMIC — cd::config tests (Sprint S2.8)
// =============================================================================
#include <cd/config/Config.hpp>
#include <cd/core/CVar.hpp>
#include <cd/io/BinaryStream.hpp>
#include <gtest/gtest.h>

#include <cstdint>
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

}  // namespace
