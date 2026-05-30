// =============================================================================
// CHROMODYNAMIC - tests/test_l10n.cpp
// Phase 501 - cd::game::l10n unit tests (G4.3).
//
// Covers the 8 brief contract requirements:
//
//   1. Load en + tr tables; get(key) returns the locale-specific string.
//   2. get_plural("apples", 1) = "1 apple"  in en.
//   3. get_plural("apples", 5) = "5 apples" in en.
//   4. Turkish plural always uses the "other" form regardless of n.
//   5. Missing key falls back to the base locale.
//   6. Missing in both = key returned (no crash).
//   7. is_rtl("ar") = true, is_rtl("en") = false.
//   8. set_locale broadcasts to observers.
//
// Plus extras documenting the secondary contracts:
//
//   9.  LoadResult::kFileNotFound for an absent path.
//   10. LoadResult::kEmptyLocale for an empty locale code.
//   11. CRLF + comment lines + blank lines parse cleanly.
//   12. ".other" fallback inside a single table.
//   13. RTL helper handles "ar-EG" / "HE" / "" properly.
//   14. remove_observer detaches; set_locale with same code is no-op.
// =============================================================================
#include <cd/game/l10n/L10n.hpp>

#include <gtest/gtest.h>

#include <atomic>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

namespace
{

using cd::game::l10n::is_rtl;
using cd::game::l10n::L10nManager;
using cd::game::l10n::LoadResult;
using cd::game::l10n::plural_rule_en;
using cd::game::l10n::plural_rule_tr;
using cd::game::l10n::PluralCategory;
using cd::game::l10n::StringTable;

constexpr const char* kEnSource =
    "# English\n"
    "greet=Hello\n"
    "farewell=Goodbye\n"
    "apples.one={n} apple\n"
    "apples.other={n} apples\n"
    "only_in_en=base only\n";

constexpr const char* kTrSource =
    "# Turkish\n"
    "greet=Merhaba\n"
    "farewell=Hosca kal\n"
    "apples.other={n} elma\n";

}  // namespace

// -----------------------------------------------------------------------------
// 1. Load en + tr tables; get(key) returns the locale-specific string.
// -----------------------------------------------------------------------------
TEST(L10n, LoadEnAndTrAndLookup)
{
    StringTable en;
    StringTable tr;
    ASSERT_EQ(en.load_from_string(kEnSource, "en"), LoadResult::kOk);
    ASSERT_EQ(tr.load_from_string(kTrSource, "tr"), LoadResult::kOk);

    L10nManager mgr;
    mgr.set_base_locale("en");
    mgr.add_locale(std::move(en));
    mgr.add_locale(std::move(tr));

    mgr.set_locale("en");
    EXPECT_EQ(mgr.get("greet"),    "Hello");
    EXPECT_EQ(mgr.get("farewell"), "Goodbye");

    mgr.set_locale("tr");
    EXPECT_EQ(mgr.get("greet"),    "Merhaba");
    EXPECT_EQ(mgr.get("farewell"), "Hosca kal");
}

// -----------------------------------------------------------------------------
// 2. get_plural("apples", 1) = "1 apple" in en.
// -----------------------------------------------------------------------------
TEST(L10n, EnPluralOne)
{
    StringTable en;
    ASSERT_EQ(en.load_from_string(kEnSource, "en"), LoadResult::kOk);

    L10nManager mgr;
    mgr.set_base_locale("en");
    mgr.add_locale(std::move(en));
    mgr.set_locale("en");

    EXPECT_EQ(mgr.get_plural("apples", 1), "1 apple");
}

// -----------------------------------------------------------------------------
// 3. get_plural("apples", 5) = "5 apples" in en.
// -----------------------------------------------------------------------------
TEST(L10n, EnPluralOther)
{
    StringTable en;
    ASSERT_EQ(en.load_from_string(kEnSource, "en"), LoadResult::kOk);

    L10nManager mgr;
    mgr.set_base_locale("en");
    mgr.add_locale(std::move(en));
    mgr.set_locale("en");

    EXPECT_EQ(mgr.get_plural("apples", 5), "5 apples");
    EXPECT_EQ(mgr.get_plural("apples", 0), "0 apples");   // 0 -> other in en
    EXPECT_EQ(mgr.get_plural("apples", 2), "2 apples");
}

// -----------------------------------------------------------------------------
// 4. Turkish plural always uses the "other" form regardless of n.
// -----------------------------------------------------------------------------
TEST(L10n, TrPluralAlwaysOther)
{
    // Sanity: the bare rule returns kOther for every n.
    EXPECT_EQ(plural_rule_tr(0),    PluralCategory::kOther);
    EXPECT_EQ(plural_rule_tr(1),    PluralCategory::kOther);
    EXPECT_EQ(plural_rule_tr(5),    PluralCategory::kOther);
    EXPECT_EQ(plural_rule_tr(1000), PluralCategory::kOther);

    StringTable tr;
    ASSERT_EQ(tr.load_from_string(kTrSource, "tr"), LoadResult::kOk);

    L10nManager mgr;
    mgr.set_base_locale("en");  // distinct base so we know tr is doing the work
    mgr.add_locale(std::move(tr));
    mgr.set_locale("tr");

    EXPECT_EQ(mgr.get_plural("apples", 1), "1 elma");
    EXPECT_EQ(mgr.get_plural("apples", 5), "5 elma");
    EXPECT_EQ(mgr.get_plural("apples", 0), "0 elma");
}

// -----------------------------------------------------------------------------
// 5. Missing key falls back to the base locale.
// -----------------------------------------------------------------------------
TEST(L10n, MissingKeyFallsBackToBase)
{
    StringTable en;
    StringTable tr;
    ASSERT_EQ(en.load_from_string(kEnSource, "en"), LoadResult::kOk);
    ASSERT_EQ(tr.load_from_string(kTrSource, "tr"), LoadResult::kOk);

    L10nManager mgr;
    mgr.set_base_locale("en");
    mgr.add_locale(std::move(en));
    mgr.add_locale(std::move(tr));
    mgr.set_locale("tr");

    // "only_in_en" is absent from tr; manager must walk to base "en".
    EXPECT_EQ(mgr.get("only_in_en"), "base only");
}

// -----------------------------------------------------------------------------
// 6. Missing in both = key returned (no crash).
// -----------------------------------------------------------------------------
TEST(L10n, MissingInBothReturnsKey)
{
    StringTable en;
    StringTable tr;
    ASSERT_EQ(en.load_from_string(kEnSource, "en"), LoadResult::kOk);
    ASSERT_EQ(tr.load_from_string(kTrSource, "tr"), LoadResult::kOk);

    L10nManager mgr;
    mgr.set_base_locale("en");
    mgr.add_locale(std::move(en));
    mgr.add_locale(std::move(tr));
    mgr.set_locale("tr");

    EXPECT_EQ(mgr.get("nope.such.key"),         "nope.such.key");
    EXPECT_EQ(mgr.get_plural("ghost_apples", 3), "ghost_apples");
}

// -----------------------------------------------------------------------------
// 7. is_rtl("ar") = true, is_rtl("en") = false.
// -----------------------------------------------------------------------------
TEST(L10n, RtlDetection)
{
    EXPECT_TRUE(is_rtl("ar"));
    EXPECT_TRUE(is_rtl("he"));
    EXPECT_TRUE(is_rtl("fa"));
    EXPECT_TRUE(is_rtl("ur"));

    EXPECT_FALSE(is_rtl("en"));
    EXPECT_FALSE(is_rtl("tr"));
    EXPECT_FALSE(is_rtl("de"));
    EXPECT_FALSE(is_rtl("ja"));
}

// -----------------------------------------------------------------------------
// 8. set_locale broadcasts to observers.
// -----------------------------------------------------------------------------
TEST(L10n, SetLocaleBroadcastsToObservers)
{
    L10nManager mgr;
    mgr.set_base_locale("en");

    std::vector<std::string> captured;
    const auto id = mgr.on_locale_change(
        [&captured](const std::string& code) { captured.push_back(code); });
    EXPECT_NE(id, 0U);
    EXPECT_EQ(mgr.observer_count(), 1U);

    mgr.set_locale("en");
    mgr.set_locale("tr");
    mgr.set_locale("tr");   // no-op (same code): must NOT fire again
    mgr.set_locale("ar");

    ASSERT_EQ(captured.size(), 3U);
    EXPECT_EQ(captured[0], "en");
    EXPECT_EQ(captured[1], "tr");
    EXPECT_EQ(captured[2], "ar");
}

// -----------------------------------------------------------------------------
// 9. LoadResult::kFileNotFound for an absent path.
// -----------------------------------------------------------------------------
TEST(L10n, LoadReportsFileNotFound)
{
    StringTable t;
    EXPECT_EQ(t.load("path/that/does/not/exist.lang", "en"),
              LoadResult::kFileNotFound);
    EXPECT_TRUE(t.empty());
    EXPECT_TRUE(t.locale_code().empty());
}

// -----------------------------------------------------------------------------
// 10. LoadResult::kEmptyLocale for an empty locale code.
// -----------------------------------------------------------------------------
TEST(L10n, LoadRejectsEmptyLocale)
{
    StringTable t;
    EXPECT_EQ(t.load_from_string("greet=Hi\n", ""), LoadResult::kEmptyLocale);
    EXPECT_TRUE(t.empty());
}

// -----------------------------------------------------------------------------
// 11. CRLF + comment lines + blank lines parse cleanly.
// -----------------------------------------------------------------------------
TEST(L10n, ParserHandlesCrlfCommentsBlanks)
{
    constexpr const char* kCrlf =
        "# top comment\r\n"
        "\r\n"
        "greet=Hello\r\n"
        "  spaced  =  value-after-eq\r\n"
        "url=https://example.com/?a=1&b=2\r\n";

    StringTable t;
    ASSERT_EQ(t.load_from_string(kCrlf, "en"), LoadResult::kOk);
    EXPECT_EQ(t.get("greet").value_or(""), "Hello");
    EXPECT_EQ(t.get("spaced").value_or(""), "  value-after-eq");
    EXPECT_EQ(t.get("url").value_or(""), "https://example.com/?a=1&b=2");
    EXPECT_EQ(t.size(), 3U);
}

// -----------------------------------------------------------------------------
// 12. ".other" fallback inside a single table: get_plural without an explicit
//     ".one" entry still resolves via ".other".
// -----------------------------------------------------------------------------
TEST(L10n, PluralOtherFallback)
{
    StringTable t;
    ASSERT_EQ(t.load_from_string("apples.other={n} apples\n", "en"),
              LoadResult::kOk);

    // n=1 -> kOne by rule, but only ".other" is present; the table must
    // fall back to ".other" and still produce a valid string.
    auto s = t.get_plural("apples", 1, &plural_rule_en);
    ASSERT_TRUE(s.has_value());
    EXPECT_EQ(*s, "1 apples");
}

// -----------------------------------------------------------------------------
// 13. RTL helper handles regional subtags, uppercase, and empty input.
// -----------------------------------------------------------------------------
TEST(L10n, RtlHandlesRegionAndCase)
{
    EXPECT_TRUE(is_rtl("ar-EG"));
    EXPECT_TRUE(is_rtl("ar_SA"));
    EXPECT_TRUE(is_rtl("HE"));        // case-insensitive
    EXPECT_TRUE(is_rtl("fa-IR"));
    EXPECT_FALSE(is_rtl(""));         // empty = not RTL
    EXPECT_FALSE(is_rtl("xx-YY"));    // unknown = not RTL
    EXPECT_FALSE(is_rtl("EN-US"));
}

// -----------------------------------------------------------------------------
// 14. remove_observer detaches; set_locale with the same code is a no-op.
// -----------------------------------------------------------------------------
TEST(L10n, RemoveObserverDetaches)
{
    L10nManager mgr;
    int fires = 0;
    const auto id = mgr.on_locale_change(
        [&fires](const std::string&) { ++fires; });
    EXPECT_EQ(mgr.observer_count(), 1U);

    mgr.set_locale("en");
    EXPECT_EQ(fires, 1);

    mgr.remove_observer(id);
    EXPECT_EQ(mgr.observer_count(), 0U);

    mgr.set_locale("tr");
    EXPECT_EQ(fires, 1);          // still 1: observer detached

    mgr.remove_observer(id);      // double-remove = no-op (no crash)
    EXPECT_EQ(mgr.observer_count(), 0U);
}

// -----------------------------------------------------------------------------
// Round-trip: load() from a real on-disk file with the same contract as
// load_from_string. Uses tmpnam to stay within the gtest sandbox.
// -----------------------------------------------------------------------------
TEST(L10n, LoadFromDiskRoundTrip)
{
    // GoogleTest provides a per-test temp directory via testing::TempDir().
    const std::string path = ::testing::TempDir() + "/cd_l10n_test.lang";
    {
        std::ofstream out(path, std::ios::out | std::ios::binary);
        ASSERT_TRUE(out.is_open());
        out << "# disk-loaded\n";
        out << "greet=Hi from disk\n";
        out << "apples.one={n} pomme\n";
        out << "apples.other={n} pommes\n";
    }

    StringTable t;
    ASSERT_EQ(t.load(path, "fr"), LoadResult::kOk);
    EXPECT_EQ(t.locale_code(), "fr");
    EXPECT_EQ(t.get("greet").value_or(""), "Hi from disk");

    auto p1 = t.get_plural("apples", 1, &plural_rule_en);
    auto p5 = t.get_plural("apples", 5, &plural_rule_en);
    ASSERT_TRUE(p1.has_value());
    ASSERT_TRUE(p5.has_value());
    EXPECT_EQ(*p1, "1 pomme");
    EXPECT_EQ(*p5, "5 pommes");

    std::remove(path.c_str());
}
