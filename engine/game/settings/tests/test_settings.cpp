// =============================================================================
// CHROMODYNAMIC — tests/test_settings.cpp
// Phase 471 — cd::game::settings::Settings unit tests.
//
// Coverage (per task brief, plus a few defence-in-depth checks):
//   1. set/get roundtrip — bool
//   2. set/get roundtrip — int
//   3. set/get roundtrip — float
//   4. set/get roundtrip — std::string
//   5. save/load roundtrip preserves typed values
//   6. on_change fires when value changes
//   7. on_change does NOT fire when set() is called with the same value
//   8. Multiple observers on one key — all fire, in registration order
//   9. Missing key — get<T>() returns nullopt
//  10. Type mismatch — get<T>() returns nullopt when stored type != T
//  11. Comment lines + blanks preserved across save/load roundtrip
//  12. Re-loading from disk fires observers (live broadcast on apply)
// =============================================================================
#include <cd/game/settings/Settings.hpp>

#include <gtest/gtest.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace
{

using cd::game::settings::ChangeCallback;
using cd::game::settings::Settings;
using cd::game::settings::Value;

// -----------------------------------------------------------------------------
// Test fixture: gives each test a unique temp file path under the
// platform temp dir + cleans it up on teardown. Keeps the tests
// hermetic so they can be re-run in parallel without colliding.
// -----------------------------------------------------------------------------
class SettingsFile : public ::testing::Test
{
protected:
    void SetUp() override
    {
        const auto base = std::filesystem::temp_directory_path();
        const auto name = "cd_game_settings_test_"
                          + std::to_string(::testing::UnitTest::GetInstance()
                                              ->current_test_info()
                                              ->name()[0])
                          + "_"
                          + std::to_string(reinterpret_cast<std::uintptr_t>(this))
                          + ".ini";
        path_ = (base / name).string();
        std::remove(path_.c_str());
    }

    void TearDown() override
    {
        std::remove(path_.c_str());
    }

    std::string path_;
};

// -----------------------------------------------------------------------------
// 1) set/get roundtrip — bool.
// -----------------------------------------------------------------------------
TEST(Settings, RoundtripBool)
{
    Settings s;
    s.set<bool>("audio.enabled", true);
    const auto v = s.get<bool>("audio.enabled");
    ASSERT_TRUE(v.has_value());
    EXPECT_TRUE(*v);

    s.set<bool>("audio.enabled", false);
    EXPECT_FALSE(*s.get<bool>("audio.enabled"));
}

// -----------------------------------------------------------------------------
// 2) set/get roundtrip — int.
// -----------------------------------------------------------------------------
TEST(Settings, RoundtripInt)
{
    Settings s;
    s.set<int>("graphics.shadow_cascades", 4);
    const auto v = s.get<int>("graphics.shadow_cascades");
    ASSERT_TRUE(v.has_value());
    EXPECT_EQ(*v, 4);

    s.set<int>("graphics.shadow_cascades", -7);
    EXPECT_EQ(*s.get<int>("graphics.shadow_cascades"), -7);
}

// -----------------------------------------------------------------------------
// 3) set/get roundtrip — float.
// -----------------------------------------------------------------------------
TEST(Settings, RoundtripFloat)
{
    Settings s;
    s.set<float>("audio.master_volume", 0.75F);
    const auto v = s.get<float>("audio.master_volume");
    ASSERT_TRUE(v.has_value());
    EXPECT_FLOAT_EQ(*v, 0.75F);
}

// -----------------------------------------------------------------------------
// 4) set/get roundtrip — std::string.
// -----------------------------------------------------------------------------
TEST(Settings, RoundtripString)
{
    Settings s;
    s.set<std::string>("user.locale", std::string{ "en-GB" });
    const auto v = s.get<std::string>("user.locale");
    ASSERT_TRUE(v.has_value());
    EXPECT_EQ(*v, "en-GB");
}

// -----------------------------------------------------------------------------
// 5) save / load roundtrip preserves typed values across instances.
// -----------------------------------------------------------------------------
TEST_F(SettingsFile, SaveLoadRoundtripPreservesTypes)
{
    {
        Settings out;
        out.set<bool>       ("audio.enabled",          true);
        out.set<int>        ("graphics.cascades",      4);
        out.set<float>      ("audio.master_volume",    0.5F);
        out.set<std::string>("user.locale",            std::string{ "tr-TR" });
        ASSERT_TRUE(out.save(path_));
    }

    Settings in;
    ASSERT_TRUE(in.load(path_));

    const auto b = in.get<bool>        ("audio.enabled");
    const auto i = in.get<int>         ("graphics.cascades");
    const auto f = in.get<float>       ("audio.master_volume");
    const auto s = in.get<std::string> ("user.locale");

    ASSERT_TRUE(b.has_value()); EXPECT_TRUE(*b);
    ASSERT_TRUE(i.has_value()); EXPECT_EQ   (*i, 4);
    ASSERT_TRUE(f.has_value()); EXPECT_FLOAT_EQ(*f, 0.5F);
    ASSERT_TRUE(s.has_value()); EXPECT_EQ   (*s, "tr-TR");
}

// -----------------------------------------------------------------------------
// 6) on_change fires when the value actually changes.
// -----------------------------------------------------------------------------
TEST(Settings, OnChangeFiresOnValueChange)
{
    Settings s;
    int fires = 0;
    Value last;
    s.on_change("graphics.cascades", [&](const Value& v) {
        ++fires;
        last = v;
    });

    s.set<int>("graphics.cascades", 2);
    EXPECT_EQ(fires, 1);
    EXPECT_EQ(std::get<int>(last), 2);

    s.set<int>("graphics.cascades", 4);
    EXPECT_EQ(fires, 2);
    EXPECT_EQ(std::get<int>(last), 4);
}

// -----------------------------------------------------------------------------
// 7) on_change does NOT fire when set() is called with the same value.
// -----------------------------------------------------------------------------
TEST(Settings, OnChangeSilentWhenValueUnchanged)
{
    Settings s;
    int fires = 0;
    s.on_change("audio.master_volume", [&](const Value&) { ++fires; });

    s.set<float>("audio.master_volume", 0.5F);
    EXPECT_EQ(fires, 1);

    // Repeated set with identical value: must NOT broadcast.
    s.set<float>("audio.master_volume", 0.5F);
    s.set<float>("audio.master_volume", 0.5F);
    s.set<float>("audio.master_volume", 0.5F);
    EXPECT_EQ(fires, 1);

    // Real change wakes it up again.
    s.set<float>("audio.master_volume", 0.6F);
    EXPECT_EQ(fires, 2);
}

// -----------------------------------------------------------------------------
// 8) Multiple observers on one key — all fire, in registration order.
// -----------------------------------------------------------------------------
TEST(Settings, MultipleObserversPerKeyAllFire)
{
    Settings s;
    std::vector<int> order;

    s.on_change("graphics.cascades", [&](const Value&) { order.push_back(1); });
    s.on_change("graphics.cascades", [&](const Value&) { order.push_back(2); });
    s.on_change("graphics.cascades", [&](const Value&) { order.push_back(3); });

    EXPECT_EQ(s.observer_count("graphics.cascades"), 3U);

    s.set<int>("graphics.cascades", 4);
    ASSERT_EQ(order.size(), 3U);
    EXPECT_EQ(order[0], 1);
    EXPECT_EQ(order[1], 2);
    EXPECT_EQ(order[2], 3);
}

// -----------------------------------------------------------------------------
// 9) Missing key — get<T>() returns nullopt for every T.
// -----------------------------------------------------------------------------
TEST(Settings, MissingKeyReturnsNullopt)
{
    Settings s;
    EXPECT_FALSE(s.get<bool>       ("nope").has_value());
    EXPECT_FALSE(s.get<int>        ("nope").has_value());
    EXPECT_FALSE(s.get<float>      ("nope").has_value());
    EXPECT_FALSE(s.get<std::string>("nope").has_value());
    EXPECT_FALSE(s.has_key("nope"));
    EXPECT_EQ   (s.key_count(), 0U);
}

// -----------------------------------------------------------------------------
// 10) Type mismatch — get<T>() returns nullopt when stored type != T.
//     (Numeric coercion is intentionally NOT done; see header rationale.)
// -----------------------------------------------------------------------------
TEST(Settings, TypeMismatchReturnsNullopt)
{
    Settings s;
    s.set<int>("graphics.cascades", 4);

    EXPECT_TRUE (s.get<int>        ("graphics.cascades").has_value());
    EXPECT_FALSE(s.get<bool>       ("graphics.cascades").has_value());
    EXPECT_FALSE(s.get<float>      ("graphics.cascades").has_value());
    EXPECT_FALSE(s.get<std::string>("graphics.cascades").has_value());

    s.set<std::string>("user.locale", std::string{ "en" });
    EXPECT_FALSE(s.get<int>("user.locale").has_value());
}

// -----------------------------------------------------------------------------
// 11) Comment lines + blank lines preserved across save/load roundtrip.
// -----------------------------------------------------------------------------
TEST_F(SettingsFile, CommentsAndBlanksPreservedOnSave)
{
    // Hand-author a config file with comments + blanks + values, then
    // verify that load() preserves the layout and save() emits it back.
    {
        std::ofstream f{ path_, std::ios::binary };
        ASSERT_TRUE(f.good());
        f << "# CHROMODYNAMIC settings — hand-authored\n";
        f << "# audio section\n";
        f << "\n";
        f << "audio.enabled=true\n";
        f << "audio.master_volume=0.5\n";
        f << "\n";
        f << "# graphics section\n";
        f << "graphics.cascades=4\n";
    }

    Settings s;
    ASSERT_TRUE(s.load(path_));
    EXPECT_EQ(s.key_count(), 3U);

    // Mutate one value; save should rewrite ONLY that line's value
    // while preserving every comment / blank / key in its original
    // position.
    s.set<int>("graphics.cascades", 8);
    ASSERT_TRUE(s.save(path_));

    std::ifstream in{ path_, std::ios::binary };
    std::ostringstream ss;
    ss << in.rdbuf();
    const auto content = ss.str();

    // Comments preserved verbatim.
    EXPECT_NE(content.find("# CHROMODYNAMIC settings"), std::string::npos);
    EXPECT_NE(content.find("# audio section"),           std::string::npos);
    EXPECT_NE(content.find("# graphics section"),        std::string::npos);

    // Updated value present.
    EXPECT_NE(content.find("graphics.cascades=8"), std::string::npos);

    // Blank lines preserved — at least two "\n\n" sequences from the
    // pair of blanks in the authored file.
    auto first  = content.find("\n\n");
    ASSERT_NE(first, std::string::npos);
    EXPECT_NE(content.find("\n\n", first + 1), std::string::npos);

    // Comment-then-data ordering preserved: "audio section" comment
    // must precede "audio.enabled=" line.
    const auto pos_audio_cmt = content.find("# audio section");
    const auto pos_audio_key = content.find("audio.enabled=");
    ASSERT_NE(pos_audio_cmt, std::string::npos);
    ASSERT_NE(pos_audio_key, std::string::npos);
    EXPECT_LT(pos_audio_cmt, pos_audio_key);
}

// -----------------------------------------------------------------------------
// 12) load() fires observers as values are applied — this is the
//     "settings file changed on disk" path and matches the
//     live-broadcast contract.
// -----------------------------------------------------------------------------
TEST_F(SettingsFile, LoadFiresObserversForAppliedValues)
{
    {
        std::ofstream f{ path_, std::ios::binary };
        ASSERT_TRUE(f.good());
        f << "graphics.cascades=4\n";
        f << "audio.master_volume=0.75\n";
    }

    Settings s;
    int      fires_cascades = 0;
    int      fires_volume   = 0;
    s.on_change("graphics.cascades",   [&](const Value&) { ++fires_cascades; });
    s.on_change("audio.master_volume", [&](const Value&) { ++fires_volume; });

    ASSERT_TRUE(s.load(path_));

    EXPECT_EQ(fires_cascades, 1);
    EXPECT_EQ(fires_volume,   1);

    const auto cas = s.get<int>("graphics.cascades");
    const auto vol = s.get<float>("audio.master_volume");
    ASSERT_TRUE(cas.has_value());
    ASSERT_TRUE(vol.has_value());
    EXPECT_EQ(*cas, 4);
    EXPECT_FLOAT_EQ(*vol, 0.75F);
}

}  // namespace
