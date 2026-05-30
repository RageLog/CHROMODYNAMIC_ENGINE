// =============================================================================
// CHROMODYNAMIC - cd/game/l10n/L10n.hpp
// Phase 501 - cd::game::l10n (G4.3: Localization with CLDR plurals + RTL +
//             fallback chain)
//
// A small, allocation-light localization runtime modelled on the contract
// shared by GNU gettext, ICU MessageFormat (Unicode CLDR plural rules
// v45) and Mozilla Fluent. The three building blocks are:
//
//   * StringTable     -- one .lang file (one locale) loaded from disk into a
//                        key/value store keyed by stable identifier. CLDR
//                        plural suffixes (".one", ".other", ".few", ".many",
//                        ".two", ".zero") are recognised on top of base
//                        keys; selecting the correct plural form is delegated
//                        to a per-locale rule function.
//
//   * L10nManager     -- owns N locales' tables, a current locale, and a base
//                        (fallback) locale. `get(key)` consults the current
//                        locale first and walks back to the base locale on a
//                        miss; the key itself is returned on a double-miss so
//                        the UI never crashes on stale localisation data.
//                        Observers registered via `on_locale_change` fire
//                        every time `set_locale()` actually changes the
//                        active locale.
//
//   * is_rtl(locale)  -- script-direction helper. True for Arabic ("ar"),
//                        Hebrew ("he"), Farsi/Persian ("fa") and Urdu ("ur");
//                        false for every other locale tag. Used by UI tier to
//                        flip glyph runs / paragraph direction.
//
// Why bake CLDR rules in (only for "en" and "tr") instead of pulling ICU?
// ----------------------------------------------------------------------
// The Phase-G gameplay tier sits above cd::core ONLY (CLAUDE.md S7). Linking
// ICU just for plural-selection would drag a 30+ MB transitive dependency
// chain into every cd::game::* binary. CLDR v45's rule set for "en" / "tr"
// is documented in `plurals.xml` (Unicode 2024) and reduces to a couple of
// integer checks per call; we bake the rules directly here. Adding more
// locales is a one-function-per-locale change inside L10n.cpp + a register
// call in L10nManager's ctor. See the per-locale comment block in L10n.cpp.
//
// File format ("key=value" .lang)
// -------------------------------
// One entry per line: <key>=<value>. The first '=' splits key from value
// (later '=' characters become part of the value). Lines beginning with '#'
// are comments and skipped. Blank lines are skipped. Whitespace around the
// key is trimmed; the value is taken verbatim after the first '='. Plural
// keys use suffixes (Unicode CLDR plural categories):
//
//     apples.one   = {n} apple
//     apples.other = {n} apples
//     apples.few   = {n} apples       # used by langs with a "few" category
//     apples.many  = {n} apples       # likewise "many"
//     apples.two   = {n} apples       # likewise "two"
//     apples.zero  = {n} apples       # likewise "zero"
//
// The "{n}" placeholder is substituted with the integer count on
// `get_plural(key, n)`; it is purely textual -- no parser, no escape
// sequences. Substitution is the only transformation get_plural performs.
//
// Threading: instances of StringTable and L10nManager are NOT thread-safe.
// Treat them like UI-thread singletons; the locale-change broadcast fires
// on the thread that called set_locale().
//
// Dependencies (CLAUDE.md S7): cd::core only at the header level.
//
// Design references:
//   * Unicode Consortium. "CLDR Plural Rules v45", Unicode Common Locale
//     Data Repository (https://cldr.unicode.org/index/cldr-spec/plural-rules),
//     accessed 2026.
//   * GNU. "GNU gettext utilities -- Plural forms", The gettext Manual
//     (https://www.gnu.org/software/gettext/manual/html_node/Plural-forms.html),
//     accessed 2026.
//   * Mozilla. "Project Fluent -- Plurals and Selectors"
//     (https://projectfluent.org/fluent/guide/selectors.html), accessed 2026.
//   * Becker, Mark. "BiDi Algorithm UAX #9", Unicode Standard Annex
//     (https://unicode.org/reports/tr9/), accessed 2026 -- used to derive
//     the canonical RTL locale list.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace cd::game::l10n
{

// -----------------------------------------------------------------------------
// PluralCategory - the six CLDR plural categories. Every locale uses a subset
// (English uses {one, other}; Turkish uses {other}; Arabic uses all six).
// The selector function for a locale maps an integer count to one of these.
// -----------------------------------------------------------------------------
enum class PluralCategory : std::uint8_t
{
    kZero  = 0,
    kOne   = 1,
    kTwo   = 2,
    kFew   = 3,
    kMany  = 4,
    kOther = 5,
};

/// Canonical lowercase suffix for a PluralCategory ("one", "other", ...).
/// Used by StringTable to key plural variants on disk.
CD_NODISCARD std::string_view plural_suffix(PluralCategory cat) noexcept;

// -----------------------------------------------------------------------------
// PluralRuleFn - locale-specific plural selector. Takes a non-negative integer
// count, returns the CLDR category to look up. The function MUST be pure
// (no captures, no side effects) so that L10nManager can swap rules across
// locales by pointer.
// -----------------------------------------------------------------------------
using PluralRuleFn = PluralCategory (*)(int n) noexcept;

/// CLDR plural rule for English ("en"):
///   n == 1                  -> kOne
///   anything else (incl. 0) -> kOther
/// (CLDR v45 plurals.xml, locale "en".)
CD_NODISCARD PluralCategory plural_rule_en(int n) noexcept;

/// CLDR plural rule for Turkish ("tr"):
///   any n -> kOther
/// Turkish has a single plural form; the language marks plurality via the
/// "-ler"/"-lar" suffix at the noun level, not via separate forms.
/// (CLDR v45 plurals.xml, locale "tr".)
CD_NODISCARD PluralCategory plural_rule_tr(int n) noexcept;

// -----------------------------------------------------------------------------
// is_rtl - script-direction helper. Returns true for locales whose primary
// script is right-to-left (Arabic, Hebrew, Persian/Farsi, Urdu). Returns
// false for every other locale tag, including unknown ones. The comparison
// matches the leading language subtag (everything up to '-' or '_'), so
// "ar-EG", "ar_SA" and "ar" all return true.
//
// Reference: Unicode UAX #9 (BiDi). The canonical list of RTL scripts is
// Arabic, Hebrew, Syriac, Thaana, N'Ko, ...; this helper exposes the four
// that map to the localisation tags games realistically ship.
// -----------------------------------------------------------------------------
CD_NODISCARD bool is_rtl(std::string_view locale) noexcept;

// -----------------------------------------------------------------------------
// StringTable - one locale's worth of strings, loaded from a flat .lang file.
//
// Storage is a single `unordered_map<string, string>`; plural variants are
// keyed with the full suffixed key ("apples.one", "apples.other") so that
// `find(key + "." + suffix)` is the natural lookup path.
//
// Lifetime: tables are typically owned by an L10nManager, but standalone use
// is supported -- the class has no hidden global state.
//
// Errors: `load()` reports success / failure via `LoadResult`. A failed load
// leaves the table empty (atomic semantics, like cd::game::dialogue VM).
// -----------------------------------------------------------------------------
enum class LoadResult : std::uint8_t
{
    kOk           = 0,
    kFileNotFound = 1,  ///< the requested path could not be opened
    kEmptyLocale  = 2,  ///< locale code was empty
};

class StringTable
{
public:
    StringTable() = default;

    /// Load a .lang file from disk for the given locale. Pre-existing
    /// entries are cleared first; a failed load leaves the table empty.
    /// `locale_code` is stored verbatim and used by L10nManager to key the
    /// table; it is never parsed (use raw "en", "tr", "ar-EG", ...).
    LoadResult load(const std::string& path, std::string locale_code);

    /// Parse-from-memory variant used by tests and bundled-string asset
    /// pipelines. Same contract as `load()`; the caller supplies the file
    /// contents directly. `locale_code` must be non-empty.
    LoadResult load_from_string(std::string_view source, std::string locale_code);

    /// Plain string lookup. Returns `std::nullopt` on a miss; never throws.
    /// Callers normally route through L10nManager::get to pick up the
    /// fallback chain -- direct StringTable::get exists for debug / editor
    /// tooling that needs to inspect a single locale's coverage.
    CD_NODISCARD std::optional<std::string> get(const std::string& key) const;

    /// CLDR plural lookup. Uses the supplied `rule` to map `n` to a category,
    /// then looks up `key + "." + suffix(category)`. On a miss, falls back to
    /// `key + ".other"`; on a second miss, returns std::nullopt. The "{n}"
    /// placeholder in the returned string is substituted with the integer
    /// count (decimal, no separators).
    CD_NODISCARD std::optional<std::string>
        get_plural(const std::string& key, int n, PluralRuleFn rule) const;

    CD_NODISCARD const std::string& locale_code() const noexcept { return locale_; }
    CD_NODISCARD std::size_t        size()        const noexcept { return entries_.size(); }
    CD_NODISCARD bool               empty()       const noexcept { return entries_.empty(); }

    /// Direct entry setter for unit tests / programmatic table construction.
    void set(std::string key, std::string value) { entries_[std::move(key)] = std::move(value); }

    void clear() noexcept;

private:
    std::string                                  locale_  {};
    std::unordered_map<std::string, std::string> entries_ {};
};

// -----------------------------------------------------------------------------
// L10nManager - holds multiple StringTables (one per locale) and a fallback
// chain. The fallback chain is rooted at `base_locale` and walked when a key
// is absent from the current locale.
//
// API shape mirrors GNU gettext's `bind_textdomain` + `setlocale`:
//
//     L10nManager mgr;
//     mgr.set_base_locale("en");
//     mgr.add_locale(std::move(en_table));   // "en"
//     mgr.add_locale(std::move(tr_table));   // "tr"
//     mgr.set_locale("tr");                  // active locale
//     auto s = mgr.get("greet");             // tr first, then en, then "greet"
//
// Plural rules: each locale's rule is registered via `set_plural_rule(code, fn)`
// or comes pre-baked from L10nManager's ctor for "en" and "tr". Unknown
// locales fall back to `plural_rule_en` (one/other) which matches the CLDR
// "default" category set and is the safest neutral choice for the brief's
// "no crash on missing rule" contract.
//
// Observer broadcast: any callback registered via `on_locale_change` fires
// the moment `set_locale()` mutates the active locale (no-op when the new
// locale equals the old one). Observers receive the *new* locale code.
// -----------------------------------------------------------------------------
using LocaleChangeCallback = std::function<void(const std::string& new_locale)>;

class L10nManager
{
public:
    /// Ctor pre-registers "en" / "tr" plural rules; no tables are loaded.
    L10nManager();

    // ------- locale & fallback wiring -------------------------------------

    /// Install a loaded StringTable; replaces any previous table with the
    /// same locale code. Tables are owned by the manager.
    void add_locale(StringTable table);

    /// Set the base (fallback) locale. The base locale is consulted when the
    /// current locale lacks a key. Setting this is independent from
    /// `set_locale()` -- a typical workflow installs base = "en" once and
    /// toggles `set_locale()` between user-chosen locales at runtime.
    void set_base_locale(std::string code) { base_locale_ = std::move(code); }

    /// Set the active locale. Broadcasts to observers when this actually
    /// changes the value. An unknown code is still accepted -- the lookup
    /// path will fall through to the base locale on every key.
    void set_locale(std::string code);

    CD_NODISCARD const std::string& base_locale() const noexcept { return base_locale_; }
    CD_NODISCARD const std::string& current_locale() const noexcept { return current_locale_; }

    /// True if a StringTable is registered for the given locale code.
    CD_NODISCARD bool has_locale(const std::string& code) const noexcept
    {
        return tables_.find(code) != tables_.end();
    }

    // ------- plural rules --------------------------------------------------

    /// Register (or replace) the plural rule for a locale.
    void set_plural_rule(const std::string& code, PluralRuleFn rule)
    {
        rules_[code] = rule;
    }

    /// Look up the plural rule for a locale. Falls back to `plural_rule_en`
    /// for unknown locales; never returns nullptr.
    CD_NODISCARD PluralRuleFn plural_rule_for(const std::string& code) const noexcept;

    // ------- lookup --------------------------------------------------------

    /// Localised string lookup. Walks current_locale -> base_locale -> key.
    /// Returns the key itself on a double-miss so callers see the missing
    /// id verbatim rather than crashing on `unwrap()` of an empty optional.
    CD_NODISCARD std::string get(const std::string& key) const;

    /// Plural-aware lookup. Uses the current locale's plural rule on the
    /// current-locale table; falls back to the base locale's table (with its
    /// own plural rule); falls back to the key itself.
    CD_NODISCARD std::string get_plural(const std::string& key, int n) const;

    // ------- observers -----------------------------------------------------

    using ObserverId = std::uint32_t;

    /// Register a callback fired whenever `set_locale()` changes the value.
    /// Returns an opaque id usable with `remove_observer`. The callback is
    /// invoked synchronously on the thread that called `set_locale()`.
    ObserverId on_locale_change(LocaleChangeCallback cb);

    /// Detach a previously registered observer. Unknown id = no-op.
    void remove_observer(ObserverId id);

    CD_NODISCARD std::size_t observer_count() const noexcept { return observers_.size(); }

private:
    struct Observer
    {
        ObserverId           id {0};
        LocaleChangeCallback cb {};
    };

    CD_NODISCARD const StringTable* find_table(const std::string& code) const noexcept;

    std::unordered_map<std::string, StringTable>  tables_         {};
    std::unordered_map<std::string, PluralRuleFn> rules_          {};
    std::string                                   base_locale_    {};
    std::string                                   current_locale_ {};
    std::vector<Observer>                         observers_      {};
    ObserverId                                    next_id_        {1};
};

}  // namespace cd::game::l10n
