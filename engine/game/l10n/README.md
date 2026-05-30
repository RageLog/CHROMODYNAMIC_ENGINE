# cd::game_l10n

## Purpose
Localization runtime for the Phase-G gameplay tier. Loads `.lang` key/value
tables, selects plural forms via Unicode CLDR plural rules (English + Turkish
baked in, additional locales pluggable), detects right-to-left scripts, and
walks a current-locale -> base-locale fallback chain on every lookup.

Phase 501 / G4.3 in the engine roadmap.

## Namespace
`cd::game::l10n`

## Public headers
- `include/cd/game/l10n/L10n.hpp` — `PluralCategory`, `PluralRuleFn`,
  `plural_rule_en`, `plural_rule_tr`, `is_rtl`, `LoadResult`, `StringTable`,
  `LocaleChangeCallback`, `L10nManager`.

## Primary types
| Type                 | Role                                                                |
|----------------------|---------------------------------------------------------------------|
| `StringTable`        | One locale's `key=value` entries with plural-suffix lookup.         |
| `L10nManager`        | Multi-locale registry + current/base locale + fallback chain.       |
| `PluralCategory`     | CLDR's six plural categories (`kZero` / `kOne` / `kTwo` / `kFew` / `kMany` / `kOther`). |
| `PluralRuleFn`       | `PluralCategory (*)(int n) noexcept` — per-locale selector.         |
| `LocaleChangeCallback` | `void(const std::string&)` invoked when the active locale changes. |
| `LoadResult`         | `kOk` / `kFileNotFound` / `kEmptyLocale` table-load tags.           |

## File format (`.lang`)
One entry per line, `key=value`. The first `=` splits key from value (later
`=` characters become part of the value). Lines whose first non-whitespace
character is `#` are comments and skipped. Blank lines are skipped. Plural
keys carry a CLDR suffix:

```
# Greetings
greet=Hello
farewell=Goodbye

# Plural variants
apples.one   = {n} apple
apples.other = {n} apples
```

The `{n}` placeholder is the only substitution `get_plural()` performs — it
is replaced with the integer count rendered as a decimal. There is no escape
syntax; the format is deliberately trivial so localisation bundles stay
human-editable.

## CLDR plural rules
Two locales are baked in for the Phase G4.3 contract; additional rules are
registered via `L10nManager::set_plural_rule(code, fn)`:

- **`plural_rule_en`** — `n == 1` -> `kOne`, otherwise `kOther` (CLDR v45
  plurals.xml, locale `en`).
- **`plural_rule_tr`** — every `n` -> `kOther` (Turkish marks plurality on
  the noun via a suffix, not via separate forms).

`L10nManager::get_plural` consults the current locale's rule, falls back to
the base locale's rule (with the base locale's table) on a miss, and
returns the key verbatim if both tables miss.

## RTL detection
`is_rtl(locale)` extracts the leading language subtag (everything up to the
first `-` / `_`), lower-cases it, and returns true for `ar`, `he`, `fa`,
`ur`. Everything else — including unknown / empty input — returns false.
Reference: Unicode UAX #9 (Bidirectional Algorithm).

## Usage example
```cpp
#include <cd/game/l10n/L10n.hpp>

using namespace cd::game::l10n;

StringTable en;
StringTable tr;
en.load_from_string(R"(
greet=Hello
apples.one={n} apple
apples.other={n} apples
)", "en");
tr.load_from_string(R"(
greet=Merhaba
apples.other={n} elma
)", "tr");

L10nManager mgr;
mgr.set_base_locale("en");
mgr.add_locale(std::move(en));
mgr.add_locale(std::move(tr));

mgr.on_locale_change([](const std::string& code) {
    // Re-render UI for new locale.
});

mgr.set_locale("tr");
mgr.get("greet");            // "Merhaba"
mgr.get_plural("apples", 5); // "5 elma"

mgr.set_locale("en");
mgr.get_plural("apples", 1); // "1 apple"
mgr.get_plural("apples", 5); // "5 apples"

is_rtl("ar-EG");             // true
is_rtl("en");                // false
```

## Build / Test
```bash
cmake --build --preset ninja-debug --target cd_game_l10n
ctest --preset ninja-debug -R game_l10n --output-on-failure
```

## Dependencies
- `cd::core` — `Defines.hpp` only at the header level. Standard library
  `<fstream>` is the sole I/O dependency in the implementation.

## References
- Unicode Consortium. *CLDR Plural Rules v45*. Unicode Common Locale Data
  Repository (<https://cldr.unicode.org/index/cldr-spec/plural-rules>),
  accessed 2026.
- GNU. *GNU gettext utilities — Plural forms*. The gettext Manual
  (<https://www.gnu.org/software/gettext/manual/html_node/Plural-forms.html>),
  accessed 2026.
- Mozilla. *Project Fluent — Plurals and Selectors*
  (<https://projectfluent.org/fluent/guide/selectors.html>), accessed 2026.
- Becker, Mark. *Bidirectional Algorithm (UAX #9)*. Unicode Standard Annex
  (<https://unicode.org/reports/tr9/>), accessed 2026 — canonical RTL
  locale list.

## Notes
- Single translation unit (`src/L10n.cpp`) carries StringTable, L10nManager,
  the parser, and the CLDR rule functions.
- Thread-safety: NOT thread-safe by design — one manager per owning thread
  (typically the UI / engine main thread). Observers fire on the thread that
  called `set_locale()`.
- The library never throws. All failure paths use return tags
  (`LoadResult`) or return the key verbatim (`get` / `get_plural` double-miss)
  so the UI cannot crash on stale localisation assets.
- Future work (out of scope for G4.3): full ICU MessageFormat (gender,
  ordinal forms, currency / date formatters), CLDR rule packs beyond en/tr,
  `.po` / `.xliff` loaders, hot-reload watcher.
