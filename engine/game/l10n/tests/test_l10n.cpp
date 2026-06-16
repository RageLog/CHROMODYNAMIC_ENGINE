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

// -----------------------------------------------------------------------------
// 15. Plural edge: a negative count is a valid integer for {n} substitution and
//     routes through the rule's "other" category (n != 1). Locks the
//     substitute_count + plural_rule_en behaviour for n < 0, which prior tests
//     never exercised (they only used 0/1/2/5).
// -----------------------------------------------------------------------------
TEST(L10n, PluralNegativeCountSubstitutes)
{
    // plural_rule_en: anything != 1 -> kOther (including negatives).
    EXPECT_EQ(plural_rule_en(-1), PluralCategory::kOther);
    EXPECT_EQ(plural_rule_en(-5), PluralCategory::kOther);

    StringTable t;
    ASSERT_EQ(t.load_from_string("debt.one={n} coin\ndebt.other={n} coins\n", "en"),
              LoadResult::kOk);

    auto neg = t.get_plural("debt", -3, &plural_rule_en);
    ASSERT_TRUE(neg.has_value());
    EXPECT_EQ(*neg, "-3 coins");  // negative routes to .other, "{n}" -> "-3"
}

// -----------------------------------------------------------------------------
// 16. Plural edge: multiple "{n}" placeholders in a single template are ALL
//     substituted (one-pass replace), and a "{n}" at the very end of the
//     string substitutes correctly (off-by-one boundary in substitute_count).
// -----------------------------------------------------------------------------
TEST(L10n, PluralMultipleAndTrailingPlaceholders)
{
    StringTable t;
    ASSERT_EQ(t.load_from_string(
                  "score.other={n} of {n} (total: {n})\ncount.other=items: {n}\n",
                  "en"),
              LoadResult::kOk);

    auto multi = t.get_plural("score", 7, &plural_rule_en);
    ASSERT_TRUE(multi.has_value());
    EXPECT_EQ(*multi, "7 of 7 (total: 7)");  // all three {n} replaced

    auto trailing = t.get_plural("count", 42, &plural_rule_en);
    ASSERT_TRUE(trailing.has_value());
    EXPECT_EQ(*trailing, "items: 42");  // trailing {n} at end-of-string
}

// -----------------------------------------------------------------------------
// 17. Plural edge: a literal token that is NOT exactly "{n}" is left verbatim
//     (an unterminated "{n" or a "{m}" placeholder is untouched — the
//     substitution is intentionally a single literal "{n}" match, no parser).
// -----------------------------------------------------------------------------
TEST(L10n, PluralLeavesNonMatchingTokensVerbatim)
{
    StringTable t;
    ASSERT_EQ(t.load_from_string("raw.other={m} and {n and {n}\n", "en"),
              LoadResult::kOk);

    auto s = t.get_plural("raw", 9, &plural_rule_en);
    ASSERT_TRUE(s.has_value());
    // "{m}" and the unterminated "{n " are verbatim; only the final "{n}" fires.
    EXPECT_EQ(*s, "{m} and {n and 9");
}

// -----------------------------------------------------------------------------
// 18. plural_rule_for returns the safe neutral default (one/other, == en rule)
//     for an unknown locale and never returns nullptr. Locks the manager-side
//     plural-rule fallback contract (header: "never returns nullptr").
// -----------------------------------------------------------------------------
TEST(L10n, PluralRuleForUnknownLocaleFallsBackToEn)
{
    L10nManager mgr;
    // Pre-baked rules are "en" and "tr"; an unregistered locale must fall back.
    EXPECT_EQ(mgr.plural_rule_for("xx"), &plural_rule_en);
    EXPECT_EQ(mgr.plural_rule_for(""),   &plural_rule_en);
    // Registered locales return their own rule.
    EXPECT_EQ(mgr.plural_rule_for("tr"), &plural_rule_tr);
    EXPECT_EQ(mgr.plural_rule_for("en"), &plural_rule_en);

    // A locale whose rule was explicitly set to nullptr also falls back (the
    // header promises plural_rule_for never returns nullptr).
    mgr.set_plural_rule("zz", nullptr);
    EXPECT_EQ(mgr.plural_rule_for("zz"), &plural_rule_en);
}
