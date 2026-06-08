// =============================================================================
// CHROMODYNAMIC - cd/game/l10n/L10n.cpp
// Phase 501 - cd::game::l10n implementation (G4.3).
//
// Implementation notes:
//
// * Parser: single forward scan. Lines are split on '\n'; the trailing '\r'
//   of a CRLF source is stripped before processing. Comments (lines whose
//   first non-whitespace character is '#') and blank lines are skipped. The
//   first '=' separates key from value; whitespace around the key is trimmed,
//   the value is taken verbatim after the '='. Malformed lines (no '=') are
//   silently skipped rather than aborting the whole load -- a localisation
//   asset bundle MUST NOT brick the game over one stray line.
//
// * Plural lookup precedence: `key + "." + suffix(category)` -> `key.other`
//   -> nullopt. The fallback to ".other" matches CLDR's recommendation that
//   .other is the universal default category in every locale.
//
// * is_rtl() walks the leading language subtag (everything up to '-' or '_')
//   and matches against the four RTL tags in the brief. Anything else,
//   including unknown / empty input, returns false.
//
// * L10nManager::get_plural walks two tables (current + base) with the
//   respective per-locale plural rules; the integer count is substituted
//   into the resulting template via a plain "{n}" -> "<n>" replacement,
//   done once at lookup time. No format-string parser; this is by design.
// =============================================================================
#include <cd/game/l10n/L10n.hpp>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace cd::game::l10n
{

// =============================================================================
// CLDR plural rule helpers
// =============================================================================

std::string_view plural_suffix(PluralCategory cat) noexcept
{
    switch (cat)
    {
        case PluralCategory::kZero:  return "zero";
        case PluralCategory::kOne:   return "one";
        case PluralCategory::kTwo:   return "two";
        case PluralCategory::kFew:   return "few";
        case PluralCategory::kMany:  return "many";
        case PluralCategory::kOther: return "other";
    }
    return "other";
}

PluralCategory plural_rule_en(int n) noexcept
{
    // CLDR v45 plurals.xml, locale "en": n == 1 -> one ; otherwise other.
    return (n == 1) ? PluralCategory::kOne : PluralCategory::kOther;
}

PluralCategory plural_rule_tr(int /*n*/) noexcept
{
    // CLDR v45 plurals.xml, locale "tr": single form -- always other.
    return PluralCategory::kOther;
}

// =============================================================================
// is_rtl
// =============================================================================

namespace
{

// Extract the leading language subtag from a locale code: everything up to
// the first '-' / '_' / end-of-string. Returns lower-cased ASCII.
std::string leading_subtag_lower(std::string_view locale) noexcept
{
    std::size_t end = 0;
    while (end < locale.size() && locale[end] != '-' && locale[end] != '_')
    {
        ++end;
    }
    std::string out;
    out.reserve(end);
    for (std::size_t i = 0; i < end; ++i)
    {
        out.push_back(static_cast<char>(
            std::tolower(static_cast<unsigned char>(locale[i]))));
    }
    return out;
}

// Trim ASCII whitespace from both ends of a string_view.
std::string_view trim(std::string_view s) noexcept
{
    std::size_t a = 0;
    std::size_t b = s.size();
    while (a < b && (std::isspace(static_cast<unsigned char>(s[a])) != 0))
    {
        ++a;
    }
    while (b > a && (std::isspace(static_cast<unsigned char>(s[b - 1])) != 0))
    {
        --b;
    }
    return s.substr(a, b - a);
}

// Substitute "{n}" placeholders with the decimal representation of `n`.
// One-pass, no regex; matches the gettext / Fluent ergonomics where {n} is
// the only interpolation token plural strings ever need.
std::string substitute_count(std::string_view tmpl, int n)
{
    const std::string num = std::to_string(n);
    std::string out;
    out.reserve(tmpl.size() + num.size());
    std::size_t i = 0;
    while (i < tmpl.size())
    {
        if (i + 2 < tmpl.size() && tmpl[i] == '{' && tmpl[i + 1] == 'n'
            && tmpl[i + 2] == '}')
        {
            out.append(num);
            i += 3;
        }
        else
        {
            out.push_back(tmpl[i]);
            ++i;
        }
    }
    return out;
}

}  // namespace

bool is_rtl(std::string_view locale) noexcept
{
    const std::string tag = leading_subtag_lower(locale);
    return tag == "ar" || tag == "he" || tag == "fa" || tag == "ur";
}

// =============================================================================
// StringTable
// =============================================================================

void StringTable::clear() noexcept
{
    entries_.clear();
    locale_.clear();
}

LoadResult StringTable::load_from_string(std::string_view source, std::string locale_code)
{
    // Always reset to empty first so a failed/empty load leaves no half-state.
    entries_.clear();
    locale_.clear();

    if (locale_code.empty())
    {
        return LoadResult::kEmptyLocale;
    }

    std::size_t pos = 0;
    while (pos <= source.size())
    {
        const std::size_t nl  = source.find('\n', pos);
        const std::size_t end = (nl == std::string_view::npos) ? source.size() : nl;
        std::string_view  raw = source.substr(pos, end - pos);

        // Strip CR for CRLF sources.
        if (!raw.empty() && raw.back() == '\r')
        {
            raw.remove_suffix(1);
        }

        // Advance for next iteration.
        if (nl == std::string_view::npos)
        {
            pos = source.size() + 1;
        }
        else
        {
            pos = nl + 1;
        }

        const std::string_view trimmed = trim(raw);
        if (trimmed.empty())
        {
            continue;
        }
        if (trimmed.front() == '#')
        {
            continue;
        }

        const auto eq = trimmed.find('=');
        if (eq == std::string_view::npos)
        {
            // Malformed line: skip rather than abort. Bundle authoring drift
            // (a stray "TODO note") must not brick the load.
            continue;
        }

        std::string_view key_view = trim(trimmed.substr(0, eq));
        if (key_view.empty())
        {
            continue;
        }
        // The value is taken verbatim after the first '=' (allows '=' in
        // values, e.g. a URL template like "More info: https://x?a=1").
        // Whitespace immediately after '=' is preserved -- some localisations
        // need a leading space (CJK glyph spacing); we don't second-guess it.
        std::string_view val_view = trimmed.substr(eq + 1);

        entries_.emplace(std::string {key_view}, std::string {val_view});
    }

    locale_ = std::move(locale_code);
    return LoadResult::kOk;
}

LoadResult StringTable::load(const std::string& path, std::string locale_code)
{
    if (locale_code.empty())
    {
        // Even though load_from_string would also catch this, return the
        // canonical tag here so callers get the same code regardless of which
        // entry point they used.
        entries_.clear();
        locale_.clear();
        return LoadResult::kEmptyLocale;
    }

    std::ifstream in(path, std::ios::in | std::ios::binary);
    if (!in.is_open())
    {
        entries_.clear();
        locale_.clear();
        return LoadResult::kFileNotFound;
    }

    std::ostringstream ss;
    ss << in.rdbuf();
    const std::string contents = ss.str();
    return load_from_string(contents, std::move(locale_code));
}

std::optional<std::string> StringTable::get(const std::string& key) const
{
    const auto it = entries_.find(key);
    if (it == entries_.end())
    {
        return std::nullopt;
    }
    return it->second;
}

std::optional<std::string>
StringTable::get_plural(const std::string& key, int n, PluralRuleFn rule) const
{
    const PluralRuleFn r = (rule != nullptr) ? rule : &plural_rule_en;
    const PluralCategory cat = r(n);

    // First try the rule-selected suffix.
    {
        std::string composed;
        composed.reserve(key.size() + 1 + 6);
        composed.append(key);
        composed.push_back('.');
        composed.append(plural_suffix(cat));
        const auto it = entries_.find(composed);
        if (it != entries_.end())
        {
            return substitute_count(it->second, n);
        }
    }

    // Fall back to ".other" -- CLDR's universal default category.
    if (cat != PluralCategory::kOther)
    {
        std::string composed;
        composed.reserve(key.size() + 7);
        composed.append(key);
        composed.append(".other");
        const auto it = entries_.find(composed);
        if (it != entries_.end())
        {
            return substitute_count(it->second, n);
        }
    }

    return std::nullopt;
}

// =============================================================================
// L10nManager
// =============================================================================

L10nManager::L10nManager()
{
    // Pre-bake the two CLDR rules required by the brief. Additional locales
    // are added via set_plural_rule().
    rules_.emplace("en", &plural_rule_en);
    rules_.emplace("tr", &plural_rule_tr);
}

void L10nManager::add_locale(StringTable table)
{
    // We key by the table's own locale_code so the caller's load() result
    // and the registry stay in lock-step. A previously-installed table for
    // the same locale is replaced (operator[]).
    std::string code = table.locale_code();
    tables_[std::move(code)] = std::move(table);
}

void L10nManager::set_locale(std::string code)
{
    if (code == current_locale_)
    {
        return;  // no-op: observers fire only on real transitions
    }
    current_locale_ = std::move(code);

    // Snapshot callbacks before invoking; observers that remove themselves
    // during the broadcast must NOT invalidate our walk.
    std::vector<LocaleChangeCallback> snapshot;
    snapshot.reserve(observers_.size());
    for (const auto& o : observers_)
    {
        snapshot.push_back(o.cb);
    }
    for (const auto& cb : snapshot)
    {
        if (cb)
        {
            cb(current_locale_);
        }
    }
}

PluralRuleFn L10nManager::plural_rule_for(const std::string& code) const noexcept
{
    const auto it = rules_.find(code);
    if (it == rules_.end() || it->second == nullptr)
    {
        return &plural_rule_en;  // safe neutral default (one / other).
    }
    return it->second;
}

const StringTable* L10nManager::find_table(const std::string& code) const noexcept
{
    const auto it = tables_.find(code);
    if (it == tables_.end())
    {
        return nullptr;
    }
    return &it->second;
}

std::string L10nManager::get(const std::string& key) const
{
    // Try current locale.
    if (!current_locale_.empty())
    {
        const auto* tbl = find_table(current_locale_);
        if (tbl != nullptr)
        {
            auto hit = tbl->get(key);
            if (hit.has_value())
            {
                return std::move(*hit);
            }
        }
    }

    // Fall back to base locale (if distinct and registered).
    if (!base_locale_.empty() && base_locale_ != current_locale_)
    {
        const auto* tbl = find_table(base_locale_);
        if (tbl != nullptr)
        {
            auto hit = tbl->get(key);
            if (hit.has_value())
            {
                return std::move(*hit);
            }
        }
    }

    // Double-miss: return the key itself so the UI shows a debuggable token
    // instead of an empty string. Matches gettext's `_("foo")` behaviour
    // when no .mo file is loaded.
    return key;
}

std::string L10nManager::get_plural(const std::string& key, int n) const
{
    // Try current locale with its rule.
    if (!current_locale_.empty())
    {
        const auto* tbl = find_table(current_locale_);
        if (tbl != nullptr)
        {
            const PluralRuleFn rule = plural_rule_for(current_locale_);
            auto hit = tbl->get_plural(key, n, rule);
            if (hit.has_value())
            {
                return std::move(*hit);
            }
        }
    }

    // Fall back to base locale with its rule.
    if (!base_locale_.empty() && base_locale_ != current_locale_)
    {
        const auto* tbl = find_table(base_locale_);
        if (tbl != nullptr)
        {
            const PluralRuleFn rule = plural_rule_for(base_locale_);
            auto hit = tbl->get_plural(key, n, rule);
            if (hit.has_value())
            {
                return std::move(*hit);
            }
        }
    }

    // Double-miss: same convention as get().
    return key;
}

L10nManager::ObserverId L10nManager::on_locale_change(LocaleChangeCallback cb)
{
    Observer o;
    o.id = next_id_++;
    o.cb = std::move(cb);
    observers_.push_back(std::move(o));
    return observers_.back().id;
}

void L10nManager::remove_observer(ObserverId id)
{
    const auto removed = std::ranges::remove_if(observers_,
                                                 [id](const Observer& o) { return o.id == id; });
    observers_.erase(removed.begin(), removed.end());
}

}  // namespace cd::game::l10n
