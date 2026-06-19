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
using cd::game::l10n::plural_rule_ar;
using cd::game::l10n::plural_rule_en;
using cd::game::l10n::plural_rule_ru;
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

// -----------------------------------------------------------------------------
// 19. Arabic CLDR rule (plural_rule_ar) — all six categories.
//     CLDR v45 boundaries: 0->zero, 1->one, 2->two, 3-10->few, 11-99->many,
//     everything else (100, 200, …) -> other. Negative n -> other.
// -----------------------------------------------------------------------------
TEST(L10n, ArabicPluralRuleAllSixCategories)
{
    // Arrange / Act / Assert — category boundaries per CLDR v45 plurals.xml.
    EXPECT_EQ(plural_rule_ar(0),   PluralCategory::kZero);
    EXPECT_EQ(plural_rule_ar(1),   PluralCategory::kOne);
    EXPECT_EQ(plural_rule_ar(2),   PluralCategory::kTwo);
    EXPECT_EQ(plural_rule_ar(3),   PluralCategory::kFew);
    EXPECT_EQ(plural_rule_ar(10),  PluralCategory::kFew);
    EXPECT_EQ(plural_rule_ar(11),  PluralCategory::kMany);
    EXPECT_EQ(plural_rule_ar(99),  PluralCategory::kMany);
    EXPECT_EQ(plural_rule_ar(100), PluralCategory::kOther);
    EXPECT_EQ(plural_rule_ar(101), PluralCategory::kOther);  // CLDR "one" is exactly n==1, not n%100
    EXPECT_EQ(plural_rule_ar(102), PluralCategory::kOther);  // CLDR "two" is exactly n==2, not n%100
    EXPECT_EQ(plural_rule_ar(103), PluralCategory::kFew);   // 103 % 100 == 3
    EXPECT_EQ(plural_rule_ar(111), PluralCategory::kMany);  // 111 % 100 == 11
    EXPECT_EQ(plural_rule_ar(-1),  PluralCategory::kOther); // negative -> other
    EXPECT_EQ(plural_rule_ar(-5),  PluralCategory::kOther);
}

// -----------------------------------------------------------------------------
// 20. Arabic get_plural end-to-end: a table with all six suffixes; selecting
//     the right category for representative counts.
// -----------------------------------------------------------------------------
TEST(L10n, ArabicPluralEndToEnd)
{
    constexpr const char* kArSource =
        "items.zero={n} عنصر (صفر)\n"
        "items.one={n} عنصر\n"
        "items.two={n} عنصران\n"
        "items.few={n} عناصر\n"
        "items.many={n} عنصرًا\n"
        "items.other={n} عنصر (آخر)\n";

    StringTable ar;
    ASSERT_EQ(ar.load_from_string(kArSource, "ar"), LoadResult::kOk);

    // Arrange: manager with ar as current, no base locale needed for this test.
    L10nManager mgr;
    mgr.add_locale(std::move(ar));
    mgr.set_locale("ar");

    EXPECT_EQ(mgr.get_plural("items", 0),   "0 عنصر (صفر)");
    EXPECT_EQ(mgr.get_plural("items", 1),   "1 عنصر");
    EXPECT_EQ(mgr.get_plural("items", 2),   "2 عنصران");
    EXPECT_EQ(mgr.get_plural("items", 5),   "5 عناصر");    // few
    EXPECT_EQ(mgr.get_plural("items", 15),  "15 عنصرًا");  // many
    EXPECT_EQ(mgr.get_plural("items", 100), "100 عنصر (آخر)"); // other
}

// -----------------------------------------------------------------------------
// 21. Russian CLDR rule (plural_rule_ru) — four categories (one/few/many/other).
//     Negative n -> other (not defined by CLDR; library's convention).
// -----------------------------------------------------------------------------
TEST(L10n, RussianPluralRuleFourCategories)
{
    // kOne: n % 10 == 1 && n % 100 != 11
    EXPECT_EQ(plural_rule_ru(1),  PluralCategory::kOne);
    EXPECT_EQ(plural_rule_ru(21), PluralCategory::kOne);
    EXPECT_EQ(plural_rule_ru(101),PluralCategory::kOne);
    // n % 100 == 11 is kMany, not kOne
    EXPECT_EQ(plural_rule_ru(11), PluralCategory::kMany);

    // kFew: n % 10 in [2..4] && n % 100 not in [12..14]
    EXPECT_EQ(plural_rule_ru(2),  PluralCategory::kFew);
    EXPECT_EQ(plural_rule_ru(3),  PluralCategory::kFew);
    EXPECT_EQ(plural_rule_ru(4),  PluralCategory::kFew);
    EXPECT_EQ(plural_rule_ru(22), PluralCategory::kFew);
    // n % 100 in [12..14] is kMany, not kFew
    EXPECT_EQ(plural_rule_ru(12), PluralCategory::kMany);
    EXPECT_EQ(plural_rule_ru(13), PluralCategory::kMany);
    EXPECT_EQ(plural_rule_ru(14), PluralCategory::kMany);

    // kMany: n % 10 in [5..9] or n % 10 == 0 or n % 100 in [11..14]
    EXPECT_EQ(plural_rule_ru(5),  PluralCategory::kMany);
    EXPECT_EQ(plural_rule_ru(9),  PluralCategory::kMany);
    EXPECT_EQ(plural_rule_ru(10), PluralCategory::kMany);
    EXPECT_EQ(plural_rule_ru(20), PluralCategory::kMany);

    // negative -> kOther (library convention)
    EXPECT_EQ(plural_rule_ru(-1), PluralCategory::kOther);
    EXPECT_EQ(plural_rule_ru(-11),PluralCategory::kOther);
}

// -----------------------------------------------------------------------------
// 22. Russian get_plural end-to-end via L10nManager with "ru" rule auto-wired.
// -----------------------------------------------------------------------------
TEST(L10n, RussianPluralEndToEnd)
{
    constexpr const char* kRuSource =
        "coins.one={n} монета\n"
        "coins.few={n} монеты\n"
        "coins.many={n} монет\n"
        "coins.other={n} монет\n"; // negative fallback

    StringTable ru;
    ASSERT_EQ(ru.load_from_string(kRuSource, "ru"), LoadResult::kOk);

    L10nManager mgr;
    mgr.add_locale(std::move(ru));
    mgr.set_locale("ru");

    EXPECT_EQ(mgr.get_plural("coins", 1),  "1 монета");   // one
    EXPECT_EQ(mgr.get_plural("coins", 2),  "2 монеты");   // few
    EXPECT_EQ(mgr.get_plural("coins", 5),  "5 монет");    // many
    EXPECT_EQ(mgr.get_plural("coins", 11), "11 монет");   // many (%-100 11)
    EXPECT_EQ(mgr.get_plural("coins", 21), "21 монета");  // one (21%10==1)
    EXPECT_EQ(mgr.get_plural("coins", 22), "22 монеты");  // few
}

// -----------------------------------------------------------------------------
// 23. plural_rule_for pre-baked set now includes ar and ru.
// -----------------------------------------------------------------------------
TEST(L10n, PluralRuleForPrebakedArAndRu)
{
    L10nManager mgr;
    EXPECT_EQ(mgr.plural_rule_for("ar"), &plural_rule_ar);
    EXPECT_EQ(mgr.plural_rule_for("ru"), &plural_rule_ru);
}

// -----------------------------------------------------------------------------
// 24. StringTable::set() and StringTable::clear() direct manipulation.
// -----------------------------------------------------------------------------
TEST(L10n, StringTableSetAndClear)
{
    StringTable t;
    // Arrange: programmatic construction via set().
    t.set("greet", "Bonjour");
    t.set("farewell", "Au revoir");
    // locale is still empty after set(); must be loaded with load_from_string.
    EXPECT_EQ(t.size(), 2U);
    EXPECT_FALSE(t.empty());
    EXPECT_EQ(t.get("greet").value_or(""), "Bonjour");
    EXPECT_EQ(t.get("farewell").value_or(""), "Au revoir");

    // Act: clear wipes everything.
    t.clear();
    EXPECT_EQ(t.size(), 0U);
    EXPECT_TRUE(t.empty());
    EXPECT_FALSE(t.get("greet").has_value());
    EXPECT_TRUE(t.locale_code().empty());
}

// -----------------------------------------------------------------------------
// 25. L10nManager::has_locale() and accessor correctness.
// -----------------------------------------------------------------------------
TEST(L10n, ManagerHasLocaleAndAccessors)
{
    StringTable en;
    ASSERT_EQ(en.load_from_string("greet=Hello\n", "en"), LoadResult::kOk);

    L10nManager mgr;
    mgr.set_base_locale("en");
    EXPECT_EQ(mgr.base_locale(), "en");
    EXPECT_EQ(mgr.current_locale(), "");

    EXPECT_FALSE(mgr.has_locale("en"));
    mgr.add_locale(std::move(en));
    EXPECT_TRUE(mgr.has_locale("en"));
    EXPECT_FALSE(mgr.has_locale("fr"));

    mgr.set_locale("en");
    EXPECT_EQ(mgr.current_locale(), "en");
}

// -----------------------------------------------------------------------------
// 26. add_locale replaces an existing table for the same code.
// -----------------------------------------------------------------------------
TEST(L10n, AddLocaleReplacesExisting)
{
    StringTable en1;
    StringTable en2;
    ASSERT_EQ(en1.load_from_string("greet=Hello\n", "en"), LoadResult::kOk);
    ASSERT_EQ(en2.load_from_string("greet=Hi\n", "en"), LoadResult::kOk);

    L10nManager mgr;
    mgr.set_base_locale("en");
    mgr.add_locale(std::move(en1));
    mgr.set_locale("en");
    EXPECT_EQ(mgr.get("greet"), "Hello");

    mgr.add_locale(std::move(en2));
    EXPECT_EQ(mgr.get("greet"), "Hi");  // table replaced
}

// -----------------------------------------------------------------------------
// 27. get() and get_plural() when no table is registered for current locale:
//     falls through to base locale without crashing.
// -----------------------------------------------------------------------------
TEST(L10n, GetFallsToBaseWhenCurrentTableMissing)
{
    StringTable en;
    ASSERT_EQ(en.load_from_string("greet=Hello\napples.one={n} apple\napples.other={n} apples\n",
                                   "en"),
              LoadResult::kOk);

    L10nManager mgr;
    mgr.set_base_locale("en");
    mgr.add_locale(std::move(en));
    // Set an active locale with NO registered table.
    mgr.set_locale("fr");

    EXPECT_EQ(mgr.get("greet"), "Hello");           // falls to "en" base
    EXPECT_EQ(mgr.get_plural("apples", 1), "1 apple");
    EXPECT_EQ(mgr.get_plural("apples", 5), "5 apples");
}

// -----------------------------------------------------------------------------
// 28. get() when BOTH current and base locale tables are missing: returns key.
// -----------------------------------------------------------------------------
TEST(L10n, GetReturnsKeyWhenNoTablesAtAll)
{
    L10nManager mgr;
    mgr.set_base_locale("en");
    mgr.set_locale("tr");

    EXPECT_EQ(mgr.get("any.key"),          "any.key");
    EXPECT_EQ(mgr.get_plural("items", 5),  "items");
}

// -----------------------------------------------------------------------------
// 29. StringTable::load() with empty locale code returns kEmptyLocale
//     (the early-exit path in load() distinct from load_from_string's path).
// -----------------------------------------------------------------------------
TEST(L10n, LoadDiskEmptyLocaleReturnsTag)
{
    StringTable t;
    const LoadResult r = t.load("irrelevant_path.lang", "");
    EXPECT_EQ(r, LoadResult::kEmptyLocale);
    EXPECT_TRUE(t.empty());
    EXPECT_TRUE(t.locale_code().empty());
}

// -----------------------------------------------------------------------------
// 30. Malformed lines (no '=', empty key after trim) are silently skipped;
//     valid lines in the same file still load correctly.
// -----------------------------------------------------------------------------
TEST(L10n, MalformedLinesSkipped)
{
    constexpr const char* kMixed =
        "valid_key=valid value\n"
        "no_equals_at_all\n"          // malformed: no '='
        "=starts_with_eq\n"           // empty key after trim -> skipped
        "   \t  \n"                   // whitespace-only blank line
        "another=ok\n";

    StringTable t;
    ASSERT_EQ(t.load_from_string(kMixed, "en"), LoadResult::kOk);
    EXPECT_EQ(t.get("valid_key").value_or(""), "valid value");
    EXPECT_EQ(t.get("another").value_or(""), "ok");
    EXPECT_FALSE(t.get("no_equals_at_all").has_value());
    EXPECT_FALSE(t.get("").has_value());
    // Exactly the two valid entries.
    EXPECT_EQ(t.size(), 2U);
}

// -----------------------------------------------------------------------------
// 31. load_from_string is idempotent: a second call on the same StringTable
//     replaces all prior content (no stale entries survive).
// -----------------------------------------------------------------------------
TEST(L10n, LoadFromStringIsIdempotent)
{
    StringTable t;
    ASSERT_EQ(t.load_from_string("key_a=first\nkey_b=also_first\n", "en"),
              LoadResult::kOk);
    EXPECT_EQ(t.size(), 2U);

    // Second load: only key_a present, different value.
    ASSERT_EQ(t.load_from_string("key_a=second\n", "fr"), LoadResult::kOk);
    EXPECT_EQ(t.locale_code(), "fr");
    EXPECT_EQ(t.size(), 1U);
    EXPECT_EQ(t.get("key_a").value_or(""), "second");
    EXPECT_FALSE(t.get("key_b").has_value()); // gone: not in second load
}

// -----------------------------------------------------------------------------
// 32. Observer fires with the correct new locale and multiple observers
//     can coexist; firing order matches registration order.
// -----------------------------------------------------------------------------
TEST(L10n, MultipleObserversFireInOrder)
{
    L10nManager mgr;

    std::vector<std::string> log_a;
    std::vector<std::string> log_b;
    mgr.on_locale_change([&log_a](const std::string& c) { log_a.push_back(c); });
    mgr.on_locale_change([&log_b](const std::string& c) { log_b.push_back(c); });
    EXPECT_EQ(mgr.observer_count(), 2U);

    mgr.set_locale("en");
    mgr.set_locale("ar");

    ASSERT_EQ(log_a.size(), 2U);
    ASSERT_EQ(log_b.size(), 2U);
    EXPECT_EQ(log_a[0], "en");
    EXPECT_EQ(log_a[1], "ar");
    EXPECT_EQ(log_b[0], "en");
    EXPECT_EQ(log_b[1], "ar");
}

// -----------------------------------------------------------------------------
// 33. Observer that removes itself during the broadcast does NOT crash and does
//     NOT cause a double-fire (snapshot-copy contract in set_locale).
// -----------------------------------------------------------------------------
TEST(L10n, ObserverRemovesSelfDuringBroadcastIsSafe)
{
    L10nManager mgr;
    int fire_count = 0;
    L10nManager::ObserverId self_id {};

    self_id = mgr.on_locale_change([&](const std::string&) {
        ++fire_count;
        mgr.remove_observer(self_id); // remove self mid-broadcast
    });

    mgr.set_locale("en"); // fires once, removes self
    EXPECT_EQ(fire_count, 1);
    EXPECT_EQ(mgr.observer_count(), 0U);

    mgr.set_locale("tr"); // fires zero times: observer already removed
    EXPECT_EQ(fire_count, 1);
}

// -----------------------------------------------------------------------------
// 34. is_rtl: comprehensive RTL / LTR boundary checks including Yiddish ("yi"),
//     Pashto ("ps"), underscore separator, and multi-char unknown subtags.
// -----------------------------------------------------------------------------
TEST(L10n, RtlComprehensiveBoundary)
{
    // Known RTL locales.
    EXPECT_TRUE(is_rtl("ar"));
    EXPECT_TRUE(is_rtl("he"));
    EXPECT_TRUE(is_rtl("fa"));
    EXPECT_TRUE(is_rtl("ur"));

    // Regional subtag variants — underscore separator.
    EXPECT_TRUE(is_rtl("ar_EG"));
    EXPECT_TRUE(is_rtl("he_IL"));
    EXPECT_TRUE(is_rtl("fa_AF"));
    EXPECT_TRUE(is_rtl("ur_PK"));

    // Mixed-case input.
    EXPECT_TRUE(is_rtl("AR"));
    EXPECT_TRUE(is_rtl("HE-IL"));

    // LTR / unknown locales.
    EXPECT_FALSE(is_rtl("en"));
    EXPECT_FALSE(is_rtl("zh"));
    EXPECT_FALSE(is_rtl("ja"));
    EXPECT_FALSE(is_rtl("ko"));
    EXPECT_FALSE(is_rtl("ru"));
    EXPECT_FALSE(is_rtl("de"));
    EXPECT_FALSE(is_rtl(""));      // empty
    EXPECT_FALSE(is_rtl("a"));     // single char not in set
    EXPECT_FALSE(is_rtl("zz-ZZ")); // unknown two-letter code
}

// -----------------------------------------------------------------------------
// 35. plural_suffix() covers all six PluralCategory values and never panics
//     on the out-of-switch default path (UB guard).
// -----------------------------------------------------------------------------
TEST(L10n, PluralSuffixAllCategories)
{
    using cd::game::l10n::plural_suffix;
    EXPECT_EQ(plural_suffix(PluralCategory::kZero),  "zero");
    EXPECT_EQ(plural_suffix(PluralCategory::kOne),   "one");
    EXPECT_EQ(plural_suffix(PluralCategory::kTwo),   "two");
    EXPECT_EQ(plural_suffix(PluralCategory::kFew),   "few");
    EXPECT_EQ(plural_suffix(PluralCategory::kMany),  "many");
    EXPECT_EQ(plural_suffix(PluralCategory::kOther), "other");
}

// -----------------------------------------------------------------------------
// 36. get_plural falls back to .other when only .other is present,
//     for ALL six target categories (locks the universal-default contract).
// -----------------------------------------------------------------------------
TEST(L10n, PluralOtherFallbackForAllCategories)
{
    // Table has only .other; every category must fall back to it.
    StringTable t;
    ASSERT_EQ(t.load_from_string("x.other={n} items\n", "ar"), LoadResult::kOk);

    // Arabic rule produces all six categories for different n values.
    EXPECT_EQ(t.get_plural("x", 0,  &plural_rule_ar).value_or(""), "0 items"); // zero->other
    EXPECT_EQ(t.get_plural("x", 1,  &plural_rule_ar).value_or(""), "1 items"); // one->other
    EXPECT_EQ(t.get_plural("x", 2,  &plural_rule_ar).value_or(""), "2 items"); // two->other
    EXPECT_EQ(t.get_plural("x", 5,  &plural_rule_ar).value_or(""), "5 items"); // few->other
    EXPECT_EQ(t.get_plural("x", 15, &plural_rule_ar).value_or(""), "15 items");// many->other
    EXPECT_EQ(t.get_plural("x", 100,&plural_rule_ar).value_or(""), "100 items");// other directly
}

// -----------------------------------------------------------------------------
// 37. get_plural: key present in base locale but NOT in current locale; the
//     manager falls back to the base locale's plural rule (not the current's).
// -----------------------------------------------------------------------------
TEST(L10n, GetPluralFallsToBaseWithBaseRule)
{
    // "ar" table has all six forms; "fr" table has none.
    constexpr const char* kArFull =
        "items.zero=0 عنصر\n"
        "items.one=1 عنصر\n"
        "items.two=2 عنصران\n"
        "items.few={n} عناصر\n"
        "items.many={n} عنصرًا\n"
        "items.other={n} عنصر\n";

    StringTable ar;
    StringTable fr;
    ASSERT_EQ(ar.load_from_string(kArFull, "ar"), LoadResult::kOk);
    ASSERT_EQ(fr.load_from_string("other_key=autre\n", "fr"), LoadResult::kOk);

    L10nManager mgr;
    mgr.set_base_locale("ar");
    mgr.add_locale(std::move(ar));
    mgr.add_locale(std::move(fr));
    mgr.set_locale("fr"); // current locale lacks "items.*"

    // Falls to base "ar" using ar rule: n=5 -> kFew
    EXPECT_EQ(mgr.get_plural("items", 5), "5 عناصر");
    // n=15 -> kMany
    EXPECT_EQ(mgr.get_plural("items", 15), "15 عنصرًا");
}

// -----------------------------------------------------------------------------
// 38. Value containing '=' characters is preserved verbatim (URL / template).
// -----------------------------------------------------------------------------
TEST(L10n, ValueWithEqualsSignsPreserved)
{
    StringTable t;
    ASSERT_EQ(t.load_from_string(
        "url=https://example.com/?a=1&b=2\n"
        "expr=x==y || z=0\n",
        "en"),
        LoadResult::kOk);
    EXPECT_EQ(t.get("url").value_or(""),  "https://example.com/?a=1&b=2");
    EXPECT_EQ(t.get("expr").value_or(""), "x==y || z=0");
}
